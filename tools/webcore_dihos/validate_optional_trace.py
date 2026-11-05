"""Build and execute the diagnostic candidate without touching sealed evidence."""
from pathlib import Path
import subprocess,sys
ROOT=Path(__file__).resolve().parents[2]
vm_tables='--vm-tables' in sys.argv
css_size=vm_tables or '--css-size' in sys.argv
pool_slots='--pool-slots' in sys.argv
indexeddb=css_size or pool_slots or '--indexeddb' in sys.argv
output='build/dihscover-css-size-validation' if css_size else 'build/dihscover-pool-slots-validation' if pool_slots else 'build/dihscover-indexeddb-validation' if indexeddb else 'build/dihscover-optional-trace-validation'
if vm_tables:output='build/dihscover-vm-tables-validation'
steps=[
    ('media/build_renderer.py',['--mse','--resume']),
    ('test_optional_abort.py',[]),
    ('media/test_mse_page.py',[]),
    ('media/test_mse_regressions.py',[]),
    ('media/prepare_mse_frontend.py',[])]
if indexeddb:steps.insert(2,('test_optional_abort.py',['--indexeddb-control']))
if vm_tables:steps[1:1]=[('test_optional_abort.py',['--vm-control','--old-tables']),('test_optional_abort.py',['--vm-control'])]
for script,extra in steps:
    print('Running '+script,flush=True)
    subprocess.run([sys.executable,ROOT/'tools/webcore_dihos'/script,'--output',output,*extra],cwd=ROOT,check=True)
