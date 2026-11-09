#!/usr/bin/env python3
"""Capture current launcher frames for the README and website.

make native
python3 tools/readme-shots.py

Writes build/readme-shots/light (every screen, in Beard), build/readme-shots/dark
(the few screens the README's light-and-dark section shows in Salt & Pepper)
and a manifest. --out changes that output directory. No device is accessed,
and nothing is fetched. Temporary demonstration cards use empty ROM and music
files with freeware or stand-in names; the launcher itself draws every pixel,
the game cards included. --art-dir copies local covers into the card; leave it
out for the repository's pictures, which show no commercial box art.
Achievements, playback timing, the Scraper's counts and the update page use
the launcher's built-in shot fixtures and the status file written below.

The File Transfer page (res/web) is shot in a browser: build-native/portal-serve
(make build-native/portal-serve) serves it over a scratch card of freeware
names, and headless Chrome draws it at a computer's width and a phone's, in
light, over the DevTools protocol. --chrome names the browser; --no-page
leaves those shots out. The server listens on a port the kernel picks while
it runs.
"""
import argparse
import base64
import hashlib
import json
import os
from pathlib import Path
import shutil
import socket
import sqlite3
import struct
import subprocess
import tempfile
import time
import urllib.parse
import urllib.request
import zlib

ROOT=Path(__file__).resolve().parents[1]
GAMES=[
    'Castlevania - Symphony of the Night (USA)', 'Crash Bandicoot (USA)',
    'Metal Gear Solid (USA)', "Peter Pan in Disney's Return to Never Land (USA)",
    'Tekken 3 (USA)', "Tony Hawk's Pro Skater 2 (USA)",
]
FOCUS=GAMES[2]   # the games page and its details open on Metal Gear Solid
# ScummVM's freeware games, as ScummVM's own site gives them away: a folder each
SCUMMVM=['Beneath a Steel Sky','Drascula - The Vampire Strikes Back',
         'Flight of the Amazon Queen','Lure of the Temptress','Soltys']
SCUMMVM_FOCUS=SCUMMVM[2]
# OpenBOR's shelf: Senile Team's freeware game, as a stand-in module
OPENBOR=['Beats of Rage.pak']
# name: (card, the shelf .last names, flags). 'demo' is the library above,
# 'media' the same with a film in Videos too,
# 'empty' a card with no games (the empty home), 'scummvm' ScummVM's freeware
# games alone, 'openbor' a module on the OpenBOR shelf.
SHOTS={
    'systems':('demo','PS1',['--home','spotlight']),
    'gallery':('demo','PS1',['--home','gallery']),
    'shelf':('demo','PS1',['--screen','games']),
    'info':('demo','PS1',['--screen','games','--info']),
    'info-delete':('demo','PS1',['--screen','games','--info-delete']),
    'delete-ask':('demo','PS1',['--screen','games','--delete-ask']),
    'cheevos':('demo','PS1',['--cheevos-screen','0','--cheevos-badge','{card}/Roms/PlayStation/.cheevos/Hagane.set','b#']),
    # An unlock over a game (DESIGN.md 11.7): its badge, and Zyper at the right
    'cheevo-notice':('demo','PS1',['--notice-kind','unlock','--notice',"Violated Heavens",'Unlocked \u00b7 10 points',
                                   '--notice-badge','{card}/Roms/PlayStation/.cheevos/badges/b3.png']),
    'menu':('demo','PS1',['--menu','Search Games']),
    'mp4':('media','MUSE',['--screen','games']),
    'nowplaying':('demo','MUSE',['--nowplaying','0','0','83']),
    'hare':('demo','PS1',['--hare','http://192.168.1.42','1 connected','Tobu Tobu Girl.gb']),
    'hotspot':('demo','PS1',['--hotspot','ZyperOS-7A2B','q7mx-3kpa','1 joined','waiting']),
    'usb-ask':('demo','PS1',['--usb-ask','0']),
    'scummvm':('scummvm','SCUMMVM',['--screen','games']),
    # The in-game menu over OpenBOR (src/portmenu.h), on the launcher's
    # made-up frame rather than a game's
    'openbor':('openbor','OPENBOR',['--screen','games','--gamemenu','0','port']),
    'scraper':('demo','PS1',['--scraper','2']),
    'update':('demo','PS1',['--update']),
    'ports':('ports','PRT',['--screen','games']),
    # The in-game menu over a port, and the first of its Controls pages: what
    # its README says the buttons do (the made-up Star Farm, SHOT_FOCUS)
    'ports-menu':('portdocs','PRT',['--screen','games','--gamemenu','0','port']),
    'ports-controls':('portdocs','PRT',['--screen','games','--gamecontrols','0','PRT']),
    'portmaster':('demo','PS1',['--portmaster','running']),
    # MENU > Storage (DESIGN.md 11.32) on its stand-in card of 128 GB nearly
    # full, and the demonstration card's PlayStation page
    'storage':('demo','PS1',['--storage','main']),
    'storage-system':('demo','PS1',['--storage','system']),
    # ...and its Music page: albums by size, X's order beside the list
    'storage-music':('demo','PS1',['--storage','music']),
    'empty':('empty','',['--ways','usb']),
    'tour-theme':('demo','PS1',['--tour','theme','2000','--tour-music','--tour-pick','0']),
    'tour-home':('demo','PS1',['--tour','home','2000','--tour-music','--tour-pick','1']),
    'tour-sounds':('demo','PS1',['--tour','sounds','2000','--tour-music','--tour-pick','1']),
    'tour-music':('demo','PS1',['--tour','music','2000','--tour-music','--tour-pick','0']),
    # The blog's battery guide (site/blog/): the charging screen of a handheld
    # that is off, and Zyper's heat warning on the home
    'charging':('demo','PS1',['--charging','4000']),
    'heat':('demo','PS1',['--heatpop','1600']),
    # The README's languages (DESIGN.md 11.30): the first start's step on
    # 简体中文, and MENU in Portuguese and in Japanese
    'tour-language':('demo','PS1',['--tour','lang','3000','--tour-pick','1']),
    'menu-pt':('demo','PS1',['--lang','pt-BR','--menu','Language']),
    'menu-ja':('demo','PS1',['--lang','ja','--menu','Search Games']),
    # MENU > Battery Icon on Time Left: the corner writes the time left
    # (STATUS below), as 3 h 25 min beside the battery
    'battery':('demo','PS1',['--menu','Battery Icon']),
    # The top bar on the home while a song plays, with headphones in the jack:
    # the song's mark and the headphones' (src/status.h), in both themes, and
    # the mark of a song paused
    'topbar':('demo','PS1',['--home','spotlight','--song','Night Drive']),
    'topbar-paused':('demo','PS1',['--home','spotlight','--song','Night Drive','--song-paused']),
    # The quick slot and the hotkeys (2026-11-07): the in-game menu on its
    # Quick Slot row, a PlayStation game's buttons drawn as their shapes, and
    # its Hotkeys page, SELECT with a shoulder
    'gamemenu':('demo','PS1',['--screen','games','--gamemenu','3']),
    'controls-ps1':('demo','PS1',['--screen','games','--gamecontrols','0','PS1']),
    'controls-hotkeys':('demo','PS1',['--screen','games','--gamecontrols','2','PS1']),
}
# The game a shot's shelf opens on, where it is not its system's own (below)
SHOT_FOCUS={'ports-menu':'Star Farm.sh','ports-controls':'Star Farm.sh'}
# A shot's own --status where the README's default (no Wi-Fi, 82%) would hide
# what it shows: icon= is the setting for this shot, eta= the minutes left
STATUS={'battery':'10:08 24h wifi=3 batt=62 eta=205 icon=time',
        'topbar':'10:08 24h wifi=3 phones=on batt=62','topbar-paused':'10:08 24h wifi=3 phones=on batt=62'}
# The README's light-and-dark section: these, in both themes
DARK=['systems','menu','topbar']
# A shot whose screen draws its own clock, outside the status corner, so the
# launcher's corner check (shot_corner_clear in src/main.c) reports this after
# writing a right picture: the charging screen's clock is its own (DESIGN.md
# 11.25). Accepted for that shot alone, by its exact words.
CORNER_OWN={'charging':'no corner drawn, though --status gave it something to show'}
# MENU > System Update as a card with two system slots sees an update (83 MB,
# the size of the Pixel 2's two-slot test download, tarikbc/ZyperOS#59)
UPDATE_STATUS='state available\ninstalled 0.1.0\nversion 0.1.1\ndownload 83000000\nkind system\n'

def png_rgb(path,w,h,rows):
    """An 8-bit RGB PNG from rows of (r,g,b), standard library only"""
    raw=b''.join(b'\x00'+bytes(c for px in row for c in px) for row in rows)
    def chunk(kind,body):
        out=struct.pack('>I',len(body))+kind+body
        return out+struct.pack('>I',zlib.crc32(kind+body)&0xFFFFFFFF)
    path.write_bytes(b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',w,h,8,2,0,0,0))
                    +chunk(b'IDAT',zlib.compress(raw,9))+chunk(b'IEND',b''))

# Stand-ins for an achievement's badge (src/rabadge.h): a 64-pixel square, a
# diagonal blend of one color, a frame and a diamond. RetroAchievements' own
# are the games' art, and none is in this repository.
BADGE_COLORS=[(214,87,62),(61,142,201),(242,178,52),(120,186,102),(163,92,201),(40,168,160),(224,112,90),(90,100,220)]
def stand_in_badge(path,color):
    rows=[]
    for y in range(64):
        row=[]
        for x in range(64):
            t=(x+y)/126
            c=[round(v*(1.0-0.38*t)) for v in color]
            if min(x,y,63-x,63-y)<3: c=[round(v*0.55) for v in color]
            elif abs(x-31.5)+abs(y-31.5)<15: c=[min(255,round(v*0.35+255*0.65)) for v in color]
            row.append(tuple(c))
        rows.append(row)
    png_rgb(path,64,64,rows)

def card_dirs(card):
    for folder in ['ZyperOS','Roms','userdata','shared']: (card/folder).mkdir(parents=True)
    shutil.copyfile(ROOT/'config/systems.cfg',card/'ZyperOS/systems.cfg')
    shutil.copytree(ROOT/'res/system-icons',card/'ZyperOS/system-icons')
    (card/'ZyperOS/VERSION').write_text('0.1.0\n')

def demo_card(card,art_dir,manifest):
    card_dirs(card)
    for folder,game in [('NES','Super Mario Bros.nes'),('SNES','Super Metroid.sfc'),('Game Boy','Tetris.gb'),('Genesis','Sonic the Hedgehog.md'),('Dreamcast','Sonic Adventure.chd'),('Game Boy Advance','Metroid Fusion.gba')]:
        d=card/'Roms'/folder;d.mkdir();(d/game).touch()
    pm_art(card)   # the Install PortMaster page's icon (--portmaster)
    media=card/'Roms/PlayStation/.media';media.mkdir(parents=True)
    for name in GAMES:
        (media.parent/(name+'.chd')).touch()
        art=art_dir/(name+'.png') if art_dir else None
        if art and art.is_file():
            shutil.copyfile(art,media/(name+'.png'))
            manifest['artwork'][name]=hashlib.sha256(art.read_bytes()).hexdigest()
    # Stand-in badges for the achievements list and notice shots, beside the
    # (absent) set of the PlayStation folder as src/rabadge.h keeps them
    badges=card/'Roms/PlayStation/.cheevos/badges';badges.mkdir(parents=True)
    for i,color in enumerate(BADGE_COLORS): stand_in_badge(badges/f'b{i+1}.png',color)
    # Curated metadata for the demonstration's focused game. This is
    # library data, not a modified drawing path or a user's saved state.
    dbdir=card/'shared/.zyperos';dbdir.mkdir()
    with sqlite3.connect(dbdir/'library.db') as db:
        db.execute('CREATE TABLE games(folder TEXT NOT NULL,file TEXT NOT NULL,year TEXT,publisher TEXT,developer TEXT,players TEXT,genres TEXT,esrb TEXT,note TEXT,synopsis TEXT,scraped INTEGER,PRIMARY KEY(folder,file))')
        db.execute('INSERT INTO games VALUES(?,?,?,?,?,?,?,?,?,?,?)',('PlayStation',FOCUS+'.chd','1998','Konami','KCEJ','1','Action','','','',0))
    for album in ['First Light','Night Drive','Quiet Hours']:
        d=card/'Music/Demo Library'/album;d.mkdir(parents=True)
        for track in ['01 First Light.mp3','02 Open Road.mp3','03 Home Again.mp3']: (d/track).touch()
    # The tutorial's Menu Music step shows only on a card with tracks
    shutil.copytree(ROOT/'res/bgm',card/'ZyperOS/res/bgm',ignore=shutil.ignore_patterns('*.json'))
    shutil.copytree(ROOT/'res/tour',card/'ZyperOS/res/tour')
    up=card/'userdata/update';up.mkdir(parents=True)
    (up/'status').write_text(UPDATE_STATUS)
    # A test release's source on the card counts as a network to look with
    # (docs/updates.md), so the page offers its download as on Wi-Fi
    (up/'source').write_text('file:///nonexistent\n')

def media_card(card,art_dir=None,manifest=None):
    """The demonstration card and a film, Blender's freely licensed Sintel as
    a stand-in name: MP4's shelf shows All, the albums and the videos mixed
    (src/muselib.h), and its menu has Show"""
    demo_card(card,art_dir,manifest if manifest is not None else {'artwork':{}})
    (card/'Videos').mkdir();(card/'Videos/Sintel.mp4').touch()

# The Install PortMaster page's shots: the status file as res/portmaster/
# pm-install.py writes it, read through ZYPEROS_PM_STATUS (--portmaster)
PM_SHOTS={'portmaster':'state downloading\nversion 2026.06.23-0015\ntotal 24121702\ndone 9800000\n'}

def ports_card(card):
    """A card whose ZyperOS has the PortMaster installer and whose Ports
    folder has no PortMaster yet: the Ports shelf offers to install it"""
    card_dirs(card)
    (card/'Roms/NES').mkdir(exist_ok=True);(card/'Roms/NES/Super Tilt Bro.nes').touch()
    (card/'Roms/Ports').mkdir(exist_ok=True)
    pm_art(card)
    shutil.copyfile(ROOT/'res/portmaster/pm-install.py',card/'ZyperOS/portmaster/pm-install.py')

def pm_art(card):
    """PortMaster's covers and icon in the card's ZyperOS, as the payload
    puts them there (tools/genportmaster.py)"""
    (card/'ZyperOS/portmaster').mkdir(parents=True,exist_ok=True)
    for name in ('cover-light.png','cover-dark.png','icon.png'):
        shutil.copyfile(ROOT/'res/portmaster'/name,card/'ZyperOS/portmaster'/name)

def pm_env(env,name,folder):
    """env with name's status file, when it is one of PM_SHOTS"""
    if name in PM_SHOTS:
        status=Path(folder)/('pm-'+name)/'status'
        status.parent.mkdir(parents=True,exist_ok=True)
        status.write_text(PM_SHOTS[name])
        env['ZYPEROS_PM_STATUS']=str(status)
    return env

def scummvm_card(card):
    card_dirs(card)
    for name in SCUMMVM:
        d=card/'Roms/ScummVM'/name;d.mkdir(parents=True);(d/'readme.txt').touch()

def portdocs_card(card):
    """A card whose Ports folder has the made-up ports of tools/fixtures/ports
    (port.json, keymaps, READMEs) and PortMaster's script: the in-game menu
    over a port and its Controls pages"""
    card_dirs(card)
    shutil.copytree(ROOT/'tools/fixtures/ports',card/'Roms/Ports')
    (card/'Roms/Ports/PortMaster.sh').touch()
    pm_art(card)

def openbor_card(card):
    card_dirs(card)
    d=card/'Roms/OpenBOR';d.mkdir(parents=True)
    for name in OPENBOR: (d/name).touch()

# The File Transfer page: a computer's window on a console's folder with an
# upload going (throttled to 64 KB/s, shot 4.5 s in, once its time left shows), and a phone's on the PIN
# and on ROMs, a folder per console with its color
PAGES={
    'portal':dict(width=1280,height=800,scale=1,phone=False,folder='roms/NES',upload=('Super Tilt Bro.nes',524304)),
    'portal-pin':dict(width=390,height=844,scale=2,phone=True,gate=True),
    'portal-phone':dict(width=390,height=844,scale=2,phone=True,folder='roms'),
}
# Freeware and homebrew, by folder; sizes as the files have them, roughly
PORTAL_GAMES={
    'NES':[('Alter Ego.nes',40976),('Blade Buster.nes',393232),('Lawn Mower.nes',24592),('Nova the Squirrel.nes',262160)],  # and Super Tilt Bro., uploading
    'Game Boy':[('Libbet and the Magic Floor.gb',65536),('Tobu Tobu Girl.gb',131072)],
    'PICO-8':[('Celeste Classic.p8.png',30111)],
}
PORTAL_SCUMMVM=['Beneath a Steel Sky','Flight of the Amazon Queen']
CHROME='/Applications/Google Chrome.app/Contents/MacOS/Google Chrome'

def portal_card(card):
    """A card as the first boot leaves it, a folder per console, with a few
    freeware games and the demonstration albums"""
    for line in (ROOT/'config/systems.cfg').read_text().splitlines():
        if line.startswith('sys|'): (card/'Roms'/line.split('|')[2]).mkdir(parents=True,exist_ok=True)
    for folder,files in PORTAL_GAMES.items():
        for name,size in files:
            with open(card/'Roms'/folder/name,'wb') as f: f.truncate(size)
    for name in PORTAL_SCUMMVM:
        d=card/'Roms/ScummVM'/name;d.mkdir(parents=True,exist_ok=True)
        with open(d/'readme.txt','wb') as f: f.truncate(2048)
    for album in ['First Light','Night Drive','Quiet Hours']:
        d=card/'Music/Demo Library'/album;d.mkdir(parents=True)
        for track in ['01 First Light.mp3','02 Open Road.mp3','03 Home Again.mp3']:
            with open(d/track,'wb') as f: f.truncate(4200000)
    for folder in ['Audiobooks','Videos','Bios','Saves','.userdata/shared/.zyperos','ZyperOS']:
        (card/folder).mkdir(parents=True,exist_ok=True)
    shutil.copyfile(ROOT/'config/systems.cfg',card/'ZyperOS/systems.cfg')
    # The pictures every folder wears on the page, as the card has them
    shutil.copytree(ROOT/'res/system-icons',card/'ZyperOS/system-icons')
    # The menu music's folder, which is no system and has its own picture
    (card/'Roms/BGM').mkdir(exist_ok=True)

def web_dir(web):
    """res/web as mk/payload.sh lays it out on the card"""
    shutil.copytree(ROOT/'res/web',web)
    for font in ['menu.ttf','title.ttf']: shutil.copyfile(ROOT/'res/fonts'/font,web/font)

class DevTools:
    """Just enough of a WebSocket client for Chrome's DevTools protocol:
    text frames, one call at a time, events read past."""
    def __init__(self,url):
        u=urllib.parse.urlparse(url)
        self.s=socket.create_connection((u.hostname,u.port),timeout=30)
        key=base64.b64encode(os.urandom(16)).decode()
        self.s.sendall((f'GET {u.path} HTTP/1.1\r\nHost: {u.hostname}:{u.port}\r\nUpgrade: websocket\r\n'
                        f'Connection: Upgrade\r\nSec-WebSocket-Key: {key}\r\nSec-WebSocket-Version: 13\r\n\r\n').encode())
        head=b''
        while b'\r\n\r\n' not in head: head+=self.s.recv(4096)
        head,self.buf=head.split(b'\r\n\r\n',1)
        if b' 101 ' not in head.split(b'\r\n')[0]: raise RuntimeError('DevTools refused the socket')
        self.buf=bytearray(self.buf);self.n=0
    def _take(self,n):
        while len(self.buf)<n:
            got=self.s.recv(1<<20)
            if not got: raise EOFError('DevTools closed')
            self.buf+=got
        out=bytes(self.buf[:n]);del self.buf[:n];return out
    def _message(self):
        whole=bytearray()
        while True:
            b0,b1=self._take(2);n=b1&0x7f
            if n==126: n=struct.unpack('>H',self._take(2))[0]
            elif n==127: n=struct.unpack('>Q',self._take(8))[0]
            if b1&0x80: self._take(4)
            whole+=self._take(n)
            if b0&0x0f==0x8: raise EOFError('DevTools closed')
            if b0&0x80: return whole.decode()
    def call(self,method,**params):
        self.n+=1
        data=json.dumps({'id':self.n,'method':method,'params':params}).encode()
        mask=os.urandom(4);head=bytearray([0x81])
        if len(data)<126: head.append(0x80|len(data))
        else: head.append(0x80|126);head+=struct.pack('>H',len(data))
        self.s.sendall(bytes(head)+mask+bytes(b^mask[i%4] for i,b in enumerate(data)))
        while True:
            m=json.loads(self._message())
            if m.get('id')==self.n:
                if 'error' in m: raise RuntimeError(f'{method}: {m["error"]}')
                return m.get('result',{})

def page_shots(chrome,dest,only,manifest):
    with tempfile.TemporaryDirectory(prefix='zyperos-portal-') as temporary:
        base=Path(temporary);card=base/'card';web=base/'web';profile=base/'chrome';dest.mkdir(parents=True,exist_ok=True)
        portal_card(card);web_dir(web)
        subprocess.run(['make','-s','build-native/portal-serve'],cwd=ROOT,check=True)
        # A 128 GB card's room, as the page's pictures show it
        server=subprocess.Popen([str(ROOT/'build-native/portal-serve'),str(card),str(web),'space=88.4/119.1'],
                                stdin=subprocess.PIPE,stdout=subprocess.PIPE,text=True)
        browser=None
        try:
            pin,port=server.stdout.readline().split()
            origin=f'http://127.0.0.1:{port}'
            reply=urllib.request.urlopen(urllib.request.Request(origin+'/api/auth',data=pin.encode(),method='POST'))
            cookie=reply.headers['Set-Cookie'].split(';')[0].split('=',1)
            browser=subprocess.Popen([chrome,'--headless=new','--remote-debugging-port=0',f'--user-data-dir={profile}',
                                      '--no-first-run','--no-default-browser-check','--hide-scrollbars',
                                      '--disable-background-networking','--disable-component-update','--disable-sync',
                                      '--no-pings','about:blank'],
                                     stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
            for _ in range(100):
                if (profile/'DevToolsActivePort').is_file() and (profile/'DevToolsActivePort').read_text().strip(): break
                time.sleep(0.1)
            debug=(profile/'DevToolsActivePort').read_text().split()[0]
            target=next(t for t in json.load(urllib.request.urlopen(f'http://127.0.0.1:{debug}/json/list')) if t['type']=='page')
            dt=DevTools(target['webSocketDebuggerUrl'])
            dt.call('Network.enable')
            dt.call('Emulation.setEmulatedMedia',features=[{'name':'prefers-color-scheme','value':'light'},
                                                           {'name':'prefers-reduced-motion','value':'reduce'}])
            for name,page in PAGES.items():
                if only and name not in only: continue
                gate=page.get('gate',False);folder=page.get('folder','')
                if gate: dt.call('Network.deleteCookies',name=cookie[0],url=origin+'/')
                else: dt.call('Network.setCookie',name=cookie[0],value=cookie[1],url=origin+'/',httpOnly=True)
                dt.call('Emulation.setDeviceMetricsOverride',width=page['width'],height=page['height'],
                        deviceScaleFactor=page['scale'],mobile=page['phone'])
                dt.call('Emulation.setTouchEmulationEnabled',enabled=page['phone'])
                dt.call('Page.navigate',url='about:blank')
                # Waited for: Chrome holds a navigation that comes before the
                # one ahead of it is in
                for _ in range(100):
                    if dt.call('Runtime.evaluate',returnByValue=True,expression="location.href").get('result',{}).get('value')=='about:blank': break
                    time.sleep(0.05)
                dt.call('Page.navigate',url=f'{origin}/#{urllib.parse.quote(folder)}')
                up=("!document.getElementById('gate').hidden && !!document.getElementById('app').hidden" if gate else
                    "!document.getElementById('app').hidden && document.querySelectorAll('#list li').length>0"
                    " && !document.getElementById('room').hidden")
                for _ in range(100):
                    ready=dt.call('Runtime.evaluate',returnByValue=True,awaitPromise=True,
                                  expression=f"document.fonts.ready.then(()=>document.readyState==='complete' && {up})")
                    if ready.get('result',{}).get('value'): break
                    time.sleep(0.1)
                else: raise RuntimeError(f'{name}: the page did not come up')
                if 'upload' in page:
                    file,size=page['upload']
                    dt.call('Network.emulateNetworkConditions',offline=False,latency=0,
                            downloadThroughput=-1,uploadThroughput=65536)
                    dt.call('Runtime.evaluate',expression=f"enqueue([new File([new Uint8Array({size})], {json.dumps(file)})])")
                    time.sleep(4.5)   # the time left needs three seconds of speed
                else: time.sleep(0.5)
                target=dest/(name+'.png')
                target.write_bytes(base64.b64decode(dt.call('Page.captureScreenshot',format='png')['data']))
                # The page asks before it is left mid-upload (beforeunload),
                # which would hold the next navigation: the upload is let go
                dt.call('Runtime.evaluate',expression='sending = false')
                dt.call('Network.emulateNetworkConditions',offline=False,latency=0,downloadThroughput=-1,uploadThroughput=-1)
                manifest['screenshots'].append({'file':f'light/{name}.png','card':'portal',
                    'args':dict(page,page='res/web/index.html'),
                    'sha256':hashlib.sha256(target.read_bytes()).hexdigest()})
                print('light/'+name,flush=True)
        finally:
            if browser: browser.terminate();browser.wait()
            server.stdin.close();server.wait(timeout=10)

def env_for(card):
    return dict(os.environ, ZYPEROS_HEADLESS='1', SDL_VIDEODRIVER='dummy', SDL_RENDER_DRIVER='software',
                ZYPEROS_ROOT=str(card/'ZyperOS'),ZYPEROS_CARD=str(card),ZYPEROS_ROMS=str(card/'Roms'),
                ZYPEROS_USERDATA=str(card/'userdata'),ZYPEROS_SHARED=str(card/'shared'),ZYPEROS_WEB=str(card/'web'),
                ZYPEROS_FONT=str(ROOT/'res/fonts/menu.ttf'))

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--art-dir',type=Path)
    ap.add_argument('--out',type=Path,default=ROOT/'build/readme-shots')
    ap.add_argument('--chrome',default=CHROME,help='the browser for the File Transfer page')
    ap.add_argument('--no-page',action='store_true',help='leave out the File Transfer page')
    ap.add_argument('only',nargs='*',help='shot names (default: all)')
    args=ap.parse_args();out=args.out.resolve();out.mkdir(parents=True,exist_ok=True)
    binary=ROOT/'build-native/zyperos'
    if not binary.is_file(): ap.error('run make native first')
    manifest={'source_commit':subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),
              'renderer':'current native launcher, SDL software, headless',
              'data':'demonstration library (freeware or stand-in names, empty files); built-in shot fixtures',
              'artwork':{},'screenshots':[]}
    with tempfile.TemporaryDirectory(prefix='zyperos-readme-') as temporary:
        cards={'demo':Path(temporary)/'demo','empty':Path(temporary)/'empty','scummvm':Path(temporary)/'scummvm',
               'openbor':Path(temporary)/'openbor','ports':Path(temporary)/'ports',
               'portdocs':Path(temporary)/'portdocs','media':Path(temporary)/'media'}
        demo_card(cards['demo'],args.art_dir,manifest)
        media_card(cards['media'],args.art_dir)
        scummvm_card(cards['scummvm'])
        openbor_card(cards['openbor'])
        ports_card(cards['ports'])
        portdocs_card(cards['portdocs'])
        card_dirs(cards['empty'])
        for theme in ['light','dark']:
            dest=out/theme;dest.mkdir(exist_ok=True)
            for name,(card_name,tag,flags) in SHOTS.items():
                if theme=='dark' and name not in DARK: continue
                if args.only and name not in args.only: continue
                card=cards[card_name]
                focus=SHOT_FOCUS.get(name) or {'PS1':FOCUS+'.chd','SCUMMVM':SCUMMVM_FOCUS,'OPENBOR':OPENBOR[0],'PRT':'PortMaster.sh'}.get(tag,'')
                (card/'ZyperOS/.last').write_text(tag+'\n'+focus+'\n')
                target=dest/(name+'.png')
                # The tutorial draws no status corner, so it has no --status
                status=[] if name.startswith('tour-') else ['--status',STATUS.get(name,'10:08 24h wifi=off batt=82')]
                # {card} is where this run's temporary card is; the manifest keeps the word
                command=[str(binary),'--shot',str(target),'--theme',theme,
                         '--screen','systems','--phase','3200',*status,'--fn-volume',*[f.replace('{card}',str(card)) for f in flags]]
                result=subprocess.run(command,cwd=ROOT,env=pm_env(env_for(card),name,temporary),text=True,
                                      stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
                (dest/(name+'.log')).write_text(result.stdout)
                if result.returncode and not (name in CORNER_OWN and CORNER_OWN[name] in result.stdout):
                    raise RuntimeError(f'{theme}/{name}: {result.stdout}')
                if not target.is_file(): raise RuntimeError(f'missing {target}')
                manifest['screenshots'].append({'file':f'{theme}/{name}.png','card':card_name,'args':[a.replace(str(card),'{card}') for a in command[3:]],
                    'sha256':hashlib.sha256(target.read_bytes()).hexdigest()})
                print(theme+'/'+name,flush=True)
    if not args.no_page and (not args.only or set(args.only)&set(PAGES)):
        if not Path(args.chrome).is_file(): ap.error(f'no browser at {args.chrome}: --chrome, or --no-page')
        page_shots(args.chrome,out/'light',args.only,manifest)
    (out/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')

if __name__=='__main__': main()
