from pathlib import Path
import json
root=Path(__file__).resolve().parents[1]
source=root/'tools/browser_fixtures/blob_probe.html'
(source.with_suffix('.inc')).write_text('#define BLOB_TEST_HTML '+json.dumps(source.read_text())+'\n')
