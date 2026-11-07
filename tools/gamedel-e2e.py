#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Delete Game through the real launcher, headless (make check-gamedel).

The launcher's --delete-now runs Delete Game's yes on the game under the
cursor, the path the details page runs after its question, and the shot is
the shelf after it. On a scratch card of three PlayStation games and one SNES
game, each with a cover, a save and a state:

  1. the middle game from its own shelf: gone from the card, and the shelf
     stays, on the next game;
  2. the last game on the shelf: on the one before it;
  3. a favorite from Favorites, the last game of its system: gone with its
     favorite, play time and details, its system off the home, and Favorites
     on the game that is left;
  4. the last game on the card: the empty home;

and the saved library a start shows first lists what is left, never a game
that went. Standard library only; skips without SDL2."""
import os
from pathlib import Path
import shutil
import sqlite3
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
BIN = ROOT / 'build-native/zyperos'
D = ROOT / 'build-native/gamedel-e2e'
PS1 = ['Ape Escape (USA)', 'Crash Bandicoot (USA)', 'Spyro the Dragon (USA)']
failures = 0


def check(ok, what):
    global failures
    if not ok:
        failures += 1
        print('  FAIL', what)


def env():
    return dict(os.environ, ZYPEROS_HEADLESS='1', SDL_VIDEODRIVER='dummy', SDL_RENDER_DRIVER='software',
                ZYPEROS_ROOT=str(D / 'root'), ZYPEROS_CARD=str(D), ZYPEROS_ROMS=str(D / 'Roms'),
                ZYPEROS_USERDATA=str(D / 'userdata'), ZYPEROS_SHARED=str(D / 'shared'), ZYPEROS_WEB=str(D),
                ZYPEROS_FONT=str(ROOT / 'res/fonts/menu.ttf'))


def run(*args):
    r = subprocess.run([str(BIN), *args], env=env(), cwd=ROOT, capture_output=True, text=True)
    if r.returncode:
        print(r.stderr[-2000:])
    check(r.returncode == 0, f'{args} exited {r.returncode}')
    return r.stderr


def delete(shelf, file, name):
    """Delete Game on `file`, the cursor put on it through the card's .last;
    what the shot drew: (screen, title)"""
    (D / 'root/.last').write_text(f'{shelf}\n{file}\n')
    log = run('--shot', str(D / f'{name}.png'), '--screen', 'games', '--delete-now', '--theme', 'light')
    check('delete: deleted.' in log, f'{file} was not deleted:\n{log[-1500:]}')
    wrote = [line for line in log.splitlines() if line.startswith('wrote ')]
    check(len(wrote) == 1, f'no shot line for {name}')
    if not wrote:
        return None, None
    tail = wrote[0].split('  ', 1)[1]          # "[games] Ape Escape"
    screen, _, title = tail.partition('] ')
    return screen.strip('['), title


def saved():
    text = (D / 'userdata/saved-library').read_text()
    return [line.split('\t')[1] for line in text.splitlines() if line.startswith('game\t')]


def main():
    if subprocess.run(['pkg-config', '--exists', 'sdl2', 'SDL2_image', 'SDL2_ttf']).returncode:
        print('skipped: no SDL2 here to build the launcher with')
        return 0
    if not BIN.is_file():
        print('FAIL no build-native/zyperos: make native first')
        return 1
    shutil.rmtree(D, ignore_errors=True)
    for d in ('root', 'Roms/PlayStation/.media', 'Roms/SNES', 'userdata', 'shared/.zyperos/PlayStation', 'Saves'):
        (D / d).mkdir(parents=True)
    shutil.copyfile(ROOT / 'config/systems.cfg', D / 'root/systems.cfg')
    for g in PS1:
        (D / f'Roms/PlayStation/{g}.chd').write_text('disc')
        (D / f'Roms/PlayStation/.media/{g}.png').write_text('cover')
        (D / f'Saves/{g}.srm').write_text('save')
        (D / f'shared/.zyperos/PlayStation/{g}.chd.auto.state').write_text('state')
    (D / 'Roms/SNES/Super Metroid (USA).sfc').write_text('cart')
    run('--set', 'tour.done=1')
    lib = sqlite3.connect(D / 'shared/.zyperos/library.db')
    with lib:
        for key in ('fav.PS1\tApe Escape (USA).chd', 'fav.SFC\tSuper Metroid (USA).sfc',
                    'sess.1730000000.5.PS1\tApe Escape (USA).chd', 'sess.1730000000.6.SFC\tSuper Metroid (USA).sfc'):
            lib.execute('INSERT OR REPLACE INTO settings(key, value) VALUES (?, ?)', (key, '60\tquit'))
        lib.execute("INSERT INTO games(folder, file, title) VALUES ('PlayStation', 'Ape Escape (USA).chd', 'Ape Escape')")
    lib.close()

    print('the middle game, from its own shelf:')
    screen, title = delete('PS1', 'Crash Bandicoot (USA).chd', 'middle')
    check(screen == 'games' and title == 'Spyro the Dragon', f'the shelf is on {screen} {title}, not the next game')
    for f in ('Roms/PlayStation/Crash Bandicoot (USA).chd', 'Roms/PlayStation/.media/Crash Bandicoot (USA).png',
              'Saves/Crash Bandicoot (USA).srm', 'shared/.zyperos/PlayStation/Crash Bandicoot (USA).chd.auto.state'):
        check(not (D / f).exists(), f'{f} is still there')
    for g in ('Ape Escape (USA)', 'Spyro the Dragon (USA)'):
        check((D / f'Roms/PlayStation/{g}.chd').exists() and (D / f'Saves/{g}.srm').exists(),
              f'{g} lost a file')
    check('Crash Bandicoot (USA).chd' not in saved(), 'the saved library still lists it')

    print('the last game on its shelf:')
    screen, title = delete('PS1', 'Spyro the Dragon (USA).chd', 'last')
    check(screen == 'games' and title == 'Ape Escape', f'the shelf is on {screen} {title}, not the one before')

    print('a favorite from Favorites, the last of its system:')
    screen, title = delete('FAV', 'Ape Escape (USA).chd', 'favorite')
    check(screen == 'games' and title == 'Super Metroid', f'Favorites is on {screen} {title}')
    lib = sqlite3.connect(D / 'shared/.zyperos/library.db')
    keys = [k for (k,) in lib.execute("SELECT key FROM settings WHERE key LIKE 'fav.%' OR key LIKE 'sess.%'")]
    rows = list(lib.execute("SELECT file FROM games WHERE folder = 'PlayStation'"))
    lib.close()
    check(not any('Ape Escape' in k for k in keys), f'its favorite or play time stayed: {keys}')
    check(any('Super Metroid' in k for k in keys), "the other favorite's rows went")
    check(not rows, f'its details stayed: {rows}')
    check(not any(f.endswith('.chd') for f in saved()), 'the saved library still lists a PlayStation game')
    check(not (D / 'Roms/PlayStation/.media').exists(), 'its .media, empty now, stayed')
    check((D / 'Roms/PlayStation').is_dir(), "the console's own folder went")

    print('the last game on the card:')
    screen, title = delete('SFC', 'Super Metroid (USA).sfc', 'empty')
    check(screen == 'systems', f'the shot is on {screen}, not the home')
    check(not saved(), 'the saved library still lists a game')

    if failures:
        print(f'gamedel-e2e: {failures} failure{"s" if failures != 1 else ""}')
        return 1
    print('gamedel-e2e: ok')
    shutil.rmtree(D, ignore_errors=True)
    return 0


if __name__ == '__main__':
    sys.exit(main())
