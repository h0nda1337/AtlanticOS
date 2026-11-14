"""Audit native shared regions, UPL accessors and firmware RSR target objects."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess

from test_xnu_brk_scope import function, read_object, symbol_bytes
from test_xnu_platform import strong_external_import

ROOT = Path(__file__).resolve().parents[1]
SHARED = 'osfmk/vm/vm_shared_region.c'
PAGER = 'osfmk/vm/vm_shared_region_pager.c'
PAGEOUT = 'osfmk/vm/vm_pageout.c'
RSR = 'osfmk/vm/atlantic_rsr_version.c'
EXEC = 'bsd/kern/kern_exec.c'
PATCH = '0034-retain-native-shared-region-upl-and-rsr-state.patch'
UPL = ('upl_page_present', 'upl_phys_page')
RSR_APIS = ('_rsr_get_version', '_rsr_bump_version')
EXPORTS = {
    SHARED: {'_vm_commpage_init', '_vm_commpage_text_init', '_vm_commpage_enter',
             '_commpage_text64_location', '_vm_shared_region_get', '_vm_shared_region_set',
             '_vm_shared_region_enter', '_shared_region_trace_level'},
    PAGER: {'_shared_region_pager_setup', '_shared_region_pager_ops', '_shared_region_pager_reclaimed'},
    PAGEOUT: {'_'+name for name in UPL},
    RSR: set(RSR_APIS),
}
IMPORTS = {
    SHARED: {'_commpage_populate', '_commpage_text_populate', '_pmap_insert_commpage',
             '_rsr_get_version', '_shared_region_pager_setup'},
    PAGER: {'_upl_page_present', '_upl_phys_page', '_vm_shared_region_slide_page'},
}


def digest(data):
    return hashlib.sha256(data).hexdigest()


def sha(path):
    return digest(path.read_bytes())


def source_text(path):
    # The source lock covers UTF-8 and canonical LF, including on Windows.
    return path.read_text(encoding='utf-8')


def pageout_hunk(text):
    start = text.index('#ifdef MACH_BSD\n\nboolean_t\nupl_device_page(')
    end = text.index('#endif /* MACH_BSD */\n', start)+len('#endif /* MACH_BSD */\n')
    return start, end, text[start:end]


def audit_rsr_code(image):
    raw, sections, symbols = image
    version = symbols['_rsr_current_version']
    state = sections[version['section']-1]
    assert not version['type'] & 1 and version['value'] == state['address']
    assert version['value'] % 4 == 0
    assert (state['segment'], state['name'], state['size'], state['flags'] & 0xff) == ('__DATA','__bss',4,1)
    code = {}
    for name, width, offset in [('_rsr_get_version',12,4), ('_rsr_bump_version',36,4)]:
        data = symbol_bytes(image, name, width)
        code[name] = struct.unpack('<'+'I'*(width//4), data)
        entry = symbols[name]
        section = sections[entry['section']-1]
        assert (section['segment'],section['name']) == ('__TEXT','__text')
        start = entry['value']-section['address']
        first = section['relocation_offset']
        limit = first + 8*section['relocation_count']
        assert 0 <= first <= limit <= len(raw)
        relocations = [(address-start, word) for address, word in struct.iter_unpack('<II', raw[first:limit])
                       if start <= address < start+width]
        index = version['index']
        assert sorted(relocations) == [(0, 0x3d000000|index), (offset, 0x4c000000|index)]
    # Pinned Cortex-A57 target recipe. Check actual 32-bit load/increment/store,
    # the backward retry on exclusive-store failure and the original relaxed order.
    assert code['_rsr_get_version'] == (0x90000008, 0xb9400100, 0xd65f03c0)
    assert code['_rsr_bump_version'] == (
        0x90000008, 0x91000108, 0xd503201f, 0xd503201f, 0x885f7d09,
        0x11000529, 0x880a7d09, 0x35ffffaa, 0xd65f03c0)
    return {name:[f'0x{word:08x}' for word in words] for name, words in code.items()}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    build = args.build.resolve()
    base = json.loads(build.read_text())
    lock = json.loads((ROOT/'xnu-port/patches.lock.json').read_text())
    assert base['port_id'] == lock['port_id'] and not base['failed_units'] and base['layout_provider']
    assert base['upstream_commit'] == lock['upstream_commit']
    patch_hash = sha(ROOT/'xnu-port'/PATCH)
    assert patch_hash == lock['patches'][PATCH]
    for path, expected in base['inputs'].items():
        assert sha(ROOT/path) == expected, path
    source = ROOT/'work/xnu-lab-sources'/base['port_id'][:16]
    output = args.output.resolve()
    if not output.is_relative_to(ROOT/'work') or output.exists():
        parser.error('Use a new output directory under work/')
    output.mkdir(parents=True)
    commands, outcomes = [], []

    def run(name, command, data=None, expected=0, diagnostic=None):
        proc = subprocess.run(command, cwd=ROOT, input=data, text=True, capture_output=True, timeout=90)
        commands.append([str(arg).replace(str(ROOT), '<ROOT>') for arg in command])
        (output/(name+'.log')).write_text((proc.stdout+proc.stderr).replace(str(ROOT), '<ROOT>'),
                                         encoding='utf-8', newline='\n')
        assert proc.returncode == expected, name+': '+proc.stderr[-2400:]
        if diagnostic:
            assert diagnostic in proc.stderr, name
        return proc.stdout

    def passed(name, **details):
        outcomes.append({'name':name, 'passed':True, **details})

    recipe = next(command for command in base['commands'] if any(arg.endswith('/'+PAGEOUT) for arg in command))
    assert recipe[-4] == '-c'
    target = [arg.replace('<ROOT>', str(ROOT)) for arg in recipe[:-4]]
    assert '-DVM_UNSAFE_TYPES' in target
    assert sha(Path(target[0])) == base['layout_provider']['compiler_sha256']
    records = {record['source']:record for record in base['objects']}
    objects = {}
    source_hashes, object_hashes = {}, {}
    for path in EXPORTS:
        assert sha(source/path) == records[path]['source_sha256']
        obj = (build.parent/records[path]['object']).resolve()
        assert obj.is_relative_to(build.parent) and sha(obj) == records[path]['object_sha256']
        objects[path] = obj
        source_hashes[path] = sha(source/path)
        object_hashes[path] = sha(obj)
    passed('prepared-source-and-target-object-identities')

    for path in (SHARED, PAGER, EXEC):
        assert sha(source/path) == digest(source_text(ROOT/'xnu-upstream'/path).encode())
    passed('full-native-shared-region-sources-and-bsd-exec-unchanged')

    locked = {entry['path']:entry for entry in lock['canonical_lf_files']}
    original = source_text(ROOT/'xnu-upstream'/PAGEOUT)
    modified = source_text(source/PAGEOUT)
    assert digest(original.encode()) == locked[PAGEOUT]['preimage_sha256']
    assert source_hashes[PAGEOUT] == locked[PAGEOUT]['postimage_sha256']
    old_start, old_end, old_hunk = pageout_hunk(original)
    start = modified.index('#ifdef MACH_BSD\n\nboolean_t\nupl_device_page(')
    end_marker = '#endif /* MACH_BSD */\n'
    suffix = original[old_end:]
    assert modified.startswith(original[:old_start]) and modified.endswith(suffix)
    new_hunk = modified[start:len(modified)-len(suffix)]
    assert new_hunk.endswith(end_marker)
    reconstructed = modified
    for name in UPL:
        body = function(original,name)
        assert body == function(modified,name)
        declaration = ('boolean_t\n' if name == UPL[0] else 'ppnum_t\n')+body
        wrapped = ('#endif /* MACH_BSD */\n\n'
                   '#if defined(MACH_BSD) || CONFIG_ATLANTIC_FIRMWARE\n'+declaration+
                   '#endif /* MACH_BSD || CONFIG_ATLANTIC_FIRMWARE */\n\n#ifdef MACH_BSD\n')
        assert reconstructed.count(wrapped) == 1
        reconstructed = reconstructed.replace(wrapped, declaration, 1)
    assert reconstructed == original
    passed('only-native-upl-scope-changes-with-bodies-intact', accessors=list(UPL))

    for bsd_defined in (False, True):
        flags = ['-UCONFIG_ATLANTIC_FIRMWARE', '-UMACH_BSD']
        if bsd_defined:
            flags.append('-DMACH_BSD=1')
        preprocess = [*target, *flags, '-E','-P','-x','c','-']
        suffix_name = '-bsd' if bsd_defined else '-no-bsd'
        before = run('scope-upstream'+suffix_name, preprocess, old_hunk)
        after = run('scope-nonfirmware'+suffix_name, preprocess, new_hunk)
        assert before.split() == after.split()
        passed('stock-upstream-accessor-tokens-retained', bsd_defined=bsd_defined)
    scope = run('scope-firmware', [*target,'-E','-P','-x','c','-'], new_hunk)
    assert set(re.findall(r'^([a-z_]\w*)\(',scope,re.M)) == set(UPL)
    passed('firmware-selects-exactly-two-native-upl-accessors')

    native_exec = source_text(source/EXEC)
    native_rsr = source_text(source/RSR)
    assert source_hashes[RSR] == locked[RSR]['postimage_sha256']
    notice = native_exec[:native_exec.index('#include ')]
    assert native_rsr.startswith(notice)
    fragments = {
        'state':'static uint32_t rsr_current_version = 0;\n',
        'get':'uint32_t\n'+function(native_exec,'rsr_get_version'),
        'bump':'void\n'+function(native_exec,'rsr_bump_version'),
    }
    assert all(native_exec.count(data) == native_rsr.count(data) == 1 for data in fragments.values())
    passed('native-rsr-state-and-atomic-bodies-with-original-notices',
           fragments_sha256={name:digest(data.encode()) for name,data in fragments.items()},
           notice_sha256=digest(notice.encode()))
    expected_text = 'Atlantic RSR version state requires firmware without BSD'
    for name, flags in [('nonfirmware',['-UCONFIG_ATLANTIC_FIRMWARE','-UCONFIG_ATLANTIC_C_LAYOUT',
                                       '-UCONFIG_ATLANTIC_CPP_LAYOUT']),
                        ('bsd-enabled',['-DMACH_BSD=1']), ('bsd-defined-zero',['-DMACH_BSD=0'])]:
        run('reject-rsr-'+name, [*target, *flags, '-E',str(source/RSR)], expected=1, diagnostic=expected_text)
        passed('firmware-only-rsr-unit-rejects-'+name)

    for path, required in EXPORTS.items():
        file = objects[path]
        image = read_object(file)
        listing = run(Path(path).stem+'-external-exports',['llvm-nm','--format=darwin','--defined-only','--extern-only',str(file)])
        for symbol in sorted(required):
            symbol_info = image[2][symbol]
            line = next(row for row in listing.splitlines() if row.endswith(' '+symbol))
            assert symbol_info['type'] & 1 and ' external ' in line and 'weak' not in line
        passed(Path(path).stem+'-required-strong-native-exports', symbols=sorted(required))
    for path, required in IMPORTS.items():
        assert all(strong_external_import(objects[path], name) for name in sorted(required))
        passed(Path(path).stem+'-real-strong-provider-imports', symbols=sorted(required))
    assert set(records[RSR]['defined_external_symbols']) == set(RSR_APIS)
    assert not run('rsr-undefined',['llvm-nm','--undefined-only',str(objects[RSR])]).strip()
    code = audit_rsr_code(read_object(objects[RSR]))
    passed('native-rsr-shared-state-and-relaxed-32-bit-exclusive-retry', instruction_words=code)

    report = {'schema_version':1, 'label':'HOST_TESTED',
              'scope':'NATIVE_SHARED_REGION_UPL_RSR_SOURCE_AND_ARM64_OBJECTS',
              'port_id':base['port_id'], 'upstream_commit':base['upstream_commit'],
              'build_report_sha256':sha(build), 'tool_sha256':sha(Path(__file__)),
              'patch_sha256':patch_hash, 'source_sha256':source_hashes, 'object_sha256':object_hashes,
              'outcomes':outcomes, 'commands':commands, 'xnu_executed':False,
              'shared_region_mapped':False, 'pager_executed':False, 'phone_accessed':False}
    (output/'report.json').write_text(json.dumps(report,indent=2)+'\n', encoding='utf-8',newline='\n')
    print(json.dumps({'passed':len(outcomes),'checked_units':len(objects),'xnu_executed':False}))


if __name__ == '__main__':
    main()
