"""Audit intact native Blocks, real kalloc metadata and C/C++ ABI; no execution."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess
from test_xnu_brk_scope import read_object, symbol_bytes, function
from test_xnu_platform import strong_external_import
from test_xnu_queue_vm import CPPFLAGS

ROOT = Path(__file__).resolve().parents[1]
NATIVE = ['libkern/libclosure/runtime.cpp', 'libkern/libclosure/libclosuredata.c']
CLASSES = ['StackBlock', 'MallocBlock', 'AutoBlock', 'FinalizingBlock', 'GlobalBlock', 'WeakBlockVariable']
API = ['__Block_copy', '__Block_release', '__Block_object_assign', '__Block_object_dispose',
       '__Block_tryRetain', '__Block_isDeallocating', '_Block_size', '__Block_has_signature',
       '__Block_signature', '__Block_layout', '__Block_extended_layout', '__Block_use_stret']


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def metadata_header(raw, header_bytes, flags_offset):
    assert struct.unpack_from('<HHI', raw) == (1, header_bytes, 8)
    assert struct.unpack_from('<I', raw, flags_offset)[0] == 0x61


def metadata_signature(obj, descriptor, field_offset, expected):
    raw, sections, symbols = obj
    symbol = symbols[descriptor]
    section = sections[symbol['section']-1]
    address = symbol['value'] - section['address'] + field_offset
    first = section['relocation_offset']
    count = section['relocation_count']
    assert first + 8*count <= len(raw)
    matches = [word for offset, word in struct.iter_unpack('<II', raw[first:first+8*count])
               if offset == address]
    assert len(matches) == 1 and matches[0] >> 24 == 0x0e
    index = matches[0] & 0xffffff
    name = next(name for name, entry in symbols.items() if entry['index'] == index)
    assert sections[symbols[name]['section']-1]['name'] == '__cstring'
    assert symbol_bytes(obj, descriptor, field_offset+8)[field_offset:] == bytes(8)
    assert symbol_bytes(obj, name, len(expected)+1) == expected.encode('ascii')+b'\0'
    return name


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--build', type=Path, required=True)
    ap.add_argument('--output', type=Path, required=True)
    args = ap.parse_args()
    base = json.loads(args.build.read_text())
    lock = json.loads((ROOT/'xnu-port/patches.lock.json').read_text())
    provider = base.get('layout_provider') or {}
    if base['port_id'] != lock['port_id'] or base['failed_units'] or not provider.get('cpp_layout'):
        ap.error('Use a successful current build with the separate C++ proof selected')
    for rel, expected in base['inputs'].items():
        assert sha(ROOT/rel) == expected, rel
    for rel, expected in provider['cpp_layout']['proof_inputs'].items():
        assert sha(ROOT/rel) == expected, rel
    source = ROOT/'work/xnu-lab-sources'/base['port_id'][:16]
    out = args.output.resolve()
    if not out.is_relative_to((ROOT/'work').resolve()) or out.exists():
        ap.error('Use a new output directory under work/')
    out.mkdir(parents=True)
    commands, outcomes = [], []

    def check(name, **details):
        outcomes.append({'name':name, 'passed':True, **details})

    def run(name, command, expected=0, diagnostic=None):
        p = subprocess.run(command, cwd=ROOT, text=True, capture_output=True, timeout=90)
        commands.append([str(s).replace(str(ROOT), '<ROOT>') for s in command])
        (out/(name+'.log')).write_text((p.stdout+p.stderr).replace(str(ROOT), '<ROOT>'), encoding='utf-8', newline='\n')
        assert p.returncode == expected and (not diagnostic or diagnostic in p.stderr), name+': '+p.stderr[-2400:]
        return p.stdout

    recipe = next(c for c in base['commands'] if any(s.endswith('/tests/xnu_port/boot_abi.c') for s in c))
    assert recipe[-4] == '-c'
    target = [s.replace('<ROOT>', str(ROOT)) for s in recipe[:-4]]
    assert sha(target[0]) == provider['compiler_sha256']
    assert '-DCONFIG_ATLANTIC_CPP_LAYOUT=1' in target
    c = target + ['-UMACH_KERNEL_PRIVATE', '-DLIBKERN_KERNEL_PRIVATE=1', '-DOSALLOCDEBUG=1']
    cpp = [s for s in target if s != '-std=gnu11'] + CPPFLAGS
    native_hashes = {}
    for rel in NATIVE:
        expected = hashlib.sha256((ROOT/'xnu-upstream'/rel).read_text().encode()).hexdigest()
        assert sha(source/rel) == expected
        native_hashes[rel] = expected
    check('both-entire-native-provider-sources-unchanged', source_sha256=native_hashes)
    check('cxx-proof-explicit-and-bound-to-actual-compiler', compiler_sha256=sha(target[0]),
          cpp_proof_sha256=provider['cpp_layout']['proof_sha256'])

    fixture = ROOT/'tests/xnu_port/blocks_layout.c'
    values = {}
    for language, flags in [('c', c), ('cpp', cpp)]:
        obj, ir = out/(language+'.o'), out/(language+'.ll')
        run(language+'-abi', [*flags, '-c', str(fixture), '-o', str(obj)])
        run(language+'-ir', [*flags, '-S', '-emit-llvm', str(fixture), '-o', str(ir)])
        text = ir.read_text()
        assert '__atlantic_layout_' not in text and '__atlantic_cpp_layout_' not in text
        wire = {}
        for line in text.splitlines():
            if not line.startswith('@atl_blocks_') or 'constant i64' not in line:
                continue
            match = re.fullmatch(r'@atl_blocks_(\w+) = .*constant i64 (\d+),.*', line)
            assert match and match[1] not in wire, line
            wire[match[1]] = int(match[2])
        assert len(wire) == 17 and wire['layout_bytes'] == 32 and wire['byref_bytes'] == 24
        assert wire['layout_summary'] == wire['byref_summary'] == 6
        assert 'c"1211\\00"' in text and 'c"112\\00"' in text
        values[language] = wire
        check(language+'-native-header-abi-and-folded-metadata', constants=wire, ir_sha256=sha(ir))
        for macro, diagnostic in [('ATL_TEST_WRONG_BLOCK_ABI','Block_layout target ABI mismatch'),
                                  ('ATL_TEST_WRONG_BLOCK_METADATA','Block pointer metadata mismatch')]:
            run(language+'-'+macro, [*flags, '-D'+macro, '-fsyntax-only', str(fixture)], 1, diagnostic)
            check(language+'-'+macro+'-rejected')
        # A missing independent selection must fail even with a compiler that
        # supports both APIs: C's v2 contract still rejects C++ metadata.
        if language == 'cpp':
            run('cpp-provider-not-selected', [*flags, '-UCONFIG_ATLANTIC_CPP_LAYOUT',
                '-DCONFIG_ATLANTIC_CPP_LAYOUT=0', '-fsyntax-only', str(fixture)], 1,
                'requires C and little-endian')
            check('cpp-provider-requires-explicit-separate-selection')
    assert values['c'] == values['cpp']
    check('seventeen-real-header-abi-and-metadata-constants-match-c-and-cpp')

    records = {o['source']:o for o in base['objects']}
    objects = {}
    for rel in NATIVE:
        record = records[rel]
        obj = args.build.parent/record['object']
        assert sha(obj) == record['object_sha256']
        objects[rel] = {'object_sha256':sha(obj)}
    runtime = args.build.parent/records[NATIVE[0]]['object']
    parsed = read_object(runtime)
    assert all(n in parsed[2] and parsed[2][n]['type'] & 1 for n in API)
    imports = set(run('runtime-imports', ['llvm-nm', '--undefined-only', str(runtime)]).split())
    assert {'_kalloc_ext', '_kfree_ext'} <= imports
    assert all(strong_external_import(runtime, n) for n in ['_kalloc_ext', '_kfree_ext'])
    assert not {'_malloc', '_free', '___cxa_throw', '___cxa_allocate_exception'} & imports
    objects[NATIVE[0]]['imports'] = sorted(imports)
    check('native-runtime-api-with-strong-genuine-kernel-allocation-dependencies', api=API, imports=sorted(imports))

    flags_offset = values['c']['var_view_flags']
    width = values['c']['var_view_bytes']
    for name, size, signature in [('_KT_BLOCK_LAYOUT', 32, '1211'), ('_KT_BLOCK_BYREF', 24, '112')]:
        symbol = parsed[2][name]
        section = parsed[1][symbol['section']-1]
        assert section['name'] == '__kalloc_var' and section['segment'] == '__DATA_CONST'
        raw = symbol_bytes(parsed, name, width)
        metadata_header(raw, size, flags_offset)
        header_ref = metadata_signature(parsed, name, values['c']['var_view_sig_hdr'], signature)
        payload_ref = metadata_signature(parsed, name, values['c']['var_view_sig_type'], '1')
        damaged = bytearray(raw)
        damaged[2] ^= 8
        try:
            metadata_header(damaged, size, flags_offset)
        except AssertionError:
            pass
        else:
            raise AssertionError('Corrupted native metadata header was accepted')
        check(name+'-actual-native-descriptor-and-corrupted-size-rejection',
              descriptor_sha256=hashlib.sha256(raw).hexdigest(), header_bytes=size,
              payload_bytes=8, flags='0x61', header_signature=signature, payload_signature='1',
              header_relocation_symbol=header_ref, payload_relocation_symbol=payload_ref)

    data = read_object(args.build.parent/records[NATIVE[1]]['object'])
    symbols = ['__NSConcrete'+name for name in CLASSES]
    for name in symbols:
        s = data[2][name]
        sec = data[1][s['section']-1]
        assert s['type'] & 1 and sec['flags'] & 0xff == 1
        end = min([other['value'] for other in data[2].values()
                   if other['section'] == s['section'] and other['value'] > s['value']]
                  + [sec['address']+sec['size']])
        assert end-s['value'] == 256
    assert not run('data-imports', ['llvm-nm', '--undefined-only', str(args.build.parent/records[NATIVE[1]]['object'])]).strip()
    check('six-native-zero-filled-class-placeholders-not-objc-classes', symbols=symbols, total_bss_bytes=1536)
    text = (source/NATIVE[0]).read_text()
    empty = ['_Block_retain_object', '_Block_release_object', '_Block_destructInstance']
    assert all(re.search(r'\{\s*\}', function(text, name)) for name in empty)
    check('three-empty-framework-hooks-are-native-not-objective-c-runtime-support', names=empty)
    run('runtime-disassembly', ['llvm-objdump', '--macho', '--disassemble', str(runtime)])

    report = {'schema_version':1, 'label':'HOST_TESTED',
        'scope':'NATIVE_BLOCKS_SOURCES_ABI_STATIC_METADATA_AND_OBJECTS_ONLY_NO_RUNTIME_OR_PHONE_EXECUTION',
        'port_id':base['port_id'], 'build_report_sha256':sha(args.build),
        'inputs':{p:sha(ROOT/p) for p in ['tools/test_xnu_blocks.py', 'tests/xnu_port/blocks_layout.c',
                  'tools/test_xnu_brk_scope.py', 'tools/test_xnu_platform.py', 'tools/test_xnu_queue_vm.py']},
        'native_sources':native_hashes, 'objects':objects, 'outcomes':outcomes, 'commands':commands,
        'native_runtime_executed':False, 'allocator_executed':False,
        'xnu_executed':False, 'phone_accessed':False, 'runtime_ram_measured':False}
    (out/'report.json').write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8', newline='\n')
    print(json.dumps({'passed':len(outcomes), 'native_runtime_executed':False}))


if __name__ == '__main__':
    main()
