"""Verify static Mach-O byte placement in declared RAM using a host allocation."""
import argparse
import hashlib
import json
from pathlib import Path
import platform
import shutil
import subprocess


ROOT = Path(__file__).resolve().parents[1]
FIXTURES = ROOT / 'evidence/static-image-plan-20261025/lld-fixtures'
FIXTURE_HASHES = {
    'high.macho': '06b3bb2ca32f77cc38873726923b4dc9587c3afe0f82472d2ea9abf95c979f13',
    'low.macho': 'd861a1d1d0f50147aebeca76ebc9b482d7c7c578f9d3ae9ad52dfcceff002c59',
}
INPUTS = [
    'atlantic-core/boot_prepare.c', 'atlantic-core/image_materialize.c', 'atlantic-core/macho.c',
    'include/atlantic/boot_prepare.h', 'include/atlantic/boot_tree.h',
    'include/atlantic/image_materialize.h', 'include/atlantic/macho.h',
    'tests/c/test_image_materialize.c', 'tools/test_image_materialize.py',
]


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--sanitize', action='store_true')
    args = parser.parse_args()
    output = args.output.resolve()
    cc = shutil.which('clang')
    nm = shutil.which('llvm-nm')
    readelf = shutil.which('llvm-readelf')
    if platform.system() != 'Linux' or not all((cc, nm, readelf)):
        parser.error('Use Linux/WSL with Clang, llvm-nm, and llvm-readelf')
    work = (ROOT / 'work').resolve()
    if output == work or output.exists() or not output.is_relative_to(work):
        parser.error('Use a new output directory inside work/')

    fixture_report = FIXTURES / 'report.json'
    fixture_records = json.loads(fixture_report.read_text(encoding='utf-8'))['images']
    recorded_hashes = {record['file_sha256'] for record in fixture_records}
    for name, expected in FIXTURE_HASHES.items():
        if expected not in recorded_hashes or sha256(FIXTURES / name) != expected:
            raise RuntimeError('Pinned fixture changed: ' + name)

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

    include = '-I' + str(ROOT / 'include')
    warnings = ['-std=c11', '-Wall', '-Wextra', '-Werror', include]
    host_flags = [*warnings, '-O1', '-g']
    if args.sanitize:
        host_flags += ['-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                       '-fno-omit-frame-pointer']
    implementation = ROOT / 'atlantic-core/image_materialize.c'
    executable = output / 'host-tests'
    run('host-build', [cc, *host_flags, ROOT / 'atlantic-core/macho.c',
                       ROOT / 'atlantic-core/boot_prepare.c', implementation,
                       ROOT / 'tests/c/test_image_materialize.c', '-o', executable])
    result = run('host-tests', [executable, FIXTURES / 'high.macho', FIXTURES / 'low.macho'])
    stats = json.loads(next(line[5:] for line in result.splitlines() if line.startswith('PASS ')))
    if stats != {'successful': 8, 'transactional_failures': 45, 'hole_fixtures': 5}:
        raise RuntimeError('Unexpected host coverage: ' + str(stats))

    obj = output / 'image-materialize-arm64.o'
    run('arm64-build', [cc, '--target=aarch64-none-elf', '-mstrict-align', *warnings, '-O2',
                        '-ffreestanding', '-fno-builtin', '-fno-stack-protector',
                        '-c', implementation, '-o', obj])
    elf = run('arm64-header', [readelf, '--file-header', obj])
    if 'AArch64' not in elf or 'ELF64' not in elf:
        raise RuntimeError('Unexpected materializer object target')
    imports = run('arm64-imports', [nm, '--undefined-only', obj])
    import_names = ['atl_macho_plan', 'atl_xnu_boot_prepare']
    if sorted(line.split() for line in imports.splitlines()) != [['U', name] for name in import_names]:
        raise RuntimeError('Unexpected freestanding ARM64 imports: ' + imports.strip())
    exports = run('arm64-exports', [nm, '--defined-only', '--extern-only', obj]).split()
    if len(exports) != 3 or exports[1:] != ['T', 'atl_xnu_image_materialize']:
        raise RuntimeError('Unexpected freestanding ARM64 exports: ' + ' '.join(exports))

    report = {
        'schema_version': 1,
        'label': 'HOST_TESTED',
        'scope': 'STATIC_FIXTURE_BYTES_IN_DECLARED_HOST_RAM',
        'inputs': {rel: sha256(ROOT / rel) for rel in INPUTS},
        'fixture_report_sha256': sha256(fixture_report),
        'fixture_sha256': FIXTURE_HASHES,
        'commands': commands,
        'sanitizers': ['address', 'undefined'] if args.sanitize else [],
        'host_contracts': stats,
        'arm64_object_sha256': sha256(obj),
        'arm64_imports': import_names,
        'boot_args_written_to_arena': False,
        'physical_mapping_verified': False,
        'image_authenticated': False,
        'mmu_enabled': False,
        'xnu_executed': False,
        'phone_accessed': False,
    }
    (output / 'report.json').write_text(json.dumps(report, indent=2) + '\n',
                                        encoding='utf-8', newline='\n')
    print(json.dumps({'host_passed': True, **stats, 'arm64_imports': import_names}))


if __name__ == '__main__':
    main()
