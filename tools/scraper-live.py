#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""MENU > Scraper's page through the real launcher, live, on the Mac.

ZYPEROS_SCRAPER_TEST makes the launcher open the page at start and play a
comma script on it (src/main.c, scp_test): button names (A B X Y START UP
DOWN LEFT RIGHT), wait=MS, run=MS (until a run ends, at most MS) and
shot=PATH. Each step prints a "scraper-test:" line with where the page is.

This script plays one pass on readme-shots' demo card: the plan, Systems,
one system's videos on, Videos for, Settings, then Fetch, with libretro's
real covers over the network, and the done screen. It is not a check (it
needs the network); it is how the page's loop was tried before a device.

Usage: tools/scraper-live.py OUT [light|dark]   (after make native)"""
import importlib.util
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
out = Path(sys.argv[1])
theme = sys.argv[2] if len(sys.argv) > 2 else 'light'
spec = importlib.util.spec_from_file_location('readme_shots', ROOT / 'tools' / 'readme-shots.py')
R = importlib.util.module_from_spec(spec)
spec.loader.exec_module(R)
out.mkdir(parents=True, exist_ok=True)
script = ','.join([
    'wait=2500', f'shot={out}/live-plan.png',
    'Y', 'wait=700', f'shot={out}/live-systems.png',
    'RIGHT', 'X', 'wait=600', f'shot={out}/live-systems-x.png',
    'Y', 'wait=900', 'B', 'wait=600',
    'B', 'wait=600', f'shot={out}/live-plan-after.png',
    'START', 'wait=900', 'B', 'wait=600',
    'A', 'wait=6000', f'shot={out}/live-run.png',
    'run=90000', 'wait=500', f'shot={out}/live-done.png',
    'B', 'wait=500',
])
with tempfile.TemporaryDirectory(prefix='zyperos-scraper-live-') as tmp:
    card = Path(tmp) / 'demo'
    R.demo_card(card, None, None)
    (card / 'ZyperOS/.last').write_text('PS1\n' + R.FOCUS + '.chd\n')
    for ca in sorted((ROOT / 'res/ssl').glob('*.pem')):
        shutil.copy(ca, card / 'ZyperOS' / 'cacert.pem')
        break
    R.set_settings(card, {'tour.done': '1'}, True)
    if theme == 'dark':
        R.set_settings(card, {'appearance': 'dark'}, True)
    env = dict(R.env_for(card), ZYPEROS_SCRAPER_TEST=script)
    r = subprocess.run([str(ROOT / 'build-native' / 'zyperos')], cwd=ROOT, env=env, text=True,
                       errors='replace', stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=240)
    (out / 'live.log').write_text(r.stdout)
    print('exit', r.returncode)
    for line in r.stdout.splitlines():
        if line.startswith(('scraper-test:', 'art: finished')):
            print(line[:200])
