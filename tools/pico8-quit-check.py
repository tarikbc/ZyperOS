#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""check-pico8-quit: MENU, then X over your PICO-8 ends it at once, as Quit
ends a game in Diatom (1.1.0-rc.8: "menu and then X on the pico 8 has to be
done twice for it to close pico 8").

sd/pixel2/PICO-8.sh execs pico8_64, so PICO-8 is its own group's leader and
the only process in it. launch.sh's port_end TERMs a port's processes but its
script at once and the whole group 2 s later, giving a script the time to
clean up after its game; with no other process in the group, PICO-8 got its
TERM only those 2 s later, and the cart went on meanwhile. A second MENU in
that time opened a second menu, which the late TERM then ended under it
(the 2026-11-19 card's log: "Quit in the in-game menu", then "MENU, the
in-game menu over pid 634", then "port: ended, 0").

launch.sh's run_port, on a copy with the card and /tmp in a scratch folder
and stand-ins for the launcher, portkeys, the program and killall (as
check-pico8-display does), and portkeys's stand-in sending what the menu's
Quit sends (USR2) once the program runs:
  - a script that execs its program (PICO-8.sh, OpenBOR.sh): the program's
    TERM well inside a second of the USR2, never the 2 s after; it ends by
    its own TERM (status 0, no KILL), and the shelf comes back;
  - the same for POWER (USR1), and the handheld powers off;
  - a script that runs its program as a child and goes on after it (a
    PortMaster port's): its program's TERM at once as before, and the script
    left to go on after it (its line after the game is in the log).
port_end reads /proc, which only Linux has: elsewhere this says it skipped,
and --docker runs it in tools/p8fx-gl.Dockerfile's Ubuntu 24.04.
"""
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
IMAGE = 'zyperos-p8fx-gl'
# Well inside the 2 s the group's TERM waits, and far over the device's
# scan of /proc (port_procs) on a busy runner
SOON = 1.0
fails = []


def check(ok, what):
    print(f'  {"ok  " if ok else "FAIL"} {what}')
    if not ok:
        fails.append(what)


def docker():
    if not shutil.which('docker'):
        print('skipped: no Docker for --docker')
        return 0
    if subprocess.run(['docker', 'image', 'inspect', IMAGE], capture_output=True).returncode:
        print(f'pico8-quit: building {IMAGE} (tools/p8fx-gl.Dockerfile, once)')
        if subprocess.run(['docker', 'build', '-q', '-t', IMAGE, '-f',
                           str(ROOT / 'tools/p8fx-gl.Dockerfile'), str(ROOT / 'tools')]).returncode:
            print(f'skipped: could not build the {IMAGE} image')
            return 0
    return subprocess.run(['docker', 'run', '--rm', '-v', f'{ROOT}:/z:ro', '-w', '/z', IMAGE,
                           'python3', 'tools/pico8-quit-check.py']).returncode


def put(path, text, run=True):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text)
    if run:
        path.chmod(0o755)


# The program: a stand-in pico8_64 (or OpenBOR, or a port's game) that runs
# until it is TERMed, says when, and ends as SDL's quit has it end: at once,
# status 0
PROGRAM = '''#!/usr/bin/env python3
import signal, sys, time
calls = sys.argv[1]
def term(*_):
    with open(calls, 'a') as f:
        f.write('program TERM %.3f\\n' % time.time())
    sys.exit(0)
signal.signal(signal.SIGTERM, term)
with open(calls, 'a') as f:
    f.write('program start %.3f\\n' % time.time())
t0 = time.time()
while time.time() - t0 < 15:
    time.sleep(0.02)
'''


def run(tmp, script, signal_name):
    """launch.sh with one port request for `script` (the body of the port's
    script, run by bash as launch.sh runs it); portkeys's stand-in sends
    `signal_name` once the program runs. The stand-ins' lines, and the log"""
    d = tmp / 'sh'
    card, bin_ = d / 'card', d / 'bin'
    calls = d / 'calls'
    zy = card / 'ZyperOS'
    tab = '\t'

    def edit(src):
        text = src.read_text()
        for pat in ('/mnt/SDCARD', '/tmp/', 'exec setsid /bin/bash', 'port_end()'):
            if src.name == 'launch.sh' and pat not in text:
                check(False, f'launch.sh has {pat!r}')
        # /tmp/ first: the scratch folder is itself under /tmp on Linux, and
        # the card's path put in first would be moved again
        return (text.replace('/tmp/', f'{d}/tmp/').replace('/mnt/SDCARD', str(card))
                .replace('/usr/share/zyperos/card', str(d / 'seed')).replace('/dev/kmsg', str(d / 'kmsg')))

    shutil.rmtree(d, ignore_errors=True)
    for p in (zy, bin_, d / 'tmp', card / '.userdata/pixel2', card / 'Roms/PICO-8'):
        p.mkdir(parents=True, exist_ok=True)
    put(zy / 'launch.sh', edit(ROOT / 'sd/zyperos/launch.sh'), False)
    put(zy / 'device.sh', edit(ROOT / 'sd/pixel2/device.sh') + '\ndevice_radios() { :; }\n', False)
    for c in ('poweroff', 'reboot'):
        put(bin_ / c, f'#!/bin/sh\necho {c} >> "{calls}"\n')
    put(bin_ / 'pidof', '#!/bin/sh\nexit 1\n')
    put(bin_ / 'dmesg', '#!/bin/sh\n:\n')
    put(bin_ / 'killall', f'#!/bin/sh\necho "killall $*" >> "{calls}"\n')
    # setsid, as the device's: the script leads a group of its own, and an
    # exec keeps its pid, so a program it execs leads the group
    put(bin_ / 'setsid', '#!/usr/bin/env python3\nimport os, sys\nos.setsid()\n'
                         'os.execvp(sys.argv[1], sys.argv[1:])\n')
    put(zy / 'diatom', '#!/bin/sh\nexit 0\n')
    request = tab.join((f'{zy}/pico8/PICO-8.sh', f'{card}/Roms/PICO-8/celeste.p8', 'PICO8', 'aspect', 'mp4'))
    put(zy / 'zyperos.elf', f'''#!/bin/sh
case $1 in
--mp4-follow) trap 'exit 0' TERM; while :; do sleep 0.05; done ;;
--port-menu) exit 10 ;;
esac
n=$(cat "{d}/runs" 2> /dev/null || echo 0); n=$((n + 1)); echo $n > "{d}/runs"
echo "launcher $n" >> "{calls}"
if [ $n = 1 ]; then printf '%s\\n' '{request}' > "{d}/tmp/zyperos_port"; else : > "{d}/tmp/zyperos_poweroff"; fi
exit 0
''')
    # portkeys: once the program runs, what the in-game menu's Quit (USR2)
    # or POWER (USR1) sends launch.sh, its parent, and when
    put(zy / 'portkeys', f'''#!/bin/bash
trap 'exit 0' TERM
i=0
while [ $i -lt 200 ] && ! grep -q '^program start ' "{calls}"; do sleep 0.02; i=$((i + 1)); done
sleep 0.3
echo "sent $EPOCHREALTIME" >> "{calls}"
kill -{signal_name} $PPID
while :; do sleep 0.05; done
''')
    put(zy / 'pico8/program', PROGRAM)
    put(zy / 'pico8/PICO-8.sh', '#!/bin/bash\nHERE=${0%/*}\n' + script.replace('CALLS', str(calls)))
    env = dict(os.environ, PATH=f'{bin_}:{os.environ["PATH"]}')
    try:
        subprocess.run(['bash', str(zy / 'launch.sh')], env=env, stdin=subprocess.DEVNULL,
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=40)
    except subprocess.TimeoutExpired:
        check(False, 'launch.sh ended (it ran 40 s)')
    lines = calls.read_text().splitlines() if calls.exists() else []
    log = ''
    for f in (card / '.userdata/pixel2').rglob('*.log*'):
        log += f.read_text(errors='replace')
    return lines, log


def when(lines, word):
    for l in lines:
        if l.startswith(word + ' '):
            return float(l.split()[-1])
    return None


def main():
    if '--docker' in sys.argv[1:]:
        return docker()
    if not Path('/proc/self/stat').exists():
        print('skipped: launch.sh reads /proc, which this computer has not (--docker runs it on Linux)')
        return 0
    with tempfile.TemporaryDirectory() as t:
        tmp = Path(t)
        # PICO-8.sh's own way: exec, PICO-8 the group's leader
        execs = 'exec "$HERE/program" CALLS\n'
        for sig, what in (('USR2', 'Quit in the in-game menu'), ('USR1', 'POWER')):
            print(f'launch.sh, {what} over a program its script exec\'d (PICO-8.sh, OpenBOR.sh):')
            lines, log = run(tmp, execs, sig)
            sent, term = when(lines, 'sent'), when(lines, 'program TERM')
            check(sent is not None and term is not None and term - sent < SOON,
                  f'the program\'s TERM within {SOON:.0f} s, not 2 s later '
                  f'({"none" if term is None or sent is None else "%.2f s" % (term - sent)})')
            check('port: ended, 0' in log and 'killing it' not in log,
                  'it ends by its own TERM (status 0), never killed')
            if sig == 'USR2':
                check('launcher 2' in lines and 'poweroff' in lines[lines.index('launcher 2'):],
                      'the shelf comes back after it')
            else:
                check('port: powering off' in log and 'poweroff' in lines, 'the handheld powers off')
        # A PortMaster port's way: its game a child, and lines after it
        print('launch.sh, Quit over a program its script runs as a child and goes on after:')
        lines, log = run(tmp, '"$HERE/program" CALLS\necho "script after $EPOCHREALTIME" >> CALLS\n', 'USR2')
        sent, term, after = when(lines, 'sent'), when(lines, 'program TERM'), when(lines, 'script after')
        check(sent is not None and term is not None and term - sent < SOON,
              'its program\'s TERM at once, as before')
        check(after is not None and sent is not None and after - sent < 2.0,
              'the script goes on after its program, before the group\'s TERM')
        check('port: ended, 0' in log, 'it ends by itself')
    print('check-pico8-quit:', 'FAIL ' + str(len(fails)) if fails else 'ok')
    return 1 if fails else 0


if __name__ == '__main__':
    sys.exit(main())
