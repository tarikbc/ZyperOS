#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Makes installer/THIRD-PARTY.md, the list of what ZyperOS Installer is built
from: its own code's licenses, Tauri, the web view each system draws the
window with, the fonts in ui/fonts (name, version and copyright read from the
font files), and every Rust crate compiled into the three apps, with its
license, its repository and which apps carry it.

    python3 tools/third-party-list.py            writes THIRD-PARTY.md
    python3 tools/third-party-list.py -o FILE    writes FILE instead (- for stdout)

The crates come from cargo, with Cargo.lock locked (never updated): for each
target the apps are built for, `cargo tree --edges normal` gives what cargo
resolves as normal dependencies, procedural macros among them, and not what
only build scripts or tests use; `cargo metadata` gives each crate's license
and repository. Cargo runs offline first and asks the network only when a
crate it needs is not on this computer yet.

The same Cargo.lock gives the same file, byte for byte, on any computer: no
paths, dates or tool versions go in, and everything is sorted (versions as
numbers, so 0.10 comes after 0.9). tools/release.sh makes the list again and
stops when it is not the committed file.

Python's standard library only.
"""
from pathlib import Path
import argparse
import json
import re
import struct
import subprocess
import sys
import urllib.parse

INSTALLER = Path(__file__).resolve().parent.parent
REPO = INSTALLER.parent
MANIFEST = INSTALLER / 'src-tauri' / 'Cargo.toml'
OUT = INSTALLER / 'THIRD-PARTY.md'
FONTS = INSTALLER / 'ui' / 'fonts'
SCRIPT = 'tools/third-party-list.py'

# The apps and the targets each one is built for (tools/release.sh): the macOS
# app is one universal build of two, named here for what a Mac has inside.
APPS = [
    ('macOS', [('aarch64-apple-darwin', 'Apple Silicon'), ('x86_64-apple-darwin', 'Intel')]),
    ('Windows', [('x86_64-pc-windows-msvc', None)]),
    ('Linux', [('x86_64-unknown-linux-gnu', None)]),
]
TRIPLES = [t for _, targets in APPS for t, _ in targets]

# Tauri draws the window with the system's own web view; no app carries one.
WEB_VIEWS = [
    ('macOS', "WKWebView, Apple's WebKit, which is part of macOS"),
    ('Windows', 'Microsoft Edge WebView2, which Windows provides'),
    ('Linux', "WebKitGTK (webkit2gtk-4.1) with GTK 3, from the distribution's own packages"),
]

CRATES_IO = 'registry+https://github.com/rust-lang/crates.io-index'


def say(msg):
    print(f'third-party-list: {msg}', file=sys.stderr)


def fail(msg):
    say(msg)
    sys.exit(1)


_online = False


def cargo(*args):
    """cargo's output for args, on the installer's manifest with Cargo.lock
    locked: offline first, and with the network only when that fails."""
    global _online
    cmd = ['cargo', *args, '--manifest-path', str(MANIFEST), '--locked', '--color', 'never']
    try:
        if not _online:
            r = subprocess.run(cmd + ['--offline'], capture_output=True)
            if r.returncode == 0:
                return r.stdout.decode('utf-8')
            say("cargo's registry here lacks something it needs: asking the network")
            _online = True
        r = subprocess.run(cmd, stdout=subprocess.PIPE)
    except FileNotFoundError:
        fail('needs cargo (https://rustup.rs)')
    if r.returncode != 0:
        fail(f"`{' '.join(cmd[:2])}` failed (above); is src-tauri/Cargo.lock up to date with Cargo.toml?")
    return r.stdout.decode('utf-8')


def compiled(target):
    """(name, version) of every package cargo resolves for target as a normal
    dependency, the installer's own crate among them."""
    out = cargo('tree', '--target', target, '--edges', 'normal', '--prefix', 'none', '--format', '{p}')
    found = set()
    for line in out.splitlines():
        if not line.strip():
            continue
        # "name vX.Y.Z", then maybe " (source)", " (proc-macro)" or " (*)"
        m = re.match(r'(\S+) v(\S+)(?: |$)', line)
        if not m:
            fail(f'cargo tree printed a line this script does not read: {line!r}')
        found.add((m.group(1), m.group(2)))
    return found


def version_key(v):
    """Semantic Versioning's order: numbers as numbers, a pre-release before
    its release, then the whole string, so equal keys can't be."""
    core = v.partition('+')[0]
    core, _, pre = core.partition('-')
    nums = tuple((0, int(x), '') if x.isdigit() else (1, 0, x) for x in core.split('.'))
    if pre:
        rank = (0, tuple((0, int(x), '') if x.isdigit() else (1, 0, x) for x in pre.split('.')))
    else:
        rank = (1, ())
    return (nums, rank, v)


def cell(text):
    """text for a Markdown table cell: one line, and nothing in it read as a
    column break, a link, code, emphasis or HTML."""
    text = ' '.join(str(text).split())
    return re.sub(r'([\\`*_\[\]<>|&~])', r'\\\1', text)


def url_cell(url):
    """An autolink, with what an autolink or a table can't hold encoded."""
    url = urllib.parse.quote(url.strip(), safe=":/?#@!$'()*+,;=%-._~[]&")
    return f'<{url}>'


def elsewhere(p):
    """Where a crate with no repository in its metadata can be found, and
    what to call it: its git source, or its page on crates.io."""
    source = p.get('source') or ''
    if source.startswith('git+'):
        return re.split(r'[?#]', source[4:], 1)[0], 'its git source'
    if source in (CRATES_IO, 'sparse+https://index.crates.io/'):
        return f"https://crates.io/crates/{p['name']}/{p['version']}", 'its page on crates.io'
    return None, None


def code(name):
    return f'`{name}`' if re.fullmatch(r'[A-Za-z0-9_./+-]+', name) else cell(name)


# ---------------------------------------------------------------------------
# Fonts: the name table of a TrueType or OpenType file

def font_names(path):
    """name ID -> string, preferring Windows English, then any Windows or
    Unicode record, then Macintosh Roman."""
    data = path.read_bytes()
    if data[:4] not in (b'\x00\x01\x00\x00', b'OTTO', b'true'):
        fail(f'{path.name}: not a TrueType or OpenType font')
    (count,) = struct.unpack('>H', data[4:6])
    for i in range(count):
        tag, _, off, length = struct.unpack('>4sIII', data[12 + 16 * i:28 + 16 * i])
        if tag == b'name':
            break
    else:
        fail(f'{path.name}: no name table')
    table = data[off:off + length]
    _, n, strings = struct.unpack('>HHH', table[:6])
    best = {}
    for i in range(n):
        pid, eid, lid, nid, ln, so = struct.unpack('>6H', table[6 + 12 * i:18 + 12 * i])
        raw = table[strings + so:strings + so + ln]
        if pid == 3 and eid in (0, 1, 10):
            rank, text = (0 if lid == 0x409 else 1), raw.decode('utf-16-be', 'replace')
        elif pid == 0:
            rank, text = 2, raw.decode('utf-16-be', 'replace')
        elif pid == 1 and eid == 0:
            rank, text = (3 if lid == 0 else 4), raw.decode('mac_roman', 'replace')
        else:
            continue
        if text.strip() and (nid not in best or rank < best[nid][0]):
            best[nid] = (rank, text.strip())
    return {nid: text for nid, (_, text) in best.items()}


def font_row(path):
    names = font_names(path)
    name = names.get(4) or ' '.join(filter(None, [names.get(16) or names.get(1), names.get(17) or names.get(2)]))
    version = names.get(5, '')
    m = re.match(r'\s*Version\s+(\d+(?:\.\d+)*)', version, re.I)
    if m:
        version = m.group(1)
    licence = names.get(13, '')
    m = re.search(r'SIL Open Font License,?\s+Version\s+(\d+(?:\.\d+)*)', licence, re.I)
    if m:
        licence = f'SIL Open Font License {m.group(1)}'
    return [f'[{code(path.name)}](ui/fonts/{urllib.parse.quote(path.name)})', cell(name or 'not in the font'),
            cell(version or 'not in the font'), cell(names.get(0, 'not in the font')),
            cell(licence or 'not in the font')]


# ---------------------------------------------------------------------------

def read(path):
    if not path.is_file():
        fail(f'{path.relative_to(REPO)} is missing, and the list points to it')
    return path.read_text(encoding='utf-8')


def table(head, rows, align=None):
    align = align or ['---'] * len(head)
    lines = ['| ' + ' | '.join(head) + ' |', '|' + '|'.join(align) + '|']
    lines += ['| ' + ' | '.join(r) + ' |' for r in rows]
    return '\n'.join(lines)


def build():
    # Every package each target compiles, then what cargo knows of each
    per_target = {t: compiled(t) for t in TRIPLES}
    platforms = [a for t in TRIPLES for a in ('--filter-platform', t)]
    meta = json.loads(cargo('metadata', '--format-version', '1', *platforms))
    packages = {}
    # crates.io's first, so a name and version from two sources reads the same everywhere
    for p in sorted(meta['packages'], key=lambda p: (p.get('source') != CRATES_IO, p.get('source') or '')):
        packages.setdefault((p['name'], p['version']), p)
    own = {(p['name'], p['version']) for p in meta['packages'] if p['id'] in meta['workspace_members']}
    for t in TRIPLES:
        per_target[t] -= own
        for key in per_target[t]:
            if key not in packages:
                fail(f'cargo tree lists {key[0]} {key[1]} for {t}, and cargo metadata does not know it')
    crates = sorted(set().union(*per_target.values()),
                    key=lambda k: (k[0].lower(), k[0], version_key(k[1])))
    per_app = {app: set().union(*(per_target[t] for t, _ in targets)) for app, targets in APPS}

    # The counts, in a sentence and on stderr
    app_counts = []
    for app, targets in APPS:
        n = len(per_app[app])
        if len(targets) > 1:
            parts = ' and '.join(f'{len(per_target[t])} for {label}' for t, label in targets)
            same = len({frozenset(per_target[t]) for t, _ in targets}) == 1
            app_counts.append(f'{n} in the {app} app ({parts}{", the same set" if same else ""})')
        else:
            app_counts.append(f'{n} in the {app} app')
    say(f'{len(crates)} crates in all; ' + '; '.join(
        f'{app} {len(per_app[app])}' + (' (' + ', '.join(f'{t} {len(per_target[t])}' for t, _ in targets) + ')'
                                        if len(targets) > 1 else '')
        for app, targets in APPS))

    out = []
    w = out.append

    w('# What ZyperOS Installer is built from')
    w('')
    w(f'`{SCRIPT}` makes this file from the build\'s own records: the crates from '
      '`src-tauri/Cargo.lock` through cargo, and the fonts from their files. Do not edit it by hand. '
      f'After a dependency changes, run `python3 {SCRIPT}` in this folder and commit the file; '
      '`tools/release.sh` stops when it is out of date.')
    w('')

    # 1. The installer's own code
    text = read(REPO / 'LICENSE')
    if 'GNU GENERAL PUBLIC LICENSE' not in text or 'Version 3, 29 June 2007' not in text:
        fail('the repository LICENSE is not the GNU GPL version 3: the list says it is')
    licence = 'GNU General Public License, version 3'
    notices = [l.strip() for l in read(REPO / 'NOTICE').splitlines() if l.startswith('Copyright (C)')]
    mit = [l.strip() for l in read(REPO / 'LICENSES' / 'MIT-TortOS-Installer.txt').splitlines()
           if l.startswith('Copyright')]
    same = (INSTALLER / 'LICENSE').is_file() and read(INSTALLER / 'LICENSE') == read(REPO / 'LICENSE')
    w("## ZyperOS Installer's own code")
    w('')
    w(f'ZyperOS Installer is under the {licence}, in the repository\'s [`LICENSE`](../LICENSE)'
      + (' (this folder\'s [`LICENSE`](LICENSE) is the same text)' if same else '')
      + ', with the notice and the additional terms under section 7 in its [`NOTICE`](../NOTICE), and the name and Zyper kept as trademarks in [`TRADEMARK.md`](../TRADEMARK.md):')
    w('')
    for l in notices:
        w(f'> {cell(l)}')
    w('')
    w('It started from TortOS Installer by Eric Reinsmidt, in its MIT-licensed versions. That code keeps '
      'his MIT notice' + (f', {cell(mit[0])},' if mit else '')
      + ' in [`LICENSES/MIT-TortOS-Installer.txt`](../LICENSES/MIT-TortOS-Installer.txt).')
    w('')

    # 2. What it is built on besides crates
    w('## What it is built on, besides crates')
    w('')
    tauri = [k for k in crates if k[0] == 'tauri']
    if not tauri:
        fail('no tauri crate in the build: the list says it is built with Tauri')
    w('### Tauri')
    w('')
    for k in tauri:
        p = packages[k]
        w(f'The apps are made with Tauri {cell(k[1])} (the `tauri` crate, {cell(p.get("license") or "no license in its metadata")}'
          + (f', {url_cell(p["repository"])}' if p.get('repository') else '') + '). '
          'Its crates are in the list of crates below, with the rest.')
    w('')
    w("### The system's web view")
    w('')
    w("Tauri draws the window with the web view each system provides. It is not shipped with the apps.")
    w('')
    w(table(['App', 'Web view'], [[a, v] for a, v in WEB_VIEWS]))
    w('')
    # webview2-com-sys links Microsoft's loader into an MSVC build itself
    loader = [k for k in crates if k[0] == 'webview2-com-sys' and k in per_app['Windows']]
    if loader:
        w("The Windows app does carry Microsoft's WebView2 loader, which finds the system's WebView2: "
          '`WebView2LoaderStatic.lib` from Microsoft\'s WebView2 SDK, linked in by the `webview2-com-sys` crate ('
          + ', '.join(cell(k[1]) for k in loader) + "), under Microsoft's license for the SDK.")
        w('')
    w("### Rust's standard library")
    w('')
    w('Rust builds its standard library into every program. It comes with the Rust toolchain, not from '
      '`Cargo.lock`, and is under the MIT or Apache 2.0 license, as Rust is.')
    w('')
    w('### Fonts')
    w('')
    fonts = sorted((f for f in FONTS.iterdir() if f.suffix.lower() in ('.ttf', '.otf')), key=lambda f: f.name)
    texts = sorted((f for f in FONTS.iterdir() if f.suffix.lower() == '.txt'), key=lambda f: f.name)
    rfn = []
    for f in texts:
        rfn += re.findall(r'Reserved Font Names?\s+(“[^”]+”|"[^"]+")', f.read_text(encoding='utf-8'))
    w('The page is set in the fonts in `ui/fonts`. Their names, versions and copyrights are read from the font files'
      + (', and their license is in ' + ', '.join(f'[{code("ui/fonts/" + f.name)}](ui/fonts/{urllib.parse.quote(f.name)})'
                                                  for f in texts) if texts else '')
      + (', with the Reserved Font Name ' + ', '.join(sorted(set(rfn))) if rfn else '') + '.')
    w('')
    w(table(['File', 'Font', 'Version', 'Copyright', 'License'], [font_row(f) for f in fonts]))
    w('')

    # 3 and 4. The crates and how many there are
    w('## Crates')
    w('')
    which = '; '.join(
        f'the {app} app is ' + ('one universal build of ' if len(ts) > 1 else '')
        + ' and '.join(f'`{t}`' + (f' ({label})' if label else '') for t, label in ts)
        for app, ts in APPS)
    w('Every Rust crate compiled into the apps, one row for each name and version: what cargo resolves as normal '
      'dependencies for each app\'s targets, procedural macros and what they use among them (they write code into '
      'the apps as they build). Crates only build scripts or tests use are not in it, nor is the installer\'s own '
      f'crate. The targets: {which}.')
    w('')
    w(f'**{len(crates)} crates in all**: ' + ', '.join(app_counts[:-1]) + ' and ' + app_counts[-1] + '.')
    w('')
    rows = []
    for k in crates:
        p = packages[k]
        repo, pointer = p.get('repository'), 'its repository'
        if repo:
            where = url_cell(repo)
        else:
            repo, pointer = elsewhere(p)
            where = 'none in its metadata' + (f'; {url_cell(repo)}' if repo else '')
        lic = p.get('license')
        if lic:
            lic = cell(lic)
        else:
            lf = (p.get('license_file') or '').replace('\\', '/').rsplit('/', 1)[-1]
            lic = 'not in its metadata' + (f', which names its {code(lf)} file' if lf else '') + (f'; see {pointer}' if repo else '')
        marks = []
        for app, targets in APPS:
            has = [label for t, label in targets if k in per_target[t]]
            marks.append('✓' if len(has) == len(targets) else ' and '.join(has) + ' only' if has else '')
        rows.append([code(k[0]), cell(k[1]), lic, where] + marks)
    w(table(['Crate', 'Version', 'License', 'Repository'] + [a for a, _ in APPS], rows,
            ['---'] * 4 + [':-:'] * len(APPS)))
    return '\n'.join(out) + '\n'


def main():
    ap = argparse.ArgumentParser(description='Makes installer/THIRD-PARTY.md from the build.')
    ap.add_argument('-o', '--output', default=str(OUT), help='where to write it (- for stdout)')
    args = ap.parse_args()
    data = build().encode('utf-8')
    if args.output == '-':
        sys.stdout.buffer.write(data)
    else:
        Path(args.output).write_bytes(data)
        say(f'wrote {args.output}')


if __name__ == '__main__':
    main()
