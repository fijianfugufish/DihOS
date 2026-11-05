"""Compile core units using the real CMake commands, without rebuilding JSC.

Headers/generators must already be present. PCH must be disabled by CMake,
so no flags are removed or guessed here. This does not link/run WebCore.
"""
from pathlib import Path
import argparse,hashlib,json,platform,shlex,subprocess
ROOT=Path(__file__).resolve().parents[2]
parser=argparse.ArgumentParser();parser.add_argument('--unit',action='append');parser.add_argument('--report',type=Path,default=ROOT/'build/webcore-native/report.json');args=parser.parse_args()
if platform.system()!='Linux':raise SystemExit('Run in ARM64 WSL')
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
path=args.report.resolve();config=json.loads(path.read_text())
if not config.get('webcore_configured') or config['target']!='aarch64-none-elf':raise SystemExit('Native configuration required')
if sha(ROOT/'tools/webcore_dihos/configure_native.py')!=config['builder_sha256']:raise SystemExit('Reconfigure changed builder')
for relative,digest in config.get('native_platform_sources',{}).items():
    if sha(ROOT/relative)!=digest:raise SystemExit('Changed native platform source')
for relative,digest in config.get('platform_files',{}).items():
    if sha(Path(config['source'])/relative)!=digest:raise SystemExit('Changed generated platform configuration')
build=Path(config['build']);database=build/'compile_commands.json'
if sha(database)!=config['compile_commands_sha256']:raise SystemExit('Changed compiler database')
commands=json.loads(database.read_text())
units=args.unit or ['html/parser/HTMLTokenizer.cpp','html/parser/HTMLEntityParser.cpp',
    'html/parser/HTMLEntitySearch.cpp','platform/text/SegmentedString.cpp',
    'css/parser/CSSTokenizer.cpp','css/parser/CSSParserToken.cpp',
    'css/parser/CSSTokenizerInputStream.cpp','platform/network/ResourceErrorBase.cpp']
out=path.parent/'units';out.mkdir(exist_ok=True)
result={'target':'aarch64-none-elf','configure_report_sha256':sha(path),'builder_sha256':sha(Path(__file__)),
        'units':[],'dom_executed':False,'page_rendered':False,'shipping_browser_changed':False}
failed=False
for unit in units:
    if unit.startswith('derived/'):
        expected=str(build/'WebCore/DerivedSources'/unit.removeprefix('derived/'))
        matches=[c for c in commands if c['file']==expected]
    else:matches=[c for c in commands if c['file'].endswith('/Source/WebCore/'+unit)]
    if len(matches)!=1:raise SystemExit('Expected one configured WebCore unit: '+unit)
    entry=matches[0];command=shlex.split(entry['command'])
    if '-include-pch' in command:raise SystemExit('Disable PCH through CMake before using this runner')
    obj=out/(unit.replace('/','_')+'.o');command[command.index('-o')+1]=str(obj)
    log=obj.with_suffix('.log')
    with log.open('w') as stream:
        try:code=subprocess.call(command,cwd=entry['directory'],stdout=stream,stderr=subprocess.STDOUT,timeout=120)
        except subprocess.TimeoutExpired:code=124
    record={'source':entry['file'],'source_sha256':sha(Path(entry['file'])),'argv':command,'exit_code':code,'log_sha256':sha(log)}
    if code:failed=True
    else:
        header=obj.read_bytes()[:20]
        if header[:6]!=b'\x7fELF\x02\x01' or int.from_bytes(header[16:18],'little')!=1 or int.from_bytes(header[18:20],'little')!=183:
            raise SystemExit('Expected real little-endian ARM64 relocatable object')
        record.update(object=str(obj),object_sha256=sha(obj),elf_machine=183)
    result['units'].append(record)
    result['all_selected_units_compiled']=not failed
    print(unit,code,flush=True)
    (out/'report.json').write_text(json.dumps(result,indent=2)+'\n')
raise SystemExit(1 if failed else 0)
