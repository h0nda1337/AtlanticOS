"""Verify address derivation from the pinned native boot_args fixture."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess

from test_xnu_brk_scope import read_object, symbol_bytes

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'atlantic-core/boot_locator.c'
TEST = ROOT / 'tests/c/test_boot_locator.c'
FIXTURES = ROOT / 'evidence/boot-prepare-20261025/std'
RECORD_SHA256 = '8971c60b7399ceaf65a735ffa4b59af9c0e921b3fd632690da538b5f2e2dcbcd'


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--sanitize', action='store_true')
    args = parser.parse_args()
    output = args.output.resolve()
    work = (ROOT / 'work').resolve()
    if output == work or not output.is_relative_to(work) or output.exists():
        parser.error('Use a new output directory inside work/')
    cc = shutil.which('clang')
    nm = shutil.which('llvm-nm')
    if not cc or not nm:
        parser.error('Clang and llvm-nm are required')

    fixture = FIXTURES / 'candidate-boot-args.bin'
    native = FIXTURES / 'native-boot-abi.o'
    proof = FIXTURES / 'report.json'
    evidence = json.loads(proof.read_text(encoding='utf-8'))
    actual_bytes = symbol_bytes(read_object(native), '_atl_native_boot_reference', 1152)
    if (not evidence['native_abi']['all_1152_bytes_match_native_initializer'] or
        sha256(native) != evidence['native_abi']['object_sha256'] or
        sha256(fixture) != RECORD_SHA256 or evidence['record_sha256'] != RECORD_SHA256 or
        fixture.read_bytes() != actual_bytes):
        raise RuntimeError('Pinned native initializer and test record differ')

    output.mkdir(parents=True)
    commands = []

    def run(name, command):
        result = subprocess.run(command, cwd=ROOT, text=True, capture_output=True, timeout=60)
        commands.append([str(token).replace(str(ROOT), '<ROOT>') for token in command])
        log = (result.stdout + result.stderr).replace(str(ROOT), '<ROOT>')
        (output / (name + '.log')).write_text(log, encoding='utf-8', newline='\n')
        if result.returncode:
            raise RuntimeError(name + ' failed: ' + log[-2500:])
        return result.stdout

    flags = ['-std=c11', '-Wall', '-Wextra', '-Werror', '-I' + str(ROOT / 'include')]
    host_flags = [*flags, '-O1', '-g']
    if args.sanitize:
        host_flags += ['-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                       '-fno-omit-frame-pointer']
    executable = output / 'host-tests'
    run('host-build', [cc, *host_flags, SOURCE, TEST, '-o', executable])
    result = run('host-tests', [executable, fixture])
    stats = json.loads(next(line[5:] for line in result.splitlines() if line.startswith('PASS ')))
    if stats != {'accepted': 4, 'rejected': 34}:
        raise RuntimeError('Unexpected host case counts: ' + str(stats))

    obj = output / 'boot-locator-arm64.o'
    run('arm64-build', [cc, '--target=aarch64-none-elf', '-mstrict-align', *flags, '-O2',
                        '-ffreestanding', '-fno-builtin', '-fno-stack-protector',
                        '-c', SOURCE, '-o', obj])
    if run('arm64-imports', [nm, '--undefined-only', obj]).strip():
        raise RuntimeError('Freestanding locator has imports')
    exports = run('arm64-exports', [nm, '--defined-only', '--extern-only', obj]).split()
    if len(exports) != 3 or exports[1:] != ['T', 'atl_xnu_boot_locate']:
        raise RuntimeError('Unexpected freestanding locator exports: ' + ' '.join(exports))

    paths = [
        SOURCE, TEST, Path(__file__), ROOT / 'tools/test_xnu_brk_scope.py',
        *(ROOT / 'include/atlantic' / name for name in
          ('boot_locator.h', 'boot_prepare.h', 'boot_tree.h', 'macho.h', 'fdt.h')),
        fixture, native, proof,
    ]
    report = {
        'schema_version': 1,
        'label': 'HOST_TESTED',
        'scope': 'NATIVE_BOOT_RECORD_NUMERIC_ADDRESS_DERIVATION',
        'inputs': {str(path.relative_to(ROOT)): sha256(path) for path in paths},
        'commands': commands,
        'sanitizers': args.sanitize,
        'host_contracts': stats,
        'arm64_object_sha256': sha256(obj),
        'arm64_imports': [],
        'derived_addresses_dereferenced': False,
        'live_translations_verified': False,
        'xnu_executed': False,
        'phone_accessed': False,
    }
    (output / 'report.json').write_text(json.dumps(report, indent=2) + '\n',
                                       encoding='utf-8', newline='\n')
    print(json.dumps(stats))


if __name__ == '__main__':
    main()
