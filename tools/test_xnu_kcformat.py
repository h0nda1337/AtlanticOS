"""Audit native XNU kernel-collection metadata state and the target ABI."""

import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess

from test_xnu_brk_scope import function, read_object, symbol_bytes
from test_xnu_platform import strong_external_import
from test_xnu_ram_console import external_relocations, normalized_tokens, sha, words

ROOT = Path(__file__).resolve().parents[1]
SOURCE = 'pexpert/gen/kcformat.c'
HEADER = 'pexpert/pexpert/pexpert.h'
PATCH = '0036-scope-native-kc-vnode-state-to-available-bsd.patch'
GUARD = '#if !CONFIG_ATLANTIC_FIRMWARE || defined(MACH_BSD)\n'
STATE = {'_collection_base_pointers', '_collection_mach_headers', '_collection_slide'}
APIS = {'_PE_get_kc_base_pointers', '_PE_get_kc_baseaddress', '_PE_get_kc_format',
        '_PE_get_kc_header', '_PE_get_kc_slide', '_PE_get_primary_kc_format',
        '_PE_reset_kc_header', '_PE_set_kc_header', '_PE_set_kc_header_and_base'}
IMPORTS = {'_mach_assert_jump_key', '_panic', '_segLOWESTTEXT', '_static_kernelcache', '_vm_kernel_slide'}

HEADER_ABI = """
#include <pexpert/pexpert.h>
_Static_assert(KCNumKinds == 4, "KC base array ABI");
_Static_assert(KCKindPrimary == 1 && KCKindPageable == 2 && KCKindAuxiliary == 3, "kind ABI");
_Static_assert(KCFormatStatic == 1 && KCFormatFileset == 3 && KCFormatKCGEN == 4, "format ABI");
_Static_assert(MH_FILESET == 12, "Mach-O file type ABI");
_Static_assert(__builtin_offsetof(kernel_mach_header_t, filetype) == 12, "Mach-O file type offset");
#define ABI_CHECK(name, type) _Static_assert(__builtin_types_compatible_p(__typeof__(&name), type), #name)
ABI_CHECK(PE_set_kc_header, void (*)(kc_kind_t, kernel_mach_header_t *, uintptr_t));
ABI_CHECK(PE_set_kc_header_and_base, void (*)(kc_kind_t, kernel_mach_header_t *, void *, uintptr_t));
ABI_CHECK(PE_get_kc_header, void *(*)(kc_kind_t));
ABI_CHECK(PE_get_kc_base_pointers, const void *const *(*)(void));
ABI_CHECK(PE_get_primary_kc_format, bool (*)(kc_format_t *));
"""


def source_scope(original, adapted):
    fragments = [
        'int vnode_put(void *vp);\n',
        'void * collection_vp[KCNumKinds] = {};\n',
        'SECURITY_READ_ONLY_LATE(void *) collection_vp[KCNumKinds];\n',
    ]
    fragments.append(original[original.index('void\nPE_set_kc_vp('):
                              original.index('const void * const *\nPE_get_kc_base_pointers(')-1])
    restored = adapted
    for fragment in fragments:
        wrapped = GUARD+fragment+'#endif\n'
        assert restored.count(wrapped) == 1
        restored = restored.replace(wrapped, fragment, 1)
    assert restored == original
    return [name for name in ('PE_set_kc_vp', 'PE_get_kc_vp', 'PE_reset_all_kc_vp')
            if function(original, name) == function(adapted, name)]


def audit_state(image):
    _, sections, symbols = image
    for name in STATE:
        symbol = symbols[name]
        section = sections[symbol['section']-1]
        assert symbol['type'] == 0x0f and symbol['value'] % 8 == 0
        assert (section['segment'], section['name']) == ('__DATA', '__const')
        assert symbol_bytes(image, name) == bytes(32)

    def referenced(name):
        return {target for _, _, target in external_relocations(image, name, {3, 4, 5, 6})}

    for name in ('_PE_set_kc_header', '_PE_set_kc_header_and_base', '_PE_reset_kc_header'):
        assert STATE <= referenced(name), name
    assert referenced('_PE_get_kc_base_pointers') == {'_collection_base_pointers'}
    assert {'_vm_kernel_slide', '_collection_slide'} <= referenced('_PE_get_kc_slide')
    header_refs = referenced('_PE_get_kc_header') - {'_collection_mach_headers'}
    index_table, = [name for name in header_refs if name.startswith('l_switch.table.')]
    assert symbol_bytes(image, index_table, 24) == bytes.fromhex('000000000000000001000000000000000300000000000000')
    return {'array_slots': 4, 'base_header_slide_array_bytes': 32, 'kind_to_index': [0, 1, 3]}


def audit_format_accessor(image):
    name = '_PE_get_primary_kc_format'
    refs = external_relocations(image, name, {3, 4, 5, 6})
    assert refs == [(4, 3, '_collection_mach_headers'), (8, 4, '_collection_mach_headers'),
                    (36, 5, '_static_kernelcache'), (40, 6, '_static_kernelcache')]
    code = words(image, name)
    # This is the current pinned ARM64 compiler's control flow. Verify the
    # branches around both memory accesses and the final output store.
    assert code[0] == 0xb4000200  # CBZ x0 to the true-return suffix, skipping all accesses.
    assert code[1:5] == (0x90000008, 0xf9400108, 0xb40000c8, 0xb9400d08)
    assert code[5:9] == (0x7100311f, 0x54000061, 0x52800068, 0x14000007)
    assert code[9:15] == (0x90000008, 0xf9400108, 0x39400108, 0x7100011f, 0x52800088, 0x1a9f0508)
    assert code[15:18] == (0xb9000008, 0x52800020, 0xd65f03c0)
    return {'null_output_pointer_returns_true_without_access': True,
            'null_registered_header_uses_native_static_flag': True,
            'nonfileset_header_uses_native_static_flag': True,
            'output_for_static': 1, 'output_for_fileset': 3, 'output_for_kcgen': 4,
            'true_return_proves_format_query_only': True,
            'registered_pointer_must_refer_to_readable_header': True}


def audit_link(path, build, base):
    link = json.loads(path.read_text())
    log = path.with_name('link.log')
    assert link['port_id'] == base['port_id'] and link['build_report_sha256'] == sha(build)
    assert link['link_log_sha256'] == sha(log)
    assert link['undefined_symbols_must_error'] and not link['dead_stripping_requested']
    assert link['link_exit_code'] and not link['duplicate_symbol_error_count'] and not link['output_exists']
    missing = set(re.findall(r'error: undefined symbol: (\S+)', log.read_text()))
    assert len(missing) == link['undefined_symbol_error_count'] and APIS.isdisjoint(missing)
    assert {'_PEHaltRestart', '_PEHaltRestartInternal', '_os_log_with_args'} <= missing
    return {'unresolved_count': len(missing), 'strict_link_report_sha256': sha(path),
            'strict_link_log_sha256': sha(log), 'output_exists': False}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', type=Path, required=True)
    parser.add_argument('--link', type=Path, help='Also audit a strict link rejection for this exact build')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    build = args.build.resolve()
    base = json.loads(build.read_text())
    lock = json.loads((ROOT/'xnu-port/patches.lock.json').read_text())
    assert base['port_id'] == lock['port_id'] and base['upstream_commit'] == lock['upstream_commit']
    assert base['patches'] == lock['patches'] and base['layout_provider'] and not base['failed_units']
    assert not base['kernel_linked'] and not base['xnu_executed'] and not base['phone_accessed']
    for rel, expected in base['inputs'].items():
        assert sha(ROOT/rel) == expected, rel

    output = args.output.resolve()
    if output.exists() or not output.is_relative_to((ROOT/'work').resolve()):
        parser.error('Use a new output directory under work/')
    output.mkdir(parents=True)
    outcomes, commands = [], []

    def passed(name, **details):
        outcomes.append({'name': name, 'passed': True, **details})

    def run(name, command, stdin=None, retain_stdout=True):
        proc = subprocess.run(command, cwd=ROOT, input=stdin, text=True, capture_output=True, timeout=90)
        commands.append([str(item).replace(str(ROOT), '<ROOT>') for item in command])
        log = (proc.stdout if retain_stdout else '')+proc.stderr
        (output/(name+'.log')).write_text(log.replace(str(ROOT), '<ROOT>'), encoding='utf-8', newline='\n')
        assert proc.returncode == 0, name+': '+proc.stderr[-2400:]
        return proc.stdout

    source = ROOT/'work/xnu-lab-sources'/base['port_id'][:16]
    upstream = ROOT/'xnu-upstream'
    patch = ROOT/'xnu-port'/PATCH
    assert sha(patch) == lock['patches'][PATCH]
    locked = next(entry for entry in lock['canonical_lf_files'] if entry['path'] == SOURCE)
    original = (upstream/SOURCE).read_text(encoding='utf-8')
    adapted = (source/SOURCE).read_text(encoding='utf-8')
    upstream_hash = hashlib.sha256(original.encode()).hexdigest()
    assert upstream_hash == locked['preimage_sha256'] and sha(source/SOURCE) == locked['postimage_sha256']
    header_hash = hashlib.sha256((upstream/HEADER).read_text(encoding='utf-8').encode()).hexdigest()
    assert sha(source/HEADER) == header_hash
    vnode = source_scope(original, adapted)
    assert len(vnode) == 3
    passed('only-native-vnode-ownership-is-profile-guarded', unchanged_vnode_bodies=vnode,
           upstream_sha256=upstream_hash, source_sha256=sha(source/SOURCE), header_sha256=header_hash,
           patch_sha256=sha(patch))

    arm_init = 'osfmk/arm/arm_init.c'
    kalloc = 'osfmk/kern/kalloc.c'
    rebase = function((source/arm_init).read_text(encoding='utf-8'), 'arm_slide_rebase_and_sign_image')
    alloc = function((source/kalloc).read_text(encoding='utf-8'), 'kalloc_type_view_parse')
    assert rebase == function((upstream/arm_init).read_text(encoding='utf-8'), 'arm_slide_rebase_and_sign_image')
    assert alloc == function((upstream/kalloc).read_text(encoding='utf-8'), 'kalloc_type_view_parse')
    assert '&__thread_starts_sect_end[0] != &__thread_starts_sect_start[0]' in rebase
    assert re.search(r'kPrelinkTextSegment, kPrelinkTextSection\);\s*assert\(prelink_sect\);\s*cur = prelink_sect->addr;', alloc)
    passed('native-cache-detection-and-kcgen-prelinked-section-requirement-retained',
           kcgen_prelink_section_required=True, final_image_layout_checked=False)

    record = next(item for item in base['objects'] if item['source'] == SOURCE)
    assert record['source_sha256'] == sha(source/SOURCE)
    obj = (build.parent/record['object']).resolve()
    assert obj.is_relative_to(build.parent) and sha(obj) == record['object_sha256']
    recipe = next(command for command in base['commands'] if len(command) > 4 and command[-4] == '-c'
                  and command[-3].endswith('/'+SOURCE))
    flags = [arg.replace('<ROOT>', str(ROOT)) for arg in recipe[:-4]]
    assert '-DPEXPERT_KERNEL_PRIVATE=1' in flags and '-UMACH_KERNEL_PRIVATE' in flags
    assert sha(Path(flags[0])) == base['layout_provider']['compiler_sha256']
    run('native-header-abi', [*flags, '-fsyntax-only', '-x', 'c', '-'], stdin=HEADER_ABI)
    passed('pinned-native-pe-header-and-arm64-call-abi')

    stock_path = output/'kcformat.c'
    stock_path.write_text(original, encoding='utf-8', newline='\n')
    off = ['-UCONFIG_ATLANTIC_FIRMWARE', '-UCONFIG_ATLANTIC_C_LAYOUT',
           '-UCONFIG_ATLANTIC_CPP_LAYOUT', '-UCONFIG_ATLANTIC_RAM_CONSOLE']
    before = run('stock-outside-firmware', [*flags, *off, '-E', '-P', str(stock_path)], retain_stdout=False)
    after = run('adapted-outside-firmware', [*flags, *off, '-E', '-P', str(source/SOURCE)], retain_stdout=False)
    stock_tokens = normalized_tokens(before, 'kcformat.c')
    assert stock_tokens == normalized_tokens(after, 'kcformat.c')
    firmware = run('adapted-bsd-free-firmware', [*flags, '-E', '-P', str(source/SOURCE)], retain_stdout=False)
    assert not re.search(r'(?:vnode_put|collection_vp)', firmware)
    passed('nonfirmware-tokens-preserved-and-firmware-vnode-state-elided',
           normalized_nonfirmware_tokens_sha256=hashlib.sha256(' '.join(stock_tokens).encode()).hexdigest())

    image = read_object(obj)
    exports = {line.split()[0] for line in run('defined',
               ['llvm-nm', '--extern-only', '--defined-only', '--format=posix', str(obj)]).splitlines()}
    imports = {line.strip() for line in run('undefined', ['llvm-nm', '--undefined-only', str(obj)]).splitlines()}
    assert exports == APIS | STATE | {'_kc_highest_nonlinkedit_vmaddr'}
    assert imports == IMPORTS and all(strong_external_import(obj, name) for name in imports)
    assert all(image[2][name]['type'] == 0x0f for name in exports)
    passed('real-mach-o-native-metadata-definitions-and-strong-imports',
           exports=sorted(exports), imports=sorted(imports), object_sha256=sha(obj))
    passed('native-kc-header-base-slide-storage-and-slot-mapping', **audit_state(image))
    passed('native-format-accessor-assigns-each-valid-output-path', **audit_format_accessor(image))
    if args.link:
        passed('strict-link-resolves-native-kc-apis-and-keeps-other-gaps-visible', **audit_link(args.link, build, base))

    report = {'schema_version': 1, 'label': 'HOST_TESTED',
              'scope': 'NATIVE_KC_METADATA_ARM64_OBJECT_AND_ABI_ONLY',
              'port_id': base['port_id'], 'upstream_commit': base['upstream_commit'],
              'build_report_sha256': sha(build), 'tool_sha256': sha(Path(__file__)),
              'outcomes': outcomes, 'commands': commands, 'xnu_executed': False,
              'fileset_booted': False, 'phone_accessed': False}
    (output/'report.json').write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8', newline='\n')
    print(json.dumps({'passed': len(outcomes), 'xnu_executed': False, 'fileset_booted': False}))


if __name__ == '__main__':
    main()
