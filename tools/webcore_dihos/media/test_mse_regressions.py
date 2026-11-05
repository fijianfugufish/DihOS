"""Keep prior video evidence intact while testing the MSE-enabled browser ABI."""
from pathlib import Path
import argparse,subprocess,sys
ROOT=Path(__file__).resolve().parents[3]
MEDIA=ROOT/'tools/webcore_dihos/media'
p=argparse.ArgumentParser();p.add_argument('--output',type=Path);args=p.parse_args()
OUT=(ROOT/args.output).resolve() if args.output else ROOT/'build/dihscover-mse-player'
assert OUT.is_relative_to(ROOT/'build');OUT.mkdir(exist_ok=True)
subprocess.run([sys.executable,ROOT/'tools/webcore_dihos/prepare_large_test_inputs.py','--output',OUT],cwd=ROOT,check=True)
for name in ['test_regressions.py','test_elf_limits.py']:
    text=(MEDIA/name).read_text().replace('ROOT=Path(__file__).resolve().parents[3]',
        'ROOT=Path('+repr(str(ROOT))+')',1)
    # Keep the baseline replacement pattern intact in the regression generator.
    text=text.replace('build/dihscover-video-player',OUT.relative_to(ROOT).as_posix())
    if name=='test_regressions.py':
        marker="generated=OUT/'run-regressions.py';generated.write_text(text)"
        text=text.replace(marker,'text=text.replace('+repr("'-DUSER_TEST_ARENA_PAGES=131072'")+','+repr("'-DUSER_TEST_ARENA_PAGES=131072','-DUSER_CONSENT_TESTS=1'")+')\n'+marker)
    if name=='test_regressions.py':
        marker="generated=OUT/'run-regressions.py';generated.write_text(text)"
        old="ROOT/'build/user-runtime/user_program.inc'"
        new="ROOT/"+repr((OUT/'fixtures/user_program.inc').relative_to(ROOT).as_posix())
        assert marker in text;text=text.replace(marker,'text=text.replace('+repr(old)+','+repr(new)+')\n'+marker)
    runner=OUT/('run-'+name);runner.write_text(text)
    subprocess.run([sys.executable,runner],cwd=ROOT,check=True)
print('PASS: MSE-enabled browser regressions and loader sanitizer tests')
