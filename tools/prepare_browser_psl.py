"""Pin and compile the official UTF-8 PSL, including private/wildcard/exception rules."""
from pathlib import Path
import argparse,hashlib,json,unicodedata
p=argparse.ArgumentParser();p.add_argument('--input',required=True);p.add_argument('--output',required=True);args=p.parse_args()
src=Path(args.input);data=src.read_bytes();rules=set()
for line in data.decode('utf-8').splitlines():
 line=line.strip()
 if not line or line.startswith('//'):continue
 marker='!' if line.startswith('!') else ''
 if marker:line=line[1:]
 parts=[]
 for label in line.split('.'):
  label=unicodedata.normalize('NFC',label.lower())
  parts.append(label if label.isascii() else 'xn--'+label.encode('punycode').decode('ascii'))
 rule=marker+'.'.join(parts)
 assert all(c.isascii() and (c.isalnum() or c in '.-!*') for c in rule)
 rules.add(rule)
assert len(rules)>8000 and {'com','co.uk','github.io','*.ck','!www.ck'}<=rules
out=Path(args.output);out.parent.mkdir(parents=True,exist_ok=True)
out.write_text('// Generated from the official PSL (MPL-2.0); see packaged source and licence.\nstatic constexpr std::string_view pslRules[]={\n'+''.join('"'+r+'",\n' for r in sorted(rules))+'};\n')
out.with_suffix('.json').write_text(json.dumps({'source':'https://publicsuffix.org/list/public_suffix_list.dat','input_sha256':hashlib.sha256(data).hexdigest(),'output_sha256':hashlib.sha256(out.read_bytes()).hexdigest(),'rules':len(rules),'licence':'MPL-2.0','normalization':'NFC/lowercase, direct RFC3492 Punycode for non-ASCII PSL labels'},indent=2)+'\n')
print('PASS: pinned official PSL with',len(rules),'rules')
