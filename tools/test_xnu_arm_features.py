"""Audit native commpage feature storage, SME configuration and target objects."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess

from test_xnu_brk_scope import read_object, symbol_bytes

ROOT = Path(__file__).resolve().parents[1]
COMMPAGE = 'osfmk/arm/commpage/commpage.c'
SME = 'osfmk/arm64/sme.c'
CATALOG = 'osfmk/arm/arm_features.inc'


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    base = json.loads(args.build.read_text())
    lock = json.loads((ROOT/'xnu-port/patches.lock.json').read_text())
    assert base['port_id'] == lock['port_id'] and not base['failed_units'] and base['layout_provider']
    for file, expected in base['inputs'].items():
        assert sha(ROOT/file) == expected, file
    source = ROOT/'work/xnu-lab-sources'/base['port_id'][:16]
    output = args.output.resolve()
    if not output.is_relative_to(ROOT/'work') or output.exists():
        parser.error('Use a new output directory under work/')
    output.mkdir(parents=True)
    commands, outcomes = [], []

    def run(name, command, data=None, expected=0, diagnostic=None):
        proc = subprocess.run(command, cwd=ROOT, input=data, text=True, capture_output=True, timeout=90)
        commands.append([str(s).replace(str(ROOT), '<ROOT>') for s in command])
        (output/(name+'.log')).write_text((proc.stdout+proc.stderr).replace(str(ROOT), '<ROOT>'),
                                         encoding='utf-8', newline='\n')
        assert proc.returncode == expected and (not diagnostic or diagnostic in proc.stderr), proc.stderr[-2400:]
        return proc.stdout, proc.stderr

    def passed(name, **details):
        outcomes.append({'name':name, 'passed':True, **details})

    recipe = next(c for c in base['commands'] if any(s.endswith('/'+COMMPAGE) for s in c))
    assert recipe[-4] == '-c'
    target = [s.replace('<ROOT>', str(ROOT)) for s in recipe[:-4]]
    assert sha(Path(target[0])) == base['layout_provider']['compiler_sha256']
    records = {item['source']:item for item in base['objects']}
    for name in [COMMPAGE, SME]:
        assert sha(source/name) == records[name]['source_sha256']
        assert sha(args.build.parent/records[name]['object']) == records[name]['object_sha256']
    modified = (source/COMMPAGE).read_text()
    original = (ROOT/'xnu-upstream'/COMMPAGE).read_text()
    start = modified.index('#if CONFIG_ATLANTIC_FIRMWARE\n/* The firmware profile')
    fallback = modified.index('#else\n/* For sysctl access from BSD side */', start)+len('#else\n')
    close = modified.index('#endif /* CONFIG_ATLANTIC_FIRMWARE */\n', fallback)
    end = close+len('#endif /* CONFIG_ATLANTIC_FIRMWARE */\n')
    assert modified[:start]+modified[fallback:close]+modified[end:] == original
    assert (source/SME).read_text() == (ROOT/'xnu-upstream'/SME).read_text()
    passed('full-native-commpage-probe-bodies-and-sme-source-preserved')

    for bsd in (0, 1):
        command = [*target, '-UCONFIG_ATLANTIC_FIRMWARE', '-UMACH_BSD', '-DMACH_BSD='+str(bsd),
                   '-E', '-P', '-x', 'c', '-']
        before = run('upstream-declarations-bsd-'+str(bsd), command, original[start:start+close-fallback])
        after = run('nonfirmware-declarations-bsd-'+str(bsd), command, modified[start:end])
        assert after == before
        passed('nonfirmware-declaration-control', mach_bsd=bsd)
    run('reject-firmware-bsd', [*target, '-DMACH_BSD=1', '-E', '-x', 'c', '-'], modified, 1,
        'Feature storage is already provided by the BSD sysctl source')
    passed('firmware-and-bsd-cannot-both-own-the-storage')

    catalog = (source/CATALOG).read_text()
    assert catalog == (ROOT/'xnu-upstream'/CATALOG).read_text()
    names = {'_gARM_'+name for name in re.findall(r'^ARM_FEATURE_FLAG\((\w+)\);', catalog, re.M)}
    assert len(names) == 57
    for rel, fields in [(COMMPAGE, names|{'_gUCNormalMem'}), (SME, {'_sme_max_svl_b','_sme_version'})]:
        obj_path = args.build.parent/records[rel]['object']
        obj = read_object(obj_path)
        listing, _ = run(Path(rel).stem+'-external-bindings',
                         ['llvm-nm','--format=darwin','--defined-only','--extern-only',str(obj_path)])
        for name in sorted(fields):
            symbol = obj[2][name]
            section = obj[1][symbol['section']-1]
            assert symbol['type'] & 1 and symbol['value'] % 4 == 0
            assert (section['segment'], section['name']) == ('__DATA','__const')
            assert symbol_bytes(obj, name, 4) == bytes(4)
            binding = next(line for line in listing.splitlines() if line.endswith(' '+name))
            assert ' external ' in binding and 'weak' not in binding
        passed(Path(rel).stem+'-strong-aligned-i32-native-storage', symbols=sorted(fields))

    commpage_path = args.build.parent/records[COMMPAGE]['object']
    disassembly, _ = run('commpage-register-probes',
                         ['llvm-objdump','--macho','--disassemble','--symbolize-operands','--reloc',str(commpage_path)])
    for register in ['ID_AA64ISAR0_EL1','ID_AA64ISAR1_EL1','ID_AA64PFR0_EL1','FPCR']:
        assert re.search(r'\bmrs\s+\w+,\s*'+register+r'\b', disassembly, re.I)
    for name in ['_gARM_FEAT_LSE','_gARM_AdvSIMD']:
        assert re.search(r'\bstr\s+\w+,\s*\[\w+,\s*'+name+r'@PAGEOFF\]', disassembly)
    passed('native-probes-emit-register-reads-and-catalog-stores')

    macros, _ = run('sme-target-macros', [*target, '-E', '-dM', str(source/SME)])
    assert not re.search(r'^#define HAS_ARM_FEAT_SME\b', macros, re.M)
    sme_path = args.build.parent/records[SME]['object']
    imports, _ = run('sme-imports', ['llvm-nm','--undefined-only',str(sme_path)])
    assert not imports.strip()
    assert set(records[SME]['defined_external_symbols']) == {'_arm_sme_version','_sme_max_svl_b','_sme_version'}
    image = read_object(sme_path)
    code = symbol_bytes(image, '_arm_sme_version', 12)
    assert struct.unpack('<III',code) == (0x90000008, 0xb9400100, 0xd65f03c0)
    raw, sections, symbols = image
    symbol = symbols['_arm_sme_version']
    section = sections[symbol['section']-1]
    start = symbol['value']-section['address']
    first = section['relocation_offset']
    limit = first+8*section['relocation_count']
    assert 0 <= first <= limit <= len(raw)
    relocations = {offset-start:word for offset, word in struct.iter_unpack('<II', raw[first:limit])
                   if start <= offset < start+len(code)}
    index = symbols['_sme_version']['index']
    assert relocations == {0:0x3d000000|index, 4:0x4c000000|index}
    passed('native-sme-getter-loads-disabled-profile-capability', code=code.hex(),
           sme_enabled=False, hardware_sme_observed=False)

    report = {'schema_version':1, 'label':'HOST_TESTED',
              'scope':'NATIVE_ARM_FEATURE_STORAGE_AND_DISABLED_SME_TARGET_OBJECTS',
              'port_id':base['port_id'], 'build_report_sha256':sha(args.build),
              'catalog_sha256':sha(source/CATALOG), 'tool_sha256':sha(Path(__file__)),
              'outcomes':outcomes, 'commands':commands, 'xnu_executed':False,
              'commpage_populated':False, 'phone_accessed':False}
    (output/'report.json').write_text(json.dumps(report,indent=2)+'\n', encoding='utf-8',newline='\n')
    print(json.dumps({'passed':len(outcomes),'arm_feature_catalog_entries':len(names),'sme_enabled':False}))


if __name__ == '__main__':
    main()
