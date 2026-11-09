#!/usr/bin/env python3
"""The tour video's footage: the host launcher's own frames, as video clips.

make native
python3 video/footage/takes.py [TAKE ...]

A "live" take runs build-native/zyperos on the card video/footage/card.py
builds, headless, with video/rec/rec.dylib loaded: a script of button presses
drives it, and every frame it draws goes into video/public/footage/TAKE.mp4 at
60 frames a second, on the launcher's own clock (video/rec/rec.c says how).

A "shots" take is the launcher's own --shot pictures, for what the host cannot
reach by pressing buttons (a game's in-game menu, Now Playing, the charging
screen, the first start): a screen's motion where the shot takes a time (the
first start's hello, the charging screen, a song name sliding), a frame for
each sixtieth of a second, and otherwise one picture held, as a cut from one
row to the next. They go in the same kind of clip.

TAKE.json beside each clip has its length and, for a live take, its presses.
Nothing here changes the launcher, and no window opens.
"""
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.path.insert(0, str(HERE))
import card as cardlib  # noqa: E402

BINARY = ROOT / 'build-native/zyperos'
REC = ROOT / 'video/rec/rec.dylib'
OUT = ROOT / 'video/public/footage'
WORK = Path(os.environ.get('VIDEO_WORK', Path(tempfile.gettempdir()) / 'zyperos-video'))
FPS = 60
# The corner on the host: no radio, so no Wi-Fi mark, in the shots too
STATUS = 'batt=82'
SHOT_STATUS = '10:08 24h batt=82'
CLOCK = '10:08'
MGS = 'Metal Gear Solid (USA).chd'

TAKES = {}


def keys(steps, start=700, gap=420):
    """Presses one after another: a step is a key name, (key, hold ms), or a
    pause in ms (a number)"""
    out, t = [], start
    for s in steps:
        if isinstance(s, (int, float)):
            t += s
            continue
        k, hold = (s if isinstance(s, tuple) else (s, 90))
        out.append((t, k, hold))
        t += gap if hold <= 90 else hold + 200
    return out, t


# Zyper names the menu music's song on the home as a take starts; the presses
# wait until his pop-up has gone, but on the home take, which shows it
LEAD = 2600


def live(name, last, steps, tail=900, settings=None, start=LEAD, gap=420):
    script, t = keys(steps, start=start, gap=gap)
    TAKES[name] = dict(kind='live', last=last, settings=settings or {}, script=script, end=t + tail)


def shots(name, segments, card='demo', env=None):
    """segments: (last, args, hold ms) a still, or (last, args with {t}, [ms, ...]) a motion"""
    TAKES[name] = dict(kind='shots', card=card, segments=segments, env=env or {})


def motion(t0, t1):
    """The times of a motion from t0 to t1 ms, a sixtieth of a second apart"""
    return [round(t0 + i * 1000 / FPS) for i in range(int((t1 - t0) * FPS / 1000) + 1)]


PS1 = ('PS1', MGS)

# ---- live ---------------------------------------------------------------
# The home moving between systems, with the consoles' own animation
live('home', PS1, start=700, steps=[1500, 'left', 800, 'left', 800, 'right', 700, 'right', 700, 'right', 800, 'right', 1400])
# The same home as a Gallery
live('gallery', PS1, [1100, 'right', 700, 'right', 700, 'right', 1200], settings={'home': 'gallery'})
# The PlayStation shelf: in, along the covers and back
live('shelf', ('PS1', 'Crash Bandicoot (USA).chd'),
     [1000, 'a', 1400, 'right', 750, 'right', 750, 'right', 1100, 'left', 700, 'left', 1400])
# MENU > Search Games: "son", three Sonics, and the second one started
live('search', PS1,
     [800, 'menu', 700, 'a', 900, 'down', 'right', 'a', 'up'] + ['right'] * 7 +
     ['a', 'down', 'down', 'left', 'left', 'left', 'a', 700, 'start', 1300, 'down', 450, 'down', 700, 'a', 1800],
     gap=240)
# X on Metal Gear Solid: its details
live('details', PS1, [800, 'a', 1100, 'x', 2200, 'down', 600, 'down', 600, 'down', 600, 'down', 600, 'down', 1500, 'b', 1000])
# MENU > Appearance to Dark, then the home and a shelf in the dark
live('dark', PS1,
     [800, 'menu', 600] + ['down'] * 16 + [700, 'a', 1500, 'b', 1200, 'right', 900, 'left', 900, 'a', 1300,
                                         'right', 750, 'right', 1400], gap=150, settings={'appearance': 'light'})
# MENU > Language: 日本語, and the menu in it
live('language', PS1,
     [800, 'menu', 600] + ['down'] * 11 + [700, 'a', 1100] + ['down', 300] * 6 + [700, 'a', 2400, 'b', 1200],
     gap=170, settings={'language': 'en'})

# ---- shots ----------------------------------------------------------------
GEN = ('MD', 'Sonic The Hedgehog 2 (USA).md')
SONIC_SNAP = '{art}/Sega_-_Mega_Drive_-_Genesis/Named_Snaps/Sonic The Hedgehog 2 (World).png'
# The in-game menu over Sonic 2, a row at a time, then its Controls and Hotkeys
shots('ingame', [(GEN, ['--screen', 'games', '--gamemenu', str(r), '--backdrop', SONIC_SNAP], 330) for r in range(6)] +
      [(GEN, ['--screen', 'games', '--gamecontrols', '0', 'MD', '--backdrop', SONIC_SNAP], 1700),
       (GEN, ['--screen', 'games', '--gamecontrols', '2', 'MD', '--backdrop', SONIC_SNAP], 1700)])
# PlayStation's buttons drawn as their shapes
shots('controls-ps1', [(PS1, ['--screen', 'games', '--gamecontrols', '0', 'PS1',
                              '--backdrop', '{card}/media-art/c6.png'], 2200)])
# Cover and Video: what the sources offer, focus moving, one looked at closely
shots('media', [(PS1, ['--screen', 'games', '--media', 'page', str(k), '--media-art', '{card}/media-art'], 420)
                for k in range(0, 5)] +
      [(PS1, ['--screen', 'games', '--media', 'look', '4', '--media-art', '{card}/media-art'], 1500)])
# MENU > Storage, its PlayStation page and its Music page
shots('storage', [(PS1, ['--storage', 'main'], 2200), (PS1, ['--storage', 'system'], 2000),
                  (PS1, ['--storage', 'music'], 1800)])
# Achievements: the list, then an unlock with Zyper
shots('cheevos', [(PS1, ['--cheevos-screen', str(r), '--cheevos-badge', '{card}/Roms/PlayStation/.cheevos/Hagane.set', 'b#'], 330)
                  for r in range(4)] +
      [(PS1, ['--notice-kind', 'unlock', '--notice', 'Violated Heavens', 'Unlocked · 10 points',
              '--notice-badge', '{card}/Roms/PlayStation/.cheevos/badges/b3.png'], 4700)])
# MP4: albums, the film and the book on one shelf; Now Playing; a video's controls
shots('mp4', [(('MUSE', 'Videos'), ['--screen', 'games'], 2500),
              (PS1, ['--nowplaying', '0', '0', '{t}'], [f'{83 + t / 1000:.3f}' for t in motion(0, 3500)])])
# Ports: PortMaster offered, installing, and a port paused under the in-game menu
shots('ports', [(('PRT', 'PortMaster.sh'), ['--screen', 'games'], 2000),
                (PS1, ['--portmaster', 'running'], 2000)])
shots('portmenu', [(('PRT', 'Star Farm.sh'), ['--screen', 'games', '--gamemenu', '0', 'port'], 2000),
                   (('PRT', 'Star Farm.sh'), ['--screen', 'games', '--gamecontrols', '0', 'PRT'], 2000)],
      card='portdocs')
# The first start: Zyper says hello in each language
shots('firststart', [(PS1, ['--tour', 'lang', '{t}'], motion(400, 7000)),
                     (PS1, ['--tour', 'welcome', '{t}'], motion(0, 2200))])
# The charging screen of a handheld that is off, and MENU > Battery Icon's Time Left
shots('battery', [(PS1, ['--charging', '{t}'], motion(0, 3000)),
                  (PS1, ['--menu', 'Battery Icon', '--status', '10:08 24h batt=62 eta=205 icon=time'], 3000)])
# A song's name too long for the top bar, sliding
shots('topbar', [(PS1, ['--home', 'spotlight', '--song', 'Night Drive (Extended Mix) - The Midnight Rider Collective',
                        '--phase', '{t}'], motion(0, 4200))])
# Zyper cheering the first games on an empty card
shots('cheer', [(PS1, ['--cheer', '{t}'], motion(0, 2600))])
# The empty card: the two ways to add games
shots('empty', [(('', ''), ['--ways', 'usb'], 1600)], card='empty')

# ---- v2: quicker takes and the features v1 left out ------------------------
# The PlayStation shelf flown along, then N64's and SNES's (the opening)
live('fast-shelf', ('PS1', 'Castlevania - Symphony of the Night (USA).chd'),
     ['a', 700] + ['right'] * 5 + [500] + ['left'] * 5 + [400, 'b', 500, 'left', 600, 'a', 700] + ['right'] * 3 +
     [400, 'b', 500, 'right', 300, 'right', 600, 'a', 700] + ['right'] * 3 + [600], gap=260)
# The Scraper fetching covers: its states (src/main.c, --scraper)
shots('scraper', [(PS1, ['--scraper', str(k)], 600) for k in range(6)] +
      [(PS1, ['--art', 'Super Metroid (USA)'], 1400)])
# SELECT+R1: the quick save's pop-up over the game, and the load
shots('quicksave', [(GEN, ['--screen', 'games', '--popup', 'saved', '--backdrop', SONIC_SNAP], 1400),
                    (GEN, ['--screen', 'games', '--popup', 'loaded', '--backdrop', SONIC_SNAP], 1400)])
# File Transfer: over Wi-Fi, a computer on the cable, and over the cable
shots('wifi-xfer', [(PS1, ['--hare', 'http://192.168.1.42', '1 connected', 'Tobu Tobu Girl.gb'], 5200)])
shots('cable', [(PS1, ['--usb-ask', '0'], 900), (PS1, ['--usb-ask', '1'], 900),
                (PS1, ['--hare', 'http://10.42.0.1', '1 connected', 'Super Metroid (USA).sfc'], 2200)])
shots('hotspot', [(PS1, ['--hotspot', 'ZyperOS-7A2B', 'q7mx-3kpa', '1 joined', 'waiting'], 2800)])
# Files still moving while a game plays: the ring in the in-game menu's corner, over Sonic 2
shots('xfer-ring', [(GEN, ['--screen', 'games', '--gamemenu', '0', '--backdrop', SONIC_SNAP,
                           '--status', f'10:08 24h batt=82 xfer={p}'], 80) for p in range(30, 92, 2)])
# System Update
shots('update', [(PS1, ['--update'], 2400)])
# Battery: held at the charge limit, charging toward it, MENU > Charge Limit
shots('charge', [(PS1, ['--charging', '{t}', 'kept'], motion(0, 1600)),
                 (PS1, ['--charging', '{t}', 'limit'], motion(0, 1200))])
# MP4's lock screen
shots('lock', [(PS1, ['--nowplaying', '0', '0', '83', '--locked'], 1800)])
# Nintendo DS: the in-game menu's screen rows, and N64's shelf
shots('ds', [(('NDS', 'Mario Kart DS (USA).nds'), ['--screen', 'games', '--gamemenu', str(r), 'dual',
                                                   '--backdrop', '{art}/Nintendo_-_Nintendo_DS/Named_Snaps/Mario Kart DS (USA) (En,Fr,De,Es,It).png'], 500)
             for r in (8, 9, 10)] +
     [(('NDS', 'Mario Kart DS (USA).nds'), ['--screen', 'games', '--popup', 'dslang',
                                           '--backdrop', '{art}/Nintendo_-_Nintendo_DS/Named_Snaps/Mario Kart DS (USA) (En,Fr,De,Es,It).png'], 1400)])
shots('n64', [(('N64', 'Super Mario 64 (USA).z64'), ['--screen', 'games'], 1500)])
# Light and dark, the same home; MENU in each of the eight languages
shots('home-light', [(PS1, ['--home', 'spotlight', '--phase', '3200'], 100)])
shots('home-dark', [(PS1, ['--home', 'spotlight', '--phase', '3200', '--theme', 'dark'], 100)])
shots('shelf-dark', [(PS1, ['--screen', 'games', '--theme', 'dark'], 100)])
# v3: the power-off animation, and a video snap: the bar under the resting
# cover fills (DESIGN.md 11.19), then the cover turns into the snap. The host
# has no Muse to decode it, so rec/snapsub.c gives --snap's turn a frame of
# Spyro's snap (its libretro screenshot) in place of the test picture.
shots('poweroff', [(PS1, ['--screen', 'games', '--poweroff', '--shot-at', '{t}'], motion(0, 1600))])
SPYRO = ('PS1', 'Spyro the Dragon (USA).chd')
shots('snap', [(SPYRO, ['--screen', 'games', '--snap-wait', '{t}'], [f'{t / 1000:.4f}' for t in motion(0, 1000)]),
               (SPYRO, ['--screen', 'games', '--snap', '{t}'], [f'{t / 300:.4f}' for t in motion(0, 300)]),
               (SPYRO, ['--screen', 'games', '--snap', '1'], 2700)],
      card='snaps', env={'SNAP_I420': '{work}/spyro.yuv', 'DYLD_INSERT_LIBRARIES': str(ROOT / 'video/rec/snapsub.dylib')})
for code in ['en', 'zh-Hans', 'es', 'pt-BR', 'fr', 'de', 'ja', 'it']:
    shots(f'lang-{code}', [(PS1, ['--lang', code, '--menu', 'Language'], 100)])


# ---- running them --------------------------------------------------------------
def write_last(card, tag, game):
    (card / 'ZyperOS/.last').write_text(f'{tag}\n{game}\n')


def run_live(name, spec, card):
    write_last(card, *spec['last'])
    cardlib.device_settings(card, BINARY, **{'tour.done': 1, 'clock': '24h', 'home': 'spotlight',
                                            'appearance': 'light', 'language': 'en', **spec['settings']})
    script = WORK / f'{name}.keys'
    lines = [f'{t} {k} {h}' for t, k, h in spec['script']] + [f"{spec['end']} end"]
    script.write_text('# made by video/footage/takes.py\n' + '\n'.join(lines) + '\n')
    (WORK / 'batt').write_text('82\n')
    env = cardlib.env_for(card)
    env.update(REC_OUT=str(OUT / f'{name}.mp4'), REC_SCRIPT=str(script), REC_CLOCK=CLOCK,
               REC_BATT=str(WORK / 'batt'), REC_MENU_MUSIC='1', DYLD_INSERT_LIBRARIES=str(REC))
    log = WORK / f'{name}.log'
    with open(log, 'w') as f:
        r = subprocess.run([str(BINARY), '--status', STATUS], cwd=ROOT, env=env,
                           stdout=f, stderr=subprocess.STDOUT, timeout=600)
    if r.returncode:
        raise RuntimeError(f'{name}: exit {r.returncode}, see {log}')
    (OUT / f'{name}.json').write_text(json.dumps({'fps': FPS, 'ms': spec['end'],
                                                  'presses': spec['script']}) + '\n')


def run_shots(name, spec, cards):
    card = cards[spec['card']]
    # A live take before may have left another language or theme on the card
    cardlib.device_settings(card, BINARY, **{'language': 'en', 'appearance': 'light', 'home': 'spotlight'})
    frames = WORK / name
    if frames.exists():
        shutil.rmtree(frames)
    frames.mkdir(parents=True)
    listing, total, n = [], 0, 0
    env = cardlib.env_for(card)
    env.update({k: v.replace('{work}', str(WORK)) for k, v in spec['env'].items()})
    for last, args, when in spec['segments']:
        write_last(card, *last)
        times = when if isinstance(when, list) else [None]
        for t in times:
            a = [x.replace('{t}', str(t)).replace('{card}', str(card)).replace('{art}', str(cardlib.ART_CACHE))
                 for x in args]
            status = [] if '--status' in a or '--tour' in a or '--charging' in a else ['--status', SHOT_STATUS]
            png = frames / f'{n:05d}.png'
            theme = [] if '--theme' in a else ['--theme', 'light']
            r = subprocess.run([str(BINARY), '--shot', str(png), *theme, '--fn-volume', *status, *a],
                               cwd=ROOT, env=env, capture_output=True, text=True)
            if not png.is_file():
                raise RuntimeError(f'{name}: {a}: {r.stderr[-800:]}')
            ms = 1000 / FPS if t is not None else when
            listing.append((png, ms))
            total += ms
            n += 1
    # The concat demuxer's durations, the last picture given twice so its own counts
    lst = frames / 'list.txt'
    lst.write_text(''.join(f"file '{p}'\nduration {ms / 1000:.6f}\n" for p, ms in listing) +
                   f"file '{listing[-1][0]}'\n")
    subprocess.run(['ffmpeg', '-loglevel', 'error', '-y', '-f', 'concat', '-safe', '0', '-i', str(lst),
                    '-vf', f'fps={FPS},scale=out_color_matrix=bt709:out_range=tv', '-c:v', 'libx264', '-preset', 'fast',
                    '-crf', '10', '-pix_fmt', 'yuv420p', '-color_range', 'tv', '-colorspace', 'bt709',
                    '-color_primaries', 'bt709', '-color_trc', 'bt709', '-movflags', '+faststart',
                    str(OUT / f'{name}.mp4')], check=True)
    (OUT / f'{name}.json').write_text(json.dumps({'fps': FPS, 'ms': round(total)}) + '\n')
    # A take of one picture is used as a picture too
    if n == 1:
        shutil.copyfile(frames / '00000.png', OUT / f'{name}.png')
    shutil.rmtree(frames)


def build_rec():
    flags = subprocess.check_output(['pkg-config', '--cflags', '--libs', 'sdl2'], text=True).split()
    for name in ('rec', 'snapsub'):
        src, lib = REC.parent / f'{name}.c', REC.parent / f'{name}.dylib'
        if not lib.is_file() or lib.stat().st_mtime < src.stat().st_mtime:
            subprocess.run(['cc', '-dynamiclib', '-O2', '-Wall', '-Wextra', '-o', str(lib), str(src), *flags], check=True)


def main():
    if not BINARY.is_file():
        sys.exit('run make native first')
    build_rec()
    OUT.mkdir(parents=True, exist_ok=True)
    WORK.mkdir(parents=True, exist_ok=True)
    only = sys.argv[1:]
    unknown = [t for t in only if t not in TAKES]
    if unknown:
        sys.exit(f'no take called {", ".join(unknown)}; there are {", ".join(TAKES)}')
    cards = {'demo': cardlib.build(WORK / 'card')}
    cardlib.device_settings(cards['demo'], BINARY, **{'tour.done': 1, 'clock': '24h'})
    # The demonstration card with Spyro's snap in its .media, for the play
    # mark and the bar, and that snap as the picture snapsub.dylib gives
    cards['snaps'] = cardlib.build(WORK / 'card-snaps')
    cardlib.device_settings(cards['snaps'], BINARY, **{'tour.done': 1, 'clock': '24h'})
    spyro = cardlib.ART_CACHE / 'Sony_-_PlayStation'
    shutil.copyfile(spyro / 'Videos/Spyro the Dragon (USA).mp4',
                    cards['snaps'] / 'Roms/PlayStation/.media/Spyro the Dragon (USA).mp4')
    subprocess.run(['ffmpeg', '-loglevel', 'error', '-y', '-i', str(spyro / 'Named_Snaps/Spyro the Dragon (USA).png'),
                    '-vf', 'scale=320:240', '-pix_fmt', 'yuv420p', '-f', 'rawvideo', str(WORK / 'spyro.yuv')], check=True)
    cards['portdocs'] = WORK / 'card-portdocs'
    cards['empty'] = WORK / 'card-empty'
    for extra, make in (('portdocs', cardlib.rs.portdocs_card), ('empty', cardlib.rs.card_dirs)):
        if cards[extra].exists():
            shutil.rmtree(cards[extra])
        make(cards[extra])
        cardlib.device_settings(cards[extra], BINARY, **{'tour.done': 1, 'clock': '24h'})
    for name, spec in TAKES.items():
        if only and name not in only:
            continue
        if spec['kind'] == 'live':
            run_live(name, spec, cards['demo'])
        else:
            run_shots(name, spec, cards)
        print(name, flush=True)
    # Every take's length and presses in one file, for the video
    index = {p.stem: json.loads(p.read_text()) for p in sorted(OUT.glob('*.json')) if p.stem != 'index'}
    (OUT / 'index.json').write_text(json.dumps(index) + '\n')


if __name__ == '__main__':
    main()
