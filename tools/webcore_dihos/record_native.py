"""Record native WebCore compile evidence without claiming guest execution."""
from pathlib import Path
import hashlib,json,platform
ROOT=Path(__file__).resolve().parents[2]
if platform.system()!='Linux':raise SystemExit('Run in ARM64 WSL to verify source/object paths')
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
base=ROOT/'build/webcore-native';config_path=base/'report.json';units_path=base/'units/report.json'
config=json.loads(config_path.read_text());units=json.loads(units_path.read_text())
if not config.get('webcore_configured') or config['target']!='aarch64-none-elf':raise SystemExit('Native configuration missing')
if sha(ROOT/'tools/webcore_dihos/configure_native.py')!=config['builder_sha256']:raise SystemExit('Configure builder changed')
if sha(config_path)!=units['configure_report_sha256']:raise SystemExit('Compilation used a different configuration')
if sha(ROOT/'tools/webcore_dihos/compile_units.py')!=units['builder_sha256']:raise SystemExit('Compiler runner changed')
if not units.get('all_selected_units_compiled') or len(units['units'])!=8:raise SystemExit('Expected eight successful native units')
for entry in units['units']:
    if entry['exit_code'] or entry['elf_machine']!=183:raise SystemExit('Native compile failed')
    if sha(Path(entry['source']))!=entry['source_sha256'] or sha(Path(entry['object']))!=entry['object_sha256']:
        raise SystemExit('Source/object changed after compilation')
for name,script in [('webcore-xml','build_xml.py'),('webcore-dependencies','build_dependencies.py')]:
    dep=json.loads((ROOT/'build'/name/'report.json').read_text())
    if dep['builder_sha256']!=sha(ROOT/'tools/webcore_dihos'/script):raise SystemExit('Dependency recipe changed')
    libraries=dep.get('libraries',{}).values() if name=='webcore-dependencies' else [{'path':dep['library'],'sha256':dep['library_sha256']}]
    for library in libraries:
        if sha(Path(library['path']))!=library['sha256']:raise SystemExit('Dependency archive changed')
proof={'milestone':'native WebCore configuration and core object compilation',
       'target':'aarch64-none-elf','webkit_version':'2.54.2','archive_sha256':config['archive_sha256'],
       'configure_report_sha256':sha(config_path),'units_report_sha256':sha(units_path),
       'xml_report_sha256':config['xml_report_sha256'],'dependency_report_sha256':config['dependency_report_sha256'],
       'compiled_units':[{k:v for k,v in entry.items() if k in ['source','source_sha256','object_sha256','elf_machine']} for entry in units['units']],
       'webcore_configured':True,'complete_webcore_library_built':False,'native_execution_tested':False,
       'dom_executed':False,'page_rendered':False,'shipping_browser_changed':False,
       'next_boundaries':['Native ResourceRequest/ResourceResponse/CertificateInfo contracts',
                          'WebCore resource loader over checked asynchronous SACX HTTP',
                          'Page/graphics/fonts/input/surface integration and local document execution'],
       'limits':['No native graphics backend yet','No registered SQLite VFS',
                 'Experimental XML pin requires review before shipping','HTTPS peer authentication remains unavailable']}
destination=ROOT/'docs/browser-evaluation/native-webcore-core-build-20261031.json'
destination.write_text(json.dumps(proof,indent=2)+'\n')
print('Verified native WebCore configuration and 8 ARM64 object files; no execution claim')
