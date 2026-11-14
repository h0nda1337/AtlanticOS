"""Audit the native cold-boot console queue and the original output-only RAM sink."""

import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import struct
import subprocess

from test_xnu_brk_scope import function, read_object, symbol_bytes
from test_xnu_memory_providers import imported_name
from test_xnu_platform import strong_external_import

ROOT = Path(__file__).resolve().parents[1]
GUARD = 'CONFIG_ATLANTIC_RAM_CONSOLE'
FLAG = '-D'+GUARD+'=1'
PATCH = '0035-select-output-only-ram-native-console.patch'
CONSOLE = 'osfmk/console/serial_console.c'
GENERAL = 'osfmk/console/serial_general.c'
ARM_INIT = 'osfmk/arm/arm_init.c'
STARTUP = 'osfmk/kern/startup.c'
ADAPTER = 'osfmk/kern/atlantic_early_log.c'
FORMATTER = 'osfmk/kern/printf.c'
UNITS = (CONSOLE, GENERAL, ARM_INIT, STARTUP, ADAPTER)
MIRRORS = {
    ADAPTER: 'xnu-port/platform/pe_early_log.c',
    'pexpert/pexpert/atlantic_log.h': 'xnu-port/platform/atlantic_log.h',
}
PATCH_PATHS = (*UNITS, 'osfmk/console/serial_protos.h', 'pexpert/pexpert/atlantic_log.h')
BRANCH26, PAGE21, PAGEOFF12, GOT_PAGE21, GOT_PAGEOFF12 = 2, 3, 4, 5, 6
STOCK_COLD_HARDWARE = {'_serial_init', '_PE_create_console', '_serial_set_on_demand',
                       '_switch_to_serial_console'}
STOCK_STARTUP_HARDWARE = {'_initialize_screen', '_serial_keyboard_init'}
STOCK_CONSOLE_HARDWARE = {'_serial_getc', '__serial_getc', '__vcgetc', '_serial_putc_options',
                          '_vcputc_options', '_vcgetc', '_cons_cinput', '_serial_keyboard_init',
                          '_switch_to_serial_console', '_switch_to_video_console',
                          '_switch_to_old_console', '_nmi_string'}


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def normalized_tokens(source, filename):
    # Only normalize diagnostic source line numbers and generated STARTUP labels.
    file_token = '"'+re.escape(filename)+'"'
    source = re.sub(r'\b(__startup_\w+?)_\d+\b', r'\1_LINE', source)
    source = re.sub(r'\b(MACH_ASSERT_[A-Z_]+,\s*)\d+(,\s*'+file_token+r')', r'\g<1>0\2', source)
    source = re.sub(r'('+file_token+r',\s*)\d+(\);\s*\}\))', r'\g<1>0\2', source)
    return source.split()


def external_relocations(image, name, kinds, width=None):
    raw, sections, symbols = image
    symbol = symbols[name]
    section = sections[symbol['section']-1]
    start = symbol['value']-section['address']
    size = len(symbol_bytes(image, name, width))
    definitions = {info['index']: key for key, info in symbols.items()}
    at = section['relocation_offset']
    end = at + 8*section['relocation_count']
    assert 0 <= at <= end <= len(raw)
    results = []
    for address, info in struct.iter_unpack('<iI', raw[at:end]):
        kind = info >> 28
        if not info & (1 << 27) or kind not in kinds or not start <= address < start+size:
            continue
        index = info & 0xffffff
        target = definitions[index] if index in definitions else imported_name(raw, index)
        results.append((address-start, kind, target))
    return sorted(results)


def branches(image, name):
    return [target for _, _, target in external_relocations(image, name, {BRANCH26})]


def words(image, name):
    return tuple(word for (word,) in struct.iter_unpack('<I', symbol_bytes(image, name)))


def audit_console(image, general):
    assert {'_kmem_alloc_guard', '_lck_mtx_init', '_lck_ticket_init'} <= set(branches(image, '_console_init'))
    assert {'_lck_mtx_lock', '_lck_mtx_try_lock', '_lck_ticket_lock', '_memcpy',
            '_atlantic_console_memory_putc'} <= set(branches(image, '_console_ring_try_empty'))
    assert '_atlantic_console_memory_putc' in branches(image, '_console_write_unbuffered')
    assert '_console_write' in branches(image, '_console_write_char')
    assert {'__cnputs', '_console_ring_try_empty'} <= set(branches(image, '_console_write'))
    assert '_atlantic_console_memory_putc' in branches(image, '__cnputs')
    for name in ('_console_printbuf_putc', '_console_printbuf_clear'):
        assert '_console_write' in branches(general, name)

    assert symbol_bytes(image, '_nconsops', 4) == struct.pack('<I', 1)
    assert symbol_bytes(image, '_cons_ops_index', 4) == struct.pack('<I', 0)
    assert symbol_bytes(image, '_cons_ops') == bytes(16)
    assert external_relocations(image, '_cons_ops', {0}) == [(0, 0, '_atlantic_console_memory_putc')]
    for name in ('_console_read_char', '_console_try_read_char'):
        assert branches(image, name) == ['_panic']
        code = words(image, name)
        assert code[2] & 0xffffffe0 == 0x35000060  # Nonzero route index branches to the panic path.
        assert code[3:5] == (0x12800000, 0xd65f03c0)  # Valid route returns -1.
        assert not any(word & 0xfffffc1f == 0xd63f0000 for word in code)
    assert words(general, '_console_is_serial')[:2] == (0x2a1f03e0, 0xd65f03c0)
    return {'native_drain_targets': branches(image, '_console_ring_try_empty'),
            'route_count': 1, 'route_index': 0, 'valid_input_return': -1}


def audit_adapter(image):
    for name in ('_PE_putc', '_PE_kputc'):
        assert external_relocations(image, name, {0}, 8) == [(0, 0, '_memory_putc')]
    assert symbol_bytes(image, '_disable_serial_output', 1) == b'\x01'

    _, sections, symbols = image
    boot_log = symbols['_boot_log']
    section = sections[boot_log['section']-1]
    assert boot_log['type'] == 0x0e and (section['segment'], section['name']) == ('__DATA', '__bss')
    for name in ('_memory_putc', '_atlantic_console_memory_putc'):
        refs = external_relocations(image, name, {PAGE21, PAGEOFF12, BRANCH26})
        assert [(kind, target) for _, kind, target in refs] == [
            (PAGE21, '_boot_log'), (PAGEOFF12, '_boot_log'), (BRANCH26, '_atl_early_log_append')]
        code = words(image, name)
        assert 0x52800022 in code and 0xaa1f03e3 in code  # One byte, with zero prior truncation.
    refs = external_relocations(image, '_PE_init_printf', {PAGE21, PAGEOFF12, GOT_PAGE21, GOT_PAGEOFF12, BRANCH26})
    phase = [(offset, kind, target) for offset, kind, target in refs
             if target in {'_PE_putc', '_memory_putc', '_console_write_char', '_panic'}]
    assert [(kind, target) for _, kind, target in phase] == [
        (PAGE21, '_PE_putc'), (PAGE21, '_memory_putc'), (PAGEOFF12, '_memory_putc'),
        (PAGEOFF12, '_PE_putc'), (GOT_PAGE21, '_console_write_char'),
        (GOT_PAGEOFF12, '_console_write_char'), (PAGEOFF12, '_PE_putc'), (BRANCH26, '_panic')]
    code = words(image, '_PE_init_printf')
    assert code[0] & 0xff00001f == 0x35000000  # Reject vm_initialized != FALSE.
    assert code[6] & 0xff00001f == 0x54000001  # Reject an already promoted or invalid pointer.
    store_at = phase[-2][0]//4
    assert code[store_at] & 0xffc00000 == 0xf9000000 and code[store_at+1] == 0xd65f03c0
    rejection = (code[0] >> 5) & 0x7ffff
    assert rejection == 6+((code[6] >> 5) & 0x7ffff) == store_at+2
    assert rejection <= phase[-1][0]//4
    return {'early_functions_share_ram_bss': True, 'late_putchar_store_relocations': phase,
            'physical_serial_side_channel_disabled': True}


def audit_cold_boot(arm, startup):
    cold = branches(arm, '_arm_init')
    phase = tuple(cold.index(name) for name in ('_arm_vm_init', '_PE_consistent_debug_inherit', '_PE_init_printf'))
    assert phase[0] < phase[1] < phase[2]
    assert '_kernel_startup_initialize_upto' in cold[phase[1]+1:phase[2]]
    assert cold.count('_PE_init_printf') == 1 and STOCK_COLD_HARDWARE.isdisjoint(cold)
    offset = next(offset for offset, _, name in external_relocations(arm, '_arm_init', {BRANCH26})
                  if name == '_PE_init_printf')
    assert words(arm, '_arm_init')[offset//4-1] == 0x2a1f03e0  # w0 = FALSE.
    resume = branches(arm, '_arm_init_cpu')
    assert resume.count('_serial_init') == 1

    boot = branches(startup, '_kernel_bootstrap')
    assert boot.index('_vm_mem_bootstrap') < boot.index('_console_init') < boot.index('_sched_init')
    return {'cold_boot_console_precedes_native_ring_allocation': True,
            'native_resume_serial_init_remains_pending': True,
            'startup_ram_ring_init_after_vm_bootstrap': True}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', type=Path, required=True)
    parser.add_argument('--link', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    build = args.build.resolve()
    base = json.loads(build.read_text())
    lock = json.loads((ROOT/'xnu-port/patches.lock.json').read_text())
    assert base['port_id'] == lock['port_id'] and base['upstream_commit'] == lock['upstream_commit']
    assert not base['failed_units'] and base['layout_provider']
    assert not base['xnu_executed'] and not base['phone_accessed']
    for rel, expected in base['inputs'].items():
        assert sha(ROOT/rel) == expected, rel

    source = ROOT/'work/xnu-lab-sources'/base['port_id'][:16]
    output = args.output.resolve()
    if not output.is_relative_to((ROOT/'work').resolve()) or output.exists():
        parser.error('Use a new output directory under work/')
    output.mkdir(parents=True)
    checks, commands = [], []

    def passed(name, **details):
        checks.append({'name': name, 'passed': True, **details})

    def run(name, command, cwd=ROOT, expected=0, diagnostic=None):
        proc = subprocess.run(command, cwd=cwd, text=True, capture_output=True, timeout=90)
        commands.append({'argv': [str(arg).replace(str(ROOT), '<ROOT>') for arg in command],
                         'cwd': str(cwd).replace(str(ROOT), '<ROOT>')})
        (output/(name+'.log')).write_text((proc.stdout+proc.stderr).replace(str(ROOT), '<ROOT>'),
                                         encoding='utf-8', newline='\n')
        assert proc.returncode == expected and (not diagnostic or diagnostic in proc.stderr), name+': '+proc.stderr[-2400:]
        return proc.stdout

    patch = ROOT/'xnu-port'/PATCH
    assert sha(patch) == lock['patches'][PATCH]
    assert base['patches'] == lock['patches']
    locked = {entry['path']: entry['postimage_sha256'] for entry in lock['canonical_lf_files']}
    control = output/'prior-source'
    source_hashes = {}
    for rel in PATCH_PATHS:
        source_hashes[rel] = sha(source/rel)
        assert source_hashes[rel] == locked[rel], rel
        if rel in MIRRORS:
            assert source_hashes[rel] == sha(ROOT/MIRRORS[rel]), rel
        dest = control/rel
        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source/rel, dest)
    # A local repository prevents git apply from treating paths as relative to ROOT.
    run('initialize-prior-source', ['git', 'init', '-q'], cwd=control)
    run('reverse-exact-ram-console-patch', ['git', 'apply', '--reverse', str(patch)], cwd=control)
    preceding_hashes = {rel: sha(control/rel) for rel in PATCH_PATHS}
    assert all(preceding_hashes[rel] != source_hashes[rel] for rel in PATCH_PATHS)
    passed('current-locked-patch-and-original-atlantic-mirrors', patch_sha256=sha(patch),
           source_sha256=source_hashes, preceding_source_sha256=preceding_hashes)

    # Original lock/queue and printbuf definitions retain the exact native text.
    ring_functions = ('console_init', 'console_io_allowed', 'console_ring_reserve_space',
                      'console_ring_unreserve_space', 'console_ring_put', 'console_ring_try_empty',
                      'console_suspend', 'console_resume', 'console_write', 'console_write_char',
                      'console_write_unbuffered')
    for rel, functions in [(CONSOLE, ring_functions),
                           (GENERAL, ('console_printbuf_state_init', 'console_printbuf_putc', 'console_printbuf_clear'))]:
        before, after = (control/rel).read_text(), (source/rel).read_text()
        upstream = (ROOT/'xnu-upstream'/rel).read_text()
        assert before == upstream
        assert all(function(before, name) == function(after, name) for name in functions)
    assert sha(source/FORMATTER) == hashlib.sha256((ROOT/'xnu-upstream'/FORMATTER).read_text().encode()).hexdigest()
    passed('upstream-ring-formatted-buffer-and-native-formatter-preserved')

    records = {item['source']: item for item in base['objects']}
    recipes = {recipe[-3].replace('<ROOT>', str(ROOT)): recipe for recipe in base['commands']
               if len(recipe) > 4 and recipe[-4] == '-c'}
    paths, images, object_hashes = {}, {}, {}
    for rel in (*UNITS, FORMATTER):
        obj = (build.parent/records[rel]['object']).resolve()
        assert obj.is_relative_to(build.parent) and sha(obj) == records[rel]['object_sha256']
        assert sha(source/rel) == records[rel]['source_sha256']
        paths[rel], images[rel], object_hashes[rel] = obj, read_object(obj), sha(obj)
        command = recipes[str(source/rel)]
        assert FLAG in command and sha(Path(command[0].replace('<ROOT>', str(ROOT)))) == base['layout_provider']['compiler_sha256']
    for rel in UNITS:
        recipe = [arg.replace('<ROOT>', str(ROOT)) for arg in recipes[str(source/rel)][:-4]]
        off = [arg for arg in recipe if arg != FLAG]
        before = run(Path(rel).stem+'-standard-before', [off[0], '-I'+str(control/'osfmk'),
                     '-I'+str(control/'pexpert'), *off[1:], '-E', '-P', str(control/rel)])
        after = run(Path(rel).stem+'-standard-after', [*off, '-E', '-P', str(source/rel)])
        assert normalized_tokens(before, Path(rel).name) == normalized_tokens(after, Path(rel).name), rel
    passed('console-guard-off-preserves-preceding-firmware-tokens', units=list(UNITS))

    target = [arg.replace('<ROOT>', str(ROOT)) for arg in recipes[str(source/ADAPTER)][:-4]]
    for name, flags in [('non-firmware', ['-UCONFIG_ATLANTIC_FIRMWARE']),
                        ('bsd', ['-DMACH_BSD=1']), ('iokit', ['-DIOKIT=1'])]:
        run('reject-'+name, [*target, *flags, '-E', str(source/'pexpert/pexpert/atlantic_log.h')],
            expected=1, diagnostic='RAM console requires the explicit firmware-only XNU profile')
    passed('ram-console-rejects-hardware-userland-profiles')

    for rel, symbols in {
        CONSOLE: {'_atlantic_console_memory_putc', '_kmem_alloc_guard', '_lck_mtx_lock', '_lck_ticket_lock'},
        GENERAL: {'_console_write'},
        ARM_INIT: {'_PE_init_printf', '_arm_vm_init'},
        STARTUP: {'_console_init', '_vm_mem_bootstrap'},
        ADAPTER: {'_console_write_char', '_atl_early_log_append', '_panic'},
        FORMATTER: {'_console_is_serial', '_disable_serial_output', '_os_log_with_args'},
    }.items():
        assert all(strong_external_import(paths[rel], symbol) for symbol in symbols), rel
    for rel, hardware in [(CONSOLE, STOCK_CONSOLE_HARDWARE), (GENERAL, STOCK_CONSOLE_HARDWARE),
                          (ADAPTER, STOCK_CONSOLE_HARDWARE), (STARTUP, STOCK_STARTUP_HARDWARE)]:
        listing = run(Path(rel).stem+'-all-symbols', ['llvm-nm', str(paths[rel])])
        assert hardware.isdisjoint(row.split()[-1] for row in listing.splitlines() if row.strip())
    assert b'AtlanticOS RAM console received an invalid printf phase @%s:%d\0' in images[ADAPTER][0]
    passed('real-mach-o-imports-and-no-cold-console-device-providers')
    passed('native-output-queue-and-explicitly-unsupported-input', **audit_console(images[CONSOLE], images[GENERAL]))
    passed('ram-sink-and-single-allowed-pe-printf-promotion', **audit_adapter(images[ADAPTER]))
    passed('cold-boot-phase-ordering-verified-in-actual-call-relocations', **audit_cold_boot(images[ARM_INIT], images[STARTUP]))

    link_report = args.link.resolve()
    link = json.loads(link_report.read_text())
    log = link_report.with_name('link.log')
    assert sha(log) == link['link_log_sha256']
    assert link['build_report_sha256'] == sha(build) and link['port_id'] == base['port_id']
    assert link['undefined_symbols_must_error'] and link['warnings_are_errors'] and not link['dead_stripping_requested']
    assert link['link_exit_code'] and not link['output_exists'] and not link['kernel_linked']
    assert not link['duplicate_symbol_error_count']
    assert not link['xnu_executed'] and not link['phone_accessed']
    assert re.search(r'error: undefined symbol: _os_log_with_args\s*$', log.read_text(), re.M)
    passed('actual-strict-link-keeps-native-os-log-import-unresolved', log_sha256=sha(log))

    report = {'schema_version': 1, 'label': 'HOST_TESTED',
              'scope': 'NATIVE_RAM_CONSOLE_SOURCE_AND_MACH_O_ONLY',
              'port_id': base['port_id'], 'upstream_commit': base['upstream_commit'],
              'build_report_sha256': sha(build), 'link_report_sha256': sha(link_report),
              'tool_sha256': sha(Path(__file__)), 'object_sha256': object_hashes,
              'outcomes': checks, 'commands': commands, 'xnu_executed': False, 'phone_accessed': False}
    (output/'report.json').write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8', newline='\n')
    print(json.dumps({'passed': len(checks), 'checked_objects': len(images), 'xnu_executed': False}))


if __name__ == '__main__':
    main()
