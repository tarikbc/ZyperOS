#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""mk/fetch-vendor.sh's pins, offline (make check-corepins).

The cores are fetched from ZyperOS's own cores release, never from libretro's
buildbot, whose files change under the same name: on 2026-11-11 nine of the
nineteen pins no longer matched it and `make vendor` failed on a fresh clone.
So: the script fetches from the release and nowhere else; the release's tag
is not v<number>, so the installer and System Update never take it for a
ZyperOS version; every pin is one sha256, every core pinned once, and every
one has its source in THIRD-PARTY-LICENSES.md, which mk/release-cores.sh
puts in the release's notes. Whether vendor/ holds those files is
mk/release-cores.sh's to say (and mk/payload.sh's); this needs no network
and no vendor/."""
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parent.parent
fetch = (ROOT / 'mk/fetch-vendor.sh').read_text()
licenses = (ROOT / 'THIRD-PARTY-LICENSES.md').read_text()
code = '\n'.join(l for l in fetch.splitlines() if not l.lstrip().startswith('#'))
fails = []

tag = re.search(r'^CORES_TAG=(\S+)$', fetch, re.M)
if not tag:
    fails.append('mk/fetch-vendor.sh sets no CORES_TAG')
elif re.match(r'v\d', tag.group(1)):
    fails.append(f'CORES_TAG {tag.group(1)} looks like a ZyperOS version: the installer would offer it')
if 'buildbot.libretro.com' in code:
    fails.append('mk/fetch-vendor.sh fetches from the buildbot again; its files change under the same name')
if not re.search(r'^FROM=.*github\.com/tarikbc/ZyperOS/releases/download/\$CORES_TAG', fetch, re.M):
    fails.append('mk/fetch-vendor.sh does not fetch from the cores release')

pins = re.findall(r'^fetch_core\s+(\S+)\s+(\S+)\s*$', fetch, re.M)
names = [n for n, _ in pins]
for n, sha in pins:
    if not re.fullmatch(r'[0-9a-f]{64}', sha):
        fails.append(f'{n}: pin {sha} is not a sha256')
    if names.count(n) > 1:
        fails.append(f'{n} is pinned {names.count(n)} times')
    if not re.search(r'^\| `' + re.escape(n) + r'_libretro\.so` \|', licenses, re.M):
        fails.append(f'{n} has no row in THIRD-PARTY-LICENSES.md, so the release would name no source for it')
if len(pins) < 19:
    fails.append(f'only {len(pins)} pins read; the check is not reading the script')

for f in fails:
    print(f'FAIL {f}')
if fails:
    sys.exit(1)
print(f'ok: {len(pins)} cores pinned to the {tag.group(1)} release, each with its source')
