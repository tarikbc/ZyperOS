#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""check-recents-played: a program of its own reaches Recents and Played, and
Recents says when each game was last played (DESIGN.md 11.48, 11.55).

1.1.0-rc.8, on the Pixel 2: "The recents page is not considering pico-8
games. Is the play time considering them? Also, the recents page should show
when was it last played". A cart played on your PICO-8 ran with no launcher
beside it, and no session was kept for it, nor for OpenBOR, the standalone
Flycast or a port (src/stats.h, "a program of its own").

1. launch.sh's run_port, on a copy with the card and /tmp in a scratch folder
   and stand-ins for the launcher, the program and portkeys (as
   check-pico8-mp4 does): as the program ends, the launcher is run with
   --port-played before it comes back; with POWER over the program, before
   the power-off, and no launcher after it.
2. The real host launcher (build-native/zyperos) on a card with your PICO-8
   (its two files stand-ins), a cart, Splore, OpenBOR with a module, a port
   and PortMaster, and a SNES game played yesterday at 21:40:
   - A on the cart (ZYPEROS_AUTOLAUNCH) writes the request, the note in
     /tmp and the cart's row in the library, open at 0 s;
   - --port-played, as launch.sh runs it, closes it (the note's monotonic
     start moved back 3725 s, so the session is 1h 2m) and removes the note;
   - the cart is first on Recents; under its name "PICO-8 · today HH:MM" (its
     start, the card's zone, MENU > Clock's 24 hours) and "Played 1h 2m", in
     Covers, Grid, List and Video, both themes, nothing cut or past its box;
     the SNES game, second, "SNES · yesterday 21:40"; on the PICO-8 shelf
     the cart's Played too;
   - a launcher that starts with the note still there (no --port-played)
     closes it at its start, before Recents is built;
   - OpenBOR's module and a port reach Recents the same way, and Splore,
     where its carts are played; PortMaster itself, which installs games and
     is none, writes no note and no row;
   - Recents' Games in its MENU do not count Splore, as the PICO-8 shelf's
     do not.

Shots of Recents in both themes and each View go to
build-native/recents-played-shots/ (ZYPEROS_RECENTS_SHOTS=DIR for another
folder)."""
import os
import re
import shutil
import sqlite3
import subprocess
import sys
import tempfile
import time
from datetime import datetime, timedelta
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BIN = Path(os.environ.get('ZYPEROS_BIN', ROOT / 'build-native/zyperos'))
OUT = Path(os.environ.get('ZYPEROS_RECENTS_SHOTS', ROOT / 'build-native/recents-played-shots'))
# The status corner's clock is set to the card's own time as the check
# starts (launcher), so it reads beside "today HH:MM" as on the handheld
STATUS = '10:08 24h wifi=3 bt batt=82'
ZONE = 'America/New_York'      # the card's zone as it ships (the library's timezone)
SEP = ' \u00b7 '
fails = []


def check(ok, what):
    print(f'  {"ok  " if ok else "FAIL"} {what}')
    if not ok:
        fails.append(what)


def put(path, text, run=False):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text)
    if run:
        path.chmod(0o755)


# ---- 1. launch.sh ---------------------------------------------------------

def launch_sh(tmp):
    d = tmp / 'sh'
    card, bin_ = d / 'card', d / 'bin'
    calls = d / 'calls'
    zy = card / 'ZyperOS'

    def edit(src):
        text = src.read_text()
        for pat in ('/mnt/SDCARD', '/tmp/', '--port-played'):
            if src.name == 'launch.sh' and pat not in text:
                check(False, f'launch.sh has {pat!r}')
        return (text.replace('/tmp/', f'{d}/tmp/').replace('/mnt/SDCARD', str(card))
                .replace('/usr/share/zyperos/card', str(d / 'seed')).replace('/dev/kmsg', str(d / 'kmsg')))

    def run(request, power):
        shutil.rmtree(d, ignore_errors=True)
        for p in (zy, bin_, d / 'tmp', card / '.userdata/pixel2', card / 'Roms/PICO-8'):
            p.mkdir(parents=True, exist_ok=True)
        put(zy / 'launch.sh', edit(ROOT / 'sd/zyperos/launch.sh'))
        put(zy / 'device.sh', edit(ROOT / 'sd/pixel2/device.sh') + '\ndevice_radios() { :; }\n')
        for c in ('poweroff', 'reboot'):
            put(bin_ / c, f'#!/bin/sh\necho {c} >> "{calls}"\n', True)
        put(bin_ / 'pidof', '#!/bin/sh\nexit 1\n', True)
        put(bin_ / 'dmesg', '#!/bin/sh\n:\n', True)
        put(bin_ / 'killall', f'#!/bin/sh\necho "killall $*" >> "{calls}"\n', True)
        # setsid, as the device's: the program leads a group of its own,
        # which POWER's TERM to the group reaches
        put(bin_ / 'setsid', '#!/usr/bin/env python3\nimport os, sys\nos.setsid()\n'
                             'os.execvp(sys.argv[1], sys.argv[1:])\n', True)
        put(zy / 'diatom', '#!/bin/sh\nexit 0\n', True)
        # The launcher: the request on its first run, a power-off on its
        # second; --port-played said; the follower and the menu as stand-ins
        put(zy / 'zyperos.elf', f'''#!/bin/sh
case $1 in
--port-played) echo "port-played" >> "{calls}"; exit 0 ;;
--mp4-follow) trap 'exit 0' TERM; while :; do sleep 0.05; done ;;
--port-menu) exit 10 ;;
--alsa-try) exit 0 ;;
esac
n=$(cat "{d}/runs" 2> /dev/null || echo 0); n=$((n + 1)); echo $n > "{d}/runs"
echo "launcher $n" >> "{calls}"
if [ $n = 1 ]; then printf '%s\\n' '{request}' > "{d}/tmp/zyperos_port"; else : > "{d}/tmp/zyperos_poweroff"; fi
exit 0
''', True)
        # portkeys: POWER over the program (USR1 to launch.sh, as
        # tools/portkeys.c's end_port sends it) once the program has said it
        # started; or nothing
        press = 'kill -USR1 $PPID' if power else ':'
        put(zy / 'portkeys', f'''#!/bin/sh
trap 'exit 0' TERM
i=0
while [ $i -lt 200 ] && ! grep -q '^program ' "{calls}"; do sleep 0.05; i=$((i + 1)); done
{press}
while :; do sleep 0.05; done
''', True)
        # The program: ends by itself at once, or runs until POWER ends it
        put(zy / 'pico8/PICO-8.sh', f'''#!/bin/bash
echo "program $*" >> "{calls}"
trap 'echo "program TERM" >> "{calls}"; exit 0' TERM
if [ {1 if power else 0} = 1 ]; then i=0; while [ $i -lt 400 ]; do sleep 0.05; i=$((i + 1)); done; fi
sleep 0.2
exit 0
''', True)
        env = dict(os.environ, PATH=f'{bin_}:{os.environ["PATH"]}')
        try:
            subprocess.run(['sh', str(zy / 'launch.sh')], env=env, stdin=subprocess.DEVNULL,
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=60)
        except subprocess.TimeoutExpired:
            check(False, 'launch.sh ended (it ran 60 s)')
        return calls.read_text().splitlines() if calls.exists() else []

    tab = '\t'
    request = tab.join((f'{zy}/pico8/PICO-8.sh', f'{card}/Roms/PICO-8/celeste.p8', 'PICO8', 'stretch', 'mp4'))
    print('launch.sh, your PICO-8 ending by itself:')
    lines = run(request, False)
    check(any(l.startswith('program ') for l in lines), 'the program ran')
    check('port-played' in lines and 'launcher 2' in lines and
          lines.index('port-played') < lines.index('launcher 2') and
          lines.index('port-played') > next(i for i, l in enumerate(lines) if l.startswith('program ')),
          f'the launcher closes its play time after it ends, before it comes back ({lines})')
    check(lines.count('port-played') == 1, 'once')

    print('launch.sh, POWER over your PICO-8:')
    lines = run(request, True)
    check('program TERM' in lines, 'POWER ends the program')
    check('port-played' in lines and 'poweroff' in lines and lines.index('port-played') < lines.index('poweroff'),
          f'its play time closed before the power-off ({lines})')
    check('launcher 2' not in lines, 'and no launcher after it')


# ---- 2. the launcher ------------------------------------------------------

GAMES = {'PICO8': ['celeste.p8'],
         'OPENBOR': ['Beats of Rage.pak'],
         'PRT': ['Stardew Valley.sh', 'PortMaster.sh'],
         'SFC': ['Super Metroid (USA).sfc']}


def make_card(base):
    if base.exists():
        shutil.rmtree(base)
    root = base / 'ZyperOS'
    for d in (root, base / 'Roms', base / 'userdata', base / 'shared', base / 'Bios/PICO-8'):
        d.mkdir(parents=True)
    rows, folders = [], {}
    for line in (ROOT / 'config/systems.cfg').read_text().splitlines():
        f = line.split('|')
        if line.startswith('sys|'):
            if f[4] in GAMES:
                rows.append(line)
                folders[f[4]] = f[1]
        elif line.startswith('brings|'):
            rows.append(line)
    (root / 'systems.cfg').write_text('\n'.join(rows) + '\n')
    (root / 'system-icons').symlink_to(ROOT / 'res/system-icons', target_is_directory=True)
    for tag, files in GAMES.items():
        for g in files:
            put(base / 'Roms' / folders[tag] / g, '#!/bin/bash\n' if g.endswith('.sh') else 'game\n')
    # PortMaster on the card (its program and its script), so A starts it
    put(base / 'Roms/Ports/PortMaster/pugwash', '#!/bin/sh\n', True)
    # Your PICO-8: its two files (stand-ins) and the script that runs it;
    # OpenBOR's script; Splore's art
    for f in ('pico8_64', 'pico8.dat'):
        (base / 'Bios/PICO-8' / f).write_bytes(b'stand-in')
    put(root / 'pico8/PICO-8.sh', '#!/bin/sh\n', True)
    put(root / 'openbor/OpenBOR.sh', '#!/bin/sh\n', True)
    for art in ('cover-light.png', 'cover-dark.png', 'icon.png'):
        src = ROOT / 'res/pico8' / art
        if src.exists():
            shutil.copy(src, root / 'pico8' / art)
    flags = base / 'flags'
    flags.mkdir()
    env = os.environ | {'ZYPEROS_HEADLESS': '1', 'SDL_VIDEODRIVER': 'dummy', 'SDL_AUDIODRIVER': 'dummy',
                        'SDL_RENDER_DRIVER': 'software', 'ZYPEROS_ROOT': str(root),
                        'ZYPEROS_CARD': str(base), 'ZYPEROS_ROMS': str(base / 'Roms'),
                        'ZYPEROS_USERDATA': str(base / 'userdata'), 'ZYPEROS_SHARED': str(base / 'shared'),
                        'ZYPEROS_WEB': str(base), 'ZYPEROS_FONT': str(ROOT / 'res/fonts/menu.ttf'),
                        'ZYPEROS_FLAG_DIR': str(flags), 'ZYPEROS_CTL_DIR': str(base / 'ctl')}
    for k in ('ZYPEROS_AUTOLAUNCH', 'ZYPEROS_DESK_SOCK', 'ZYPEROS_DESK_FORCE'):
        env.pop(k, None)
    run(env, '--shot', str(base / 'first.png'), '--screen', 'systems', '--status', STATUS)
    run(env, '--set', 'tour.done=1')
    return base, env, flags


def run(env, *args, extra=None):
    r = subprocess.run([str(BIN), *args], env=env | (extra or {}), cwd=ROOT, capture_output=True, text=True,
                       timeout=90)
    assert r.returncode == 0, (args, r.stderr[-1500:])
    return r.stderr


def lib(base):
    return sqlite3.connect(base / 'shared/.zyperos/library.db')


def sessions(base, tag=None):
    with lib(base) as db:
        got = db.execute("SELECT key, value FROM settings WHERE key LIKE 'sess.%'").fetchall()
    return [(k, v) for k, v in got if tag is None or k.split('\t')[0].split('.', 3)[3] == tag]


def seed(base, when, tag, file, secs):
    with lib(base) as db:
        db.execute('INSERT OR REPLACE INTO settings(key, value) VALUES (?, ?)',
                   (f'sess.{int(when)}.{int(when) % 100000}.{tag}\t{file}', f'{secs}\tquit'))


def zone_now():
    try:
        from zoneinfo import ZoneInfo
        return ZoneInfo(ZONE)
    except Exception:          # no zone data: the check reads the C library's
        return None


def local(t):
    z = zone_now()
    return datetime.fromtimestamp(t, z) if z else datetime.fromtimestamp(t)


def at_local(day, h, m):
    """The unix time of `day`'s h:m on the card's clock"""
    z = zone_now()
    dt = datetime(day.year, day.month, day.day, h, m, tzinfo=z) if z else datetime(day.year, day.month, day.day, h, m)
    return dt.timestamp()


def autolaunch(env, tag, file):
    return run(env, extra={'ZYPEROS_AUTOLAUNCH': f'{tag}\t{file}'})


def note(flags):
    p = flags / 'zyperos_port_played'
    return p.read_text().rstrip('\n').split('\t') if p.exists() else None


def back(note_fields, flags, secs):
    """The note as the launcher wrote it, its monotonic start `secs` earlier:
    a session of that length once it is closed. On a computer up for less
    than that (a CI runner) the start is before the clock's zero, a negative
    number, which the launcher takes as it takes any: the seconds are the
    difference"""
    start, key, mono, tag, file = note_fields
    (flags / 'zyperos_port_played').write_text(f'{start}\t{key}\t{int(mono) - secs * 1000}\t{tag}\t{file}\n')


def drawn(log):
    return [p.split('\n', 1)[0] for p in log.split('fit: drawn: ')[1:]]


def bad_fits(log):
    return [l for l in log.splitlines() if l.startswith('fit: overflow') or l.startswith('i18n: ')]


def to_shelf(env, shelf):
    """The next start on `shelf`: its startup system, and the place a game
    left (.last, and a port's return) gone, which would win over it"""
    run(env, '--set', f'startup_system={shelf}')
    Path(env['ZYPEROS_ROOT'], '.last').unlink(missing_ok=True)
    Path(env['ZYPEROS_FLAG_DIR'], 'zyperos_port_return').unlink(missing_ok=True)


def shot(env, path, shelf, view, theme, *more):
    to_shelf(env, shelf)
    args = ['--shot', str(path), '--screen', 'games', '--phase', '3200', '--status', STATUS, '--theme', theme]
    if view != 'covers':
        args += ['--view', view]
    return run(env, *args, *more)


def launcher(tmp):
    global STATUS
    base, env, flags = make_card(tmp / 'card')
    OUT.mkdir(parents=True, exist_ok=True)
    now = time.time()
    today = local(now)
    STATUS = f'{today:%H:%M} 24h wifi=3 bt batt=82'
    yday = today - timedelta(days=1)
    snes_at = at_local(yday, 21, 40)
    seed(base, snes_at, 'SFC', 'Super Metroid (USA).sfc', 1500)

    print('A on a cart, your PICO-8 its player:')
    log = autolaunch(env, 'PICO8', 'celeste.p8')
    req = (flags / 'zyperos_port').read_text().rstrip('\n').split('\t') if (flags / 'zyperos_port').exists() else []
    check(len(req) >= 4 and req[0].endswith('ZyperOS/pico8/PICO-8.sh') and req[1].endswith('/celeste.p8'),
          f'the request names your PICO-8 and the cart ({req[:2]})')
    n = note(flags)
    check(n is not None and n[3:] == ['PICO8', 'celeste.p8'], f'the note is in /tmp ({n})')
    rows = sessions(base, 'PICO8')
    check(len(rows) == 1 and rows[0][1] == '0\topen', f'its row in the library, open at 0 s ({rows})')
    started = int(n[0]) if n else 0
    check(abs(started - now) < 60, 'from when A was pressed')

    print('the program ends (--port-played, as launch.sh runs it):')
    if n:
        back(n, flags, 3725)
    log = run(env, '--port-played')
    check('port: its play time kept' in log, 'it says so')
    check(note(flags) is None, 'the note is gone')
    rows = sessions(base, 'PICO8')
    secs = int(rows[0][1].split('\t')[0]) if len(rows) == 1 else -1
    check(len(rows) == 1 and rows[0][1].endswith('\tquit') and 3725 <= secs < 3725 + 55,
          f'the same row, 1h 2m, quit ({rows})')
    log = run(env, '--port-played')
    check('no play time to keep' in log and len(sessions(base, 'PICO8')) == 1, 'a second run finds nothing to close')

    print('Recents:')
    clock = local(started).strftime('%H:%M')
    want = f'PICO-8{SEP}today {clock}'
    snes = f'SNES{SEP}yesterday 21:40'
    for theme in ('light', 'dark'):
        for view in ('covers', 'grid', 'list', 'video'):
            log = shot(env, OUT / f'recents-{view}-{theme}.png', 'Recents', view, theme)
            got = drawn(log)
            # The Video view's card says the facts on one line, " · " apart
            said = want in got and 'Played 1h 2m' in got if view != 'video' else \
                any(want in l and 'Played 1h 2m' in l for l in got)
            check('celeste' in got and said,
                  f'{view}, {theme}: the cart focused, "{want}" and "Played 1h 2m" under its name')
            bad = [l for l in bad_fits(log) if 'today' in l or 'Played' in l or 'PICO-8' in l]
            check(not bad, f'{view}, {theme}: nothing of it cut or past its box ({bad[:2]})')
    log = shot(env, OUT / 'recents-snes-light.png', 'Recents', 'covers', 'light', '--game', 'Super Metroid (USA).sfc')
    check(snes in drawn(log) and 'Played 25m 0s' in drawn(log), f'the SNES game: "{snes}", its Played')
    log = shot(env, OUT / 'pico8-shelf-light.png', 'PICO-8', 'covers', 'light', '--game', 'celeste.p8')
    check('Played 1h 2m' in drawn(log), 'the PICO-8 shelf has the cart\'s Played')
    check(not any(SEP + 'today' in l for l in drawn(log)), 'and no "when" there: a console\'s own shelf')
    to_shelf(env, 'Recents')
    log = run(env, '--shot', str(OUT / 'recents-12h-light.png'), '--screen', 'games', '--phase', '3200',
              '--status', f'{today:%H:%M} 12h wifi=3 bt batt=82', '--theme', 'light')
    h12 = local(started).strftime('%I:%M %p').lstrip('0')
    check(f'PICO-8{SEP}today {h12}' in drawn(log), f'MENU > Clock\'s 12 hours: "today {h12}"')

    print('a launcher that starts with the note still there:')
    autolaunch(env, 'PICO8', 'celeste.p8')
    n = note(flags)
    if n:
        back(n, flags, 90)
    for f in ('zyperos_port', 'zyperos_port_return'):
        (flags / f).unlink(missing_ok=True)
    log = run(env, '--shot', str(OUT / 'recents-start-closes.png'), '--screen', 'games', '--status', STATUS)
    check(note(flags) is not None, 'a shot reads the card as it is, and leaves the note')
    log = run(env, extra={'ZYPEROS_AUTOLAUNCH': 'SFC\tno such game'})
    check('its play time kept at the launcher\'s start' in log and note(flags) is None,
          'the launcher closes it at its start')
    rows = sorted(sessions(base, 'PICO8'))
    check(len(rows) == 2 and all(v.endswith('\tquit') for _, v in rows) and
          any(90 <= int(v.split('\t')[0]) < 145 for _, v in rows),
          f'as a quit, not lost: before stats_recover ({rows})')

    print('OpenBOR, a port, Splore and PortMaster:')
    for tag, file, name, sysname in (('OPENBOR', 'Beats of Rage.pak', 'Beats of Rage', 'OpenBOR'),
                                     ('PRT', 'Stardew Valley.sh', 'Stardew Valley', 'Ports'),
                                     ('PICO8', 'Splore.splore', 'Splore', 'PICO-8')):
        if file == 'Splore.splore':
            # Splore asks first on a handheld with Wi-Fi and no network (the
            # host's Brick profile), then hands over as a cart does
            # (launch_port_core): its session as --port-played writes it
            seed(base, time.time(), tag, file, 600)
        else:
            autolaunch(env, tag, file)
            n = note(flags)
            check(n is not None and n[3:] == [tag, file], f'{name}: the note ({n})')
            if n:
                back(n, flags, 600)
            run(env, '--port-played')
        rows = sessions(base, tag)
        check(any(k.endswith('\t' + file) and v.startswith('6') and v.endswith('\tquit') for k, v in rows),
              f'{name}: its session, 10m')
        # Focused by its file: two starts in one second are in name order
        log = shot(env, OUT / f'recents-{name.lower().replace(" ", "-")}-light.png', 'Recents', 'covers', 'light',
                   '--game', file)
        got = drawn(log)
        # 10m and the seconds between the note and --port-played: on a
        # loaded runner a launcher's start takes more than one (CI,
        # 2026-11-25: "Played 10m 1s"), as the cart's 1h 2m allows above
        played = [l for l in got if re.fullmatch(r'Played 10m ([0-9]|[1-4][0-9])s', l)]
        check(name in got and any(l.startswith(f'{sysname}{SEP}today ') for l in got) and played,
              f'{name}: on Recents, "{sysname} · today ..." and "Played 10m 0s" or a few seconds more ({played})')
    before = len(sessions(base))
    autolaunch(env, 'PRT', 'PortMaster.sh')
    req = (flags / 'zyperos_port').read_text() if (flags / 'zyperos_port').exists() else ''
    check('PortMaster.sh' in req, 'PortMaster starts (its request)')
    check(note(flags) is None and len(sessions(base)) == before, 'PortMaster itself: no note, no row')

    print("Recents' MENU, Splore on it:")
    log = shot(env, OUT / 'recents-menu-light.png', 'Recents', 'covers', 'light', '--menu', 'Games')
    got = drawn(log)
    # Splore, the cart, the SNES game, OpenBOR's, the port: four games
    check('4' in got, f'Games: 4, Splore not one ({[g for g in got if g.isdigit()]})')
    return base


def main():
    tmp = Path(tempfile.mkdtemp(prefix='zyperos-recents-played-'))
    try:
        launch_sh(tmp)
        if BIN.is_file():
            launcher(tmp)
        else:
            check(False, 'build-native/zyperos is there (make native)')
    finally:
        if not fails:
            shutil.rmtree(tmp, ignore_errors=True)
    if fails:
        print(f'check-recents-played: {len(fails)} failed (scratch in {tmp})')
        return 1
    print(f'ok: a program of its own reaches Recents and Played, and Recents says when (shots in {OUT})')
    return 0


if __name__ == '__main__':
    sys.exit(main())
