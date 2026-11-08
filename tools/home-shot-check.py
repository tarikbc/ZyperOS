#!/usr/bin/env python3
"""Exercise the real launcher and its persisted Home Screen/Appearance controls."""
import os
import re
from pathlib import Path
import shutil
import subprocess
from PIL import Image, ImageChops

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'build-native/home-shots'
OUT.mkdir(parents=True, exist_ok=True)
BIN = ROOT / 'build-native/zyperos'
STATUS = '10:08 24h wifi=3 bt batt=82'

def fixture(name, tags):
    base = OUT / name
    if base.exists():
        shutil.rmtree(base)
    for d in ('root', 'Roms', 'userdata', 'shared'):
        (base/d).mkdir(parents=True, exist_ok=True)
    rows = []
    for line in (ROOT/'config/systems.cfg').read_text().splitlines():
        if not line.startswith('sys|'):
            continue
        f = line.split('|')
        if f[4] in tags:
            rows.append(line)
            folder = base/'Roms'/f[2]
            folder.mkdir(parents=True, exist_ok=True)
            (folder/('Example.'+f[7].split(',')[0])).touch()
    (base/'root/systems.cfg').write_text('\n'.join(rows)+'\n' if rows else (ROOT/'config/systems.cfg').read_text())
    (base/'root/system-icons').symlink_to(ROOT/'res/system-icons', target_is_directory=True)
    env = os.environ | {'ZYPEROS_HEADLESS':'1', 'SDL_VIDEODRIVER':'dummy',
        'SDL_RENDER_DRIVER':'software','ZYPEROS_ROOT':str(base/'root'),
        'ZYPEROS_CARD':str(base), 'ZYPEROS_ROMS':str(base/'Roms'),
        'ZYPEROS_USERDATA':str(base/'userdata'), 'ZYPEROS_SHARED':str(base/'shared'),
        'ZYPEROS_WEB':str(base), 'ZYPEROS_FONT':str(ROOT/'res/fonts/menu.ttf')}
    return base, env

def run(env, *args):
    result = subprocess.run([str(BIN),*args],env=env,cwd=ROOT,capture_output=True,text=True)
    with (OUT/'run.log').open('a') as log:
        log.write(' '.join(args)+'\n'+result.stdout+result.stderr)
    assert result.returncode == 0, (args,result.stderr[-2000:])
    return result.stdout

def shot(env, name, *args):
    file = OUT/(name+'.png')
    run(env,'--shot',str(file),'--screen','systems','--status',STATUS,*args)
    return Image.open(file).convert('RGBA')

def same(a,b):
    return ImageChops.difference(a,b).convert('RGB').getbbox() is None

(OUT/'run.log').write_text('')
base,env=fixture('many', {'GB','GBA','SFC','N64','PS1'})
run(env,'--set','startup_system=Game Boy Advance')
for layout in ('spotlight','gallery'):
    for theme in ('light','dark'):
        plain=shot(env,f'{layout}-{theme}','--home',layout,'--theme',theme)
        # The footer must preserve the 24-unit right inset.
        margin=plain.crop((1000,696,1024,748))
        assert len(set(margin.getdata()))==1, f'{layout} footer crosses the inset'
        playing=shot(env,f'{layout}-{theme}-playing','--home',layout,'--theme',theme,'--song','Posters')
        paused=shot(env,f'{layout}-{theme}-paused','--home',layout,'--theme',theme,'--song','Posters','--song-paused')
        long=shot(env,f'{layout}-{theme}-long-track','--home',layout,'--theme',theme,
                  '--song','A very long track title with accents é 日本語 and words that must never overlap the status corner')
        assert not same(playing,plain)
        assert not same(playing,paused)
        for image in (playing,paused,long):
            assert same(image.crop((0,80,1024,768)),plain.crop((0,80,1024,768)))
            assert same(image.crop((752,0,1024,80)),plain.crop((752,0,1024,80)))
        flipped=shot(env,f'{layout}-{theme}-flip','--home',layout,'--theme',
                     'dark' if theme=='light' else 'light','--flip')
        assert same(flipped,plain), f'{layout} {theme} flip'
# Actual motion and animated atlas frames from the same native path.
for layout in ('spotlight','gallery'):
    moved=shot(env,f'{layout}-moving','--home',layout,'--theme','light','--sysmove','0.25')
    assert not same(moved,Image.open(OUT/f'{layout}-light.png').convert('RGBA'))
    shot(env,f'{layout}-animated','--home',layout,'--theme','dark','--phase','1000')
# A focus frame (main.c, focus_draw): the focused machine's square alone,
# drawn over the whole frame of an earlier moment, is the whole frame of its
# own moment byte for byte, the music header up or not; and its atlas frames
# change nothing outside that square (home_focus_rect, 536,112 448x448).
for theme,extra in (('light',()),('dark',('--song','Posters'))):
    first=shot(env,f'focus-{theme}-0','--home','spotlight','--theme',theme,'--phase','0',*extra)
    moved=0
    for ph in (150,300,450,600,900,1200):
        whole=shot(env,f'focus-{theme}-whole-{ph}','--home','spotlight','--theme',theme,'--phase',str(ph),*extra)
        part=shot(env,f'focus-{theme}-part-{ph}','--home','spotlight','--theme',theme,'--phase',str(ph),
                  '--focus-from','0',*extra)
        assert same(whole,part), f'{theme}: the focus frame at {ph} is not the whole frame'
        box=ImageChops.difference(first,whole).convert('RGB').getbbox()
        if box:
            moved+=1
            assert box[0]>=536 and box[1]>=112 and box[2]<=984 and box[3]<=560, \
                f'{theme}: the frame at {ph} changed {box}, outside the focus square'
    assert moved, f'{theme}: no atlas frame changed between 0 and 1200 ms'
# And every system focused in turn, on a card that has them all, so the row
# under each sprite is a full one, at every tile of its animation (the middle
# of each tile's first frame, from src/systemart_data.h): the machines whose
# frames reach the row have the row's sprites drawn again over them, and no
# tile of any falls back to a whole frame. Each on a card of its own (its
# .last puts the cursor there), four at a time.
from concurrent.futures import ThreadPoolExecutor
ALL = [l.split('|') for l in (ROOT/'config/systems.cfg').read_text().splitlines() if l.startswith('sys|')]
FRAMES = {m.group(1): [(int(t), int(ms)) for t, ms in re.findall(r'\{\s*(\d+),\s*(\d+)\s*\}', m.group(2))]
          for m in re.finditer(r'static const sa_frame SA_FRAMES_(\w+)\[\] = \{(.*?)\};',
                               (ROOT/'src/systemart_data.h').read_text(), re.S)}
def tile_phases(tag):
    seen, at, out = set(), 0, []
    for t, ms in FRAMES.get(tag, []):
        if t not in seen:
            seen.add(t)
            out.append(at + ms // 2)
        at += ms
    return out
def sweep(f):
    tag = f[4]
    base, envs = fixture(f'focus-all-{tag}', {g[4] for g in ALL})
    (base/'root/.last').write_text(tag+'\n')
    def one(name, *args):
        file = base/(name+'.png')
        out = subprocess.run([str(BIN),'--shot',str(file),'--screen','systems','--status',STATUS,
                              '--home','spotlight','--theme','light',*args],
                             env=envs,cwd=ROOT,capture_output=True,text=True)
        assert out.returncode == 0, (tag, args, out.stderr[-1500:])
        return Image.open(file).convert('RGBA'), out.stderr
    phases, fell = tile_phases(tag), 0
    for ph in phases:
        whole, _ = one(f'whole-{ph}','--phase',str(ph))
        part, said = one(f'part-{ph}','--phase',str(ph),'--focus-from','0')
        assert same(whole,part), f'{tag}: the focus frame at {ph} ms is not the whole frame'
        fell += 'focus: not for this sprite' in said
    shutil.rmtree(base)
    return tag, len(phases), fell
with ThreadPoolExecutor(4) as pool:
    swept = list(pool.map(sweep, ALL))
assert sum(n for _, n, _ in swept) >= 300, 'too few animation tiles found in systemart_data.h'
assert not any(fell for _, _, fell in swept), \
    'whole frames, not focus frames, for ' + ', '.join(t for t, _, fell in swept if fell)
# Zyper's song pop-up (DESIGN.md 11.22): under the header, its slab inside the
# standard panel's 800 centered, nothing it draws above y 40 and only the
# panel's faint shadow in the header row; nothing at its first instant, nor
# once it has gone; a long name holds the slab at the panel's width; on the
# games shelf too.
def footprint(image, plain, floor=24):
    """The box of what changed by more than `floor`, the shadow left out"""
    d = ImageChops.difference(image.convert('RGB'), plain.convert('RGB')).convert('L')
    return d.point(lambda v: 255 if v > floor else 0).getbbox()
SONG = 'Batman|Stage 1 - Gotham City'
for layout in ('spotlight','gallery'):
    for theme in ('light','dark'):
        plain=Image.open(OUT/f'{layout}-{theme}.png').convert('RGBA')
        up=shot(env,f'{layout}-{theme}-songpop','--home',layout,'--theme',theme,'--songpop',SONG)
        box=footprint(up,plain)
        assert box and box[0] >= 112 and box[2] <= 912 and box[1] >= 82 and box[3] <= 216, \
            f'{layout} {theme}: the pop-up at {box}'
        assert same(up.crop((0,0,1024,40)),plain.crop((0,0,1024,40))), f'{layout} {theme}: over the top'
        assert footprint(up.crop((0,0,1024,90)),plain.crop((0,0,1024,90)),16) is None, \
            f'{layout} {theme}: more than a shadow in the header'
        for ms in ('0','5000'):
            gone=shot(env,f'{layout}-{theme}-songpop-{ms}','--home',layout,'--theme',theme,'--songpop',SONG,ms)
            assert same(gone,plain), f'{layout} {theme}: the pop-up at {ms} ms'
long=shot(env,'songpop-long','--home','spotlight','--theme','light','--songpop',
          'A game whose name runs on|A song title far too long for any panel, which scrolls')
box=footprint(long,Image.open(OUT/'spotlight-light.png').convert('RGBA'))
assert box and box[0] >= 112 and box[2] <= 912 and box[2] - box[0] > 700, f'a long name at {box}'
run(env,'--shot',str(OUT/'games-plain.png'),'--screen','games','--status',STATUS,'--theme','dark')
run(env,'--shot',str(OUT/'games-songpop.png'),'--screen','games','--status',STATUS,'--theme','dark',
    '--songpop',SONG)
box=footprint(Image.open(OUT/'games-songpop.png'),Image.open(OUT/'games-plain.png'))
assert box and box[1] >= 82 and box[3] <= 216, f'the games shelf\'s pop-up at {box}'
# The real settings handler changes the database; a new process restores it.
shot(env,'setting-gallery','--menu','Home Screen','--press','right','--theme','light')
saved=shot(env,'restored-gallery','--theme','light')
assert same(saved,Image.open(OUT/'gallery-light.png').convert('RGBA'))
shot(env,'setting-spotlight','--menu','Home Screen','--press','left','--theme','light')
saved=shot(env,'restored-spotlight','--theme','light')
assert same(saved,Image.open(OUT/'spotlight-light.png').convert('RGBA'))
# Appearance still offers Auto through the real row handler.
shot(env,'setting-auto','--menu','Appearance','--press','left')
assert re.search(r'^\s+appearance\s+Auto$',run(env,'--dump'),re.M|re.I)
# MENU > Battery Icon: Percentage writes the charge left of the mark and
# changes nothing else; the real row handler saves it and Bars comes back.
shot(env,'setting-battery','--menu','Battery Icon','--press','right','--theme','light')
assert re.search(r'^\s+battery_icon\s+percentage$',run(env,'--dump'),re.M)
bars=Image.open(OUT/'spotlight-light.png').convert('RGBA')
for layout in ('spotlight','gallery'):
    for theme in ('light','dark'):
        pct=shot(env,f'battery-{layout}-{theme}','--home',layout,'--theme',theme)
        plain=Image.open(OUT/f'{layout}-{theme}.png').convert('RGBA')
        assert not same(pct.crop((560,0,1024,80)),plain.crop((560,0,1024,80))), f'{layout} {theme}: no number'
        assert same(pct.crop((0,80,1024,768)),plain.crop((0,80,1024,768)))
        assert same(pct.crop((0,0,560,80)),plain.crop((0,0,560,80)))
shot(env,'setting-battery-back','--menu','Battery Icon','--press','left','--theme','light')
assert same(shot(env,'battery-bars','--home','spotlight','--theme','light'),bars)
# MENU > Battery Icon > Time Left (src/battery_eta.h): icon= is the setting
# for one shot, eta= the minutes the estimate gives. With one, the corner
# writes the time where the percentage went and nothing else moves; with none
# yet (the first minutes after a start) it writes the percentage itself, and
# on Bars an estimate is never written.
def corner(name, status):
    file = OUT/(name+'.png')
    run(env,'--shot',str(file),'--screen','systems','--home','spotlight','--theme','light','--status',status)
    return Image.open(file).convert('RGBA')
pct=corner('time-pct',STATUS+' icon=percentage')
assert same(corner('time-none',STATUS+' icon=time'),pct), 'Time Left with no estimate must write the percentage'
left=corner('time-left',STATUS+' icon=time eta=205')
assert not same(left,pct), 'Time Left with an estimate must write the time'
assert same(left.crop((0,80,1024,768)),pct.crop((0,80,1024,768))), 'Time Left moved something below the corner'
assert same(left.crop((0,0,560,80)),pct.crop((0,0,560,80))), 'Time Left moved something left of the corner'
assert same(corner('time-bars',STATUS+' icon=bars eta=205'),bars), 'Bars must never write a time'
assert not same(corner('time-on-cable',STATUS+' icon=time chg eta=45'),corner('time-on-cable-pct',STATUS+' icon=percentage chg')), 'the time to full on the cable'
# One, two, empty and an unknown system remain valid native screens.
for name,tags in [('one',{'GB'}),('two',{'GB','GBA'}),('empty',set())]:
    _,small=fixture(name,tags)
    for layout in ('spotlight','gallery'):
        shot(small,f'{layout}-{name}','--home',layout,'--theme','light')
# The empty home (src/emptyhome.h): no games, so the two ways to add them,
# each card following what can start now (--ways), in both themes. The
# header is the home's in every state; each state changes its cards and
# nothing else; nothing on a card reaches into its right padding (a word too
# wide is fitted, never run past the edge); the Brick's words are its own.
_,empty=fixture('empty',set())
CARD_L, CARD_R, CARD_TOP, CARD_BOTTOM = (24, 500), (524, 1000), 252, 620
def in_pad(image, card):
    """A card's right padding, between its rounded corners: one color only"""
    x1 = card[1]
    return len(set(image.crop((x1 - 26, CARD_TOP + 24, x1 - 2, CARD_BOTTOM - 24)).convert('RGB').getdata())) == 1
for theme in ('light','dark'):
    ways={w:shot(empty,f'empty-{w}-{theme}','--ways',w,'--theme',theme) for w in ('usb','wifi','both','none')}
    for w,image in ways.items():
        assert same(image.crop((0,0,1024,CARD_TOP)),ways['usb'].crop((0,0,1024,CARD_TOP))), f'{w} {theme}: heading'
        assert same(image.crop((0,CARD_BOTTOM,1024,768)),ways['usb'].crop((0,CARD_BOTTOM,1024,768))), f'{w} {theme}: foot'
        assert in_pad(image,CARD_L) and in_pad(image,CARD_R), f'{w} {theme}: a card overflows'
    # A way's card is the same whenever its own state is
    left=lambda i: i.crop((CARD_L[0],CARD_TOP,CARD_L[1],CARD_BOTTOM))
    right=lambda i: i.crop((CARD_R[0],CARD_TOP,CARD_R[1],CARD_BOTTOM))
    assert same(left(ways['usb']),left(ways['both'])) and same(left(ways['wifi']),left(ways['none']))
    assert same(right(ways['wifi']),right(ways['both'])) and same(right(ways['usb']),right(ways['none']))
    assert not same(left(ways['usb']),left(ways['wifi'])) and not same(right(ways['usb']),right(ways['wifi']))
    brick=shot(empty,f'empty-brick-{theme}','--ways','none,brick','--theme',theme)
    assert in_pad(brick,CARD_L) and in_pad(brick,CARD_R), f'brick {theme}: a card overflows'
    assert not same(left(brick),left(ways['none'])) and not same(right(brick),right(ways['none']))
# Music and no games is the empty home too, with SELECT for MP4 in its foot
mus,menv=fixture('empty-mp4',set())
(mus/'Music/ZyperOS/Tones').mkdir(parents=True)
(mus/'Music/ZyperOS/Tones/01 Tone.mp3').touch()
with_mp4=shot(menv,'empty-mp4','--ways','usb','--theme','light')
plain=Image.open(OUT/'empty-usb-light.png').convert('RGBA')
assert same(with_mp4.crop((0,0,1024,680)),plain.crop((0,0,1024,680))), 'MP4 alone is still the empty home'
assert not same(with_mp4.crop((700,680,1024,768)),plain.crop((700,680,1024,768))), 'SELECT MP4 in the foot'
# The home with MP4 focused is still the home: the header every system has
# (the clock at the left), not the corner of MP4's own shelf of albums.
mp4,menv=fixture('mp4',{'GB','GBA'})
album=mp4/'Music/ZyperOS/Tones'
album.mkdir(parents=True)
(album/'01 Tone.mp3').touch()
for layout in ('spotlight','gallery'):
    run(menv,'--set','startup_system=Game Boy Advance')
    gba=shot(menv,f'{layout}-mp4-gba','--home',layout,'--theme','light')
    run(menv,'--set','startup_system=MP4')
    focused=shot(menv,f'{layout}-mp4','--home',layout,'--theme','light')
    assert not same(focused,gba), f'{layout}: MP4 not focused'
    assert same(focused.crop((0,0,1024,80)),gba.crop((0,0,1024,80))), f'{layout}: MP4 header'
# Missing and corrupt sources, without changing any source art.
bad,missing=fixture('missing',{'GBA'})
(bad/'root/system-icons').unlink()
(bad/'root/system-icons').mkdir()
shot(missing,'missing-art','--theme','light')
(bad/'root/system-icons/GBA.png').write_bytes(b'corrupt PNG')
(bad/'root/system-icons/GBA-still.png').write_bytes(b'corrupt PNG')
shot(missing,'corrupt-art','--theme','dark')
# No settings mutation from --home itself, and an invalid value is rejected.
assert re.search(r'^\s+home\s+spotlight$',run(env,'--dump'),re.M)
result=subprocess.run([str(BIN),'--home','vertical'],env=env,capture_output=True)
assert result.returncode==2
result=subprocess.run([str(BIN),'--home'],env=env,capture_output=True)
assert result.returncode==2
print('ok: native home shots, settings persistence, appearance, music header, song pop-up, theme flips, focus frames, small libraries, missing/corrupt art')
print(OUT)
