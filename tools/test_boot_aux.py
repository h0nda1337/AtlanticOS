"""Check retained XNU auxiliary wire data without entering the fixture or kernel."""

import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]
FIXTURE = 'evidence/native-split-and-context-locator-20261113/native-split.macho'
EXPECTED_FIXTURE = '06baef127289c71235c007622933a0ccc1b95b3c284c70997d61783ced8b9d55'
COMPONENTS = ('boot_aux', 'boot_locator', 'boot_prepare', 'boot_tree', 'fdt', 'macho')
SOURCES = [f'atlantic-core/{name}.c' for name in COMPONENTS]
HEADERS = [f'include/atlantic/{name}.h' for name in (*COMPONENTS, 'image_materialize')]
TEST = 'tests/c/test_boot_aux.c'


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    out = args.output.resolve()
    work = (ROOT / 'work').resolve()
    if out == work or out.exists() or not out.is_relative_to(work):
        parser.error('Use a new directory under work/')
    fixture = ROOT / FIXTURE
    if digest(fixture) != EXPECTED_FIXTURE:
        parser.error('The retained split-text fixture changed')
    out.mkdir(parents=True)
    commands = []

    def run(name, *command):
        commands.append([str(token).replace(str(ROOT), '<ROOT>') for token in command])
        result = subprocess.run(command, cwd=ROOT, text=True, capture_output=True, timeout=60)
        (out / (name + '.log')).write_text(result.stdout + result.stderr, encoding='utf-8', newline='\n')
        result.check_returncode()
        return result.stdout

    flags = ('-std=c11', '-Wall', '-Wextra', '-Werror', '-Iinclude')
    executable = out / 'boot-aux-host'
    run('host-build', 'clang', *flags, '-O1', '-g', '-fsanitize=address,undefined',
        '-fno-sanitize-recover=all', *SOURCES, TEST, '-o', executable)
    result = run('host-tests', executable, fixture)
    match = re.fullmatch(r'PASS: exact ATLC v1 wire and retained slots, (\d+) transactional rejections\n', result)
    if not match or int(match[1]) != 31:
        raise RuntimeError('Unexpected wire or rejection results')
    obj = out / 'boot-aux-arm64.o'
    run('target-build', 'clang', '--target=aarch64-none-elf', '-mcpu=cortex-a57',
        '-mstrict-align', '-mgeneral-regs-only', '-ffreestanding', '-fno-builtin',
        '-O2', *flags, '-c', SOURCES[0], '-o', obj)
    result = run('target-imports', 'llvm-nm', '--undefined-only', obj)
    if result.split() != ['U', 'atl_xnu_boot_locate']:
        raise RuntimeError('Unexpected freestanding ARM64 imports')
    report = {
        'schema_version': 1,
        'label': 'HOST_TESTED',
        'scope': 'ATLC_V1_BOOT_AUX_WIRE_AND_TRANSACTIONAL_PLACEMENT',
        'inputs': {name: digest(ROOT / name) for name in (*SOURCES, *HEADERS, TEST, FIXTURE,
                    str(Path(__file__).resolve().relative_to(ROOT)))},
        'commands': commands,
        'sanitizers': ['address', 'undefined'],
        'wire_bytes': 256,
        'native_tree_bytes': 560,
        'boot_args_bytes': 1152,
        'transactional_rejections': int(match[1]),
        'arm64_object_sha256': digest(obj),
        'arm64_imports': ['atl_xnu_boot_locate'],
        'xnu_executed': False,
        'phone_accessed': False,
    }
    (out / 'report.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8', newline='\n')
    print(json.dumps({'host_tested': True, 'transactional_rejections': int(match[1])}))


if __name__ == '__main__':
    main()
