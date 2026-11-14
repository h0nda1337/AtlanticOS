"""Audit generated native version metadata in the actual ARM64 object."""
import argparse
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess

from build_xnu_lab import VERSION_NATIVE_INPUTS, VERSION_ORIGIN, VERSION_SOURCE, VERSION_STAMP
from test_xnu_brk_scope import digest, read_object, symbol_bytes


ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    build = json.loads(args.build.read_text())
    lock = json.loads((ROOT/'xnu-port/patches.lock.json').read_text())
    dependencies = json.loads((ROOT/'dependencies.lock.json').read_text())
    assert build['port_id'] == lock['port_id'] and not build['failed_units']
    assert build['upstream_commit'] == lock['upstream_commit'] == dependencies['xnu']['commit']
    assert build['layout_provider'] and build['profile'] == 'atlanticlab'
    for path, expected in build['inputs'].items():
        assert digest(ROOT/path) == expected, path

    source = ROOT/'work/xnu-lab-sources'/build['port_id'][:16]
    build_dir = args.build.parent.resolve()
    generated = build['version_source']
    assert generated['path'] == VERSION_SOURCE and generated['stamp'] == VERSION_STAMP
    assert set(generated['upstream_inputs']) == set(VERSION_NATIVE_INPUTS)
    for path, expected in generated['upstream_inputs'].items():
        assert digest(source/path) == expected, path
        pristine = (ROOT/'xnu-upstream'/path).read_bytes().replace(b'\r\n', b'\n')
        assert (source/path).read_bytes() == pristine, path
    version_source = (build_dir/VERSION_SOURCE).resolve()
    version_header = (build_dir/'include/libkern/version.h').resolve()
    version_object = (build_dir/'version.o').resolve()
    version_input = (build_dir/'xnuVersion').resolve()
    assert all(p.is_relative_to(build_dir) for p in
               (version_source, version_header, version_object, version_input))
    record, = (o for o in build['objects'] if o['source'] == VERSION_SOURCE)
    assert record['source_origin'] == VERSION_ORIGIN and record['object'] == 'version.o'
    assert digest(version_object) == record['object_sha256'] and not record['undefined_symbols']
    assert digest(version_source) == record['source_sha256'] == generated['sha256']
    assert digest(version_header) == build['version_header_sha256']
    lab_version = VERSION_STAMP['xnuVersion']
    assert version_input.read_bytes() == (lab_version+'\n').encode()

    output = args.output.resolve()
    if not output.is_relative_to(ROOT/'work') or output.exists():
        parser.error('Use a new output directory under work/')
    output.mkdir(parents=True)

    def run(name, command, env=None):
        result = subprocess.run(command, cwd=output, env=env, text=True, capture_output=True, timeout=30)
        (output/(name+'.log')).write_text((result.stdout+result.stderr).replace(str(ROOT), '<ROOT>'),
                                         encoding='utf-8', newline='\n')
        result.check_returncode()
        return result.stdout

    # Re-stamp copies in an independent directory with the recorded lab inputs.
    c_copy, h_copy = output/'version.c', output/'version.h'
    shutil.copyfile(version_input, output/'xnuVersion')
    shutil.copyfile(source/'libkern/libkern/version.h.template', h_copy)
    template = (source/'config/version.c.template').read_text(encoding='utf-8')
    assert template.count('###KERNEL_BUILDER###') == 3
    c_copy.write_text(template.replace('###KERNEL_BUILDER###', VERSION_STAMP['builder']),
                      encoding='utf-8', newline='\n')
    stamp_env = dict(PATH=os.environ.get('PATH', os.defpath), LC_ALL='C',
                     SRCROOT=str(source), OBJROOT=str(output), TARGET=str(output),
                     **VERSION_STAMP['environment'])
    run('version-regenerate', ['perl', str(source/'config/newvers.pl'), str(c_copy), str(h_copy)], stamp_env)
    assert digest(c_copy) == digest(version_source)
    assert digest(h_copy) == digest(version_header)

    parsed = read_object(version_object)
    _, sections, symbols = parsed
    def section_of(name):
        return sections[symbols[name]['section']-1]

    major, minor, revision = map(int, lab_version.split('.'))
    integers = {'_version_major':major, '_version_minor':minor, '_version_revision':revision,
                '_version_stage':0x80, '_version_prerelease_level':0}
    stamp = VERSION_STAMP['environment']
    version_text = (f'Darwin Kernel Version {lab_version}: {stamp["KERNEL_BUILD_DATE"]}; '
                    f'{VERSION_STAMP["builder"]}:{stamp["KERNEL_BUILD_OBJROOT"]}')
    strings = {'_version':version_text, '___kernelVersionString':'@(#)VERSION: '+version_text,
               '_osbuild_config':stamp['CURRENT_KERNEL_CONFIG_LC'], '_osbuilder':VERSION_STAMP['builder'],
               '_osrelease':lab_version, '_ostype':'Darwin', '_version_variant':str(revision)}
    for name, expected in integers.items():
        assert struct.unpack('<i', symbol_bytes(parsed, name, 4))[0] == expected, name
        assert symbols[name]['value'] % 4 == 0, name
    for name, expected in strings.items():
        encoded = expected.encode()+b'\0'
        assert symbol_bytes(parsed, name, len(encoded)) == encoded, name
    for name in integers.keys() | strings.keys():
        section = section_of(name)
        assert (section['segment'], section['name']) == ('__TEXT', '__const'), name

    osversion = symbols['_osversion']
    bss = section_of('_osversion')
    assert (bss['segment'], bss['flags'] & 0xff, bss['offset']) == ('__DATA', 1, 0)
    assert osversion['value'] == bss['address'] and bss['size'] == 256
    listing = run('version-exports', ['llvm-nm','--format=darwin','--defined-only','--extern-only',str(version_object)])
    for name in integers.keys() | strings.keys() | {'_osversion'}:
        binding, = (line for line in listing.splitlines() if line.endswith(' '+name))
        assert ' external ' in binding and 'weak' not in binding, binding
    assert not run('version-imports', ['llvm-nm','--undefined-only',str(version_object)]).strip()

    report = {'schema_version':1, 'label':'HOST_TESTED',
              'scope':'NATIVE_VERSION_GENERATION_AND_ARM64_OBJECT_ABI_ONLY',
              'port_id':build['port_id'], 'upstream_commit':build['upstream_commit'],
              'build_report_sha256':digest(args.build), 'tool_sha256':digest(Path(__file__)),
              'object_sha256':digest(version_object), 'version_source':generated,
              'version_header_sha256':digest(version_header), 'integers':integers, 'strings':strings,
              'osversion_bytes':256, 'undefined_symbols':[], 'kernel_linked':False,
              'xnu_executed':False, 'phone_accessed':False,
              'outcomes':[{'name':name, 'passed':True} for name in (
                  'pinned-native-generator-and-templates', 'reproducible-version-source-and-header',
                  'native-constant-integer-and-string-values', 'writable-osversion-zero-fill',
                  'strong-version-definitions-without-imports')]}
    (output/'report.json').write_text(json.dumps(report,indent=2)+'\n', encoding='utf-8', newline='\n')
    print(json.dumps({'checks_passed':len(report['outcomes']), 'port_id':build['port_id'],
                      'osversion_bytes':256, 'xnu_executed':False}))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
