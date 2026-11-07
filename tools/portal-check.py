#!/usr/bin/env python3
"""The File Transfer page, driven in a browser: make check-portal.

    make build-native/portal-serve
    python3 tools/portal-check.py [--chrome PATH]

build-native/portal-serve serves the real page (res/web) and the real routes
(src/hare.c) over a scratch card of freeware names, as tools/readme-shots.py
does for its pictures, and headless Chrome runs the page over the DevTools
protocol. What it holds, each a thing I asked for on 2026-11-06:

  - every folder wears its pixel art (the shelf's, served at /icons/KEY.png):
    a console its own, Atomiswave too though it has no games, the menu
    music's BGM the cassette, the card's folders in the sidebar theirs
  - dot folders (.media) are not listed, and are when Settings says so
  - a batch that does not fit the card is asked about before a byte goes,
    with the files that fit offered in their order
  - the uploads show the time left for all of them, once there is a speed
  - the handheld's "stop after this file": the file on its way finishes, the
    rest stay on the computer and the panel says so
  - Settings: the update over the cable, off on an image with no release key,
    and with one, the files dropped, sent, checked and installed

No device is touched and nothing is fetched from the internet: the release
GitHub would answer is given to the page. Chrome is needed; without it the
check says so and fails, as a check that cannot run is not a pass.
"""
import argparse
import base64
import contextlib
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time
import urllib.request

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('readme_shots', ROOT / 'tools/readme-shots.py')
rs = importlib.util.module_from_spec(spec)
spec.loader.exec_module(rs)

failures = 0


def check(ok, what):
    global failures
    print(('  ok: ' if ok else '  FAIL: ') + what, flush=True)
    if not ok:
        failures += 1


@contextlib.contextmanager
def scratch(prefix):
    """A temp folder, removed afterwards however long Chrome takes to let go:
    its helper processes can still write into the profile here for a moment
    after the browser has exited, and TemporaryDirectory's removal then
    failed the check (seen under make check's parallel run, 2026-11-06)"""
    path = tempfile.mkdtemp(prefix=prefix)
    try:
        yield path
    finally:
        for _ in range(50):
            shutil.rmtree(path, ignore_errors=True)
            if not os.path.exists(path):
                break
            time.sleep(0.1)


class Page:
    """A served page in a headless Chrome tab"""

    def __init__(self, chrome, base):
        self.profile = base / 'chrome'
        self.browser = subprocess.Popen(
            [chrome, '--headless=new', '--remote-debugging-port=0', f'--user-data-dir={self.profile}',
             '--no-first-run', '--no-default-browser-check', '--disable-background-networking',
             '--disable-component-update', '--disable-sync', '--no-pings', 'about:blank'],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        for _ in range(100):
            port = self.profile / 'DevToolsActivePort'
            if port.is_file() and port.read_text().strip():
                break
            time.sleep(0.1)
        debug = (self.profile / 'DevToolsActivePort').read_text().split()[0]
        target = next(t for t in json.load(urllib.request.urlopen(f'http://127.0.0.1:{debug}/json/list'))
                      if t['type'] == 'page')
        self.dt = rs.DevTools(target['webSocketDebuggerUrl'])
        self.dt.call('Network.enable')
        self.dt.call('Emulation.setDeviceMetricsOverride', width=1280, height=800, deviceScaleFactor=1, mobile=False)
        self.dt.call('Emulation.setEmulatedMedia', features=[{'name': 'prefers-reduced-motion', 'value': 'reduce'}])

    def js(self, expr):
        r = self.dt.call('Runtime.evaluate', expression=expr, returnByValue=True, awaitPromise=True)
        if 'exceptionDetails' in r:
            raise RuntimeError(f'{expr[:60]}...: {r["exceptionDetails"]}')
        return r.get('result', {}).get('value')

    def until(self, expr, seconds=10):
        end = time.time() + seconds
        while time.time() < end:
            if self.js(expr):
                return True
            time.sleep(0.1)
        return False

    def close(self):
        self.browser.terminate()
        self.browser.wait()


class Server:
    """build-native/portal-serve over a card, with its stop question on stdin"""

    def __init__(self, card, web, *args):
        self.p = subprocess.Popen([str(ROOT / 'build-native/portal-serve'), str(card), str(web), *args],
                                  stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
        self.pin, port = self.p.stdout.readline().split()
        self.origin = f'http://127.0.0.1:{port}'

    def say(self, line):
        self.p.stdin.write(line + '\n')
        self.p.stdin.flush()

    def stop(self):
        try:
            self.p.stdin.close()
        except BrokenPipeError:
            pass
        self.p.wait(timeout=10)


def fresh(page, url):
    """A new page at url: by way of about:blank, waited for, or Chrome holds
    the second navigation behind the first. An upload the page is in the middle
    of is let go first: the page asks before it is left (beforeunload), and
    the question would hold the navigation."""
    page.js("typeof sending === 'undefined' || (sending = false)")
    page.dt.call('Page.navigate', url='about:blank')
    page.until("location.href === 'about:blank'")
    page.dt.call('Page.navigate', url=url)


def signed_in(page, server, path='roms'):
    fresh(page, f'{server.origin}/#{path}')
    page.until("document.readyState === 'complete' && typeof sendPin === 'function'")
    page.js(f"sendPin({json.dumps(server.pin)})")
    return page.until("!document.getElementById('app').hidden && "
                      "(document.querySelectorAll('#list li').length > 0 || !document.getElementById('settings').hidden)")


RELEASE = {'tag_name': 'v0.4.1', 'assets': [
    {'name': 'ZyperOS-pixel2-update.txt', 'size': 18000, 'browser_download_url': 'https://example.invalid/a'},
    {'name': 'ZyperOS-v0.4.1-pixel2-update.zpk', 'size': 38000000, 'browser_download_url': 'https://example.invalid/b'}]}


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--chrome', default=rs.CHROME)
    args = ap.parse_args()
    if not Path(args.chrome).is_file():
        print(f'FAIL: no browser at {args.chrome} (--chrome PATH)')
        return 1
    # make check-portal has just built it; run alone, this builds it once
    if not (ROOT / 'build-native/portal-serve').is_file():
        subprocess.run(['make', '-s', 'build-native/portal-serve'], cwd=ROOT, check=True)
    with scratch('zyperos-portal-check-') as temporary:
        base = Path(temporary)
        card, web = base / 'card', base / 'web'
        rs.portal_card(card)
        rs.web_dir(web)
        (card / 'Roms/NES/.media').mkdir()
        page = Page(args.chrome, base)
        try:
            print('the folders, their art, and what is hidden:')
            server = Server(card, web, 'usb', 'update=off')
            check(signed_in(page, server), 'the page came up past the PIN')
            got = page.js("""(() => { const row = (n) => [...document.querySelectorAll('#list li')]
                .find((li) => li.querySelector('.name').textContent === n);
                const src = (n) => { const r = row(n); const i = r && r.querySelector('.sico img');
                    return i ? i.getAttribute('src') : null; };
                return { nes: src('NES'), aw: src('Atomiswave'), bgm: src('BGM'), snes: src('SNES'),
                         tile: row('NES') && row('NES').querySelector('.sico').style.getPropertyValue('--sys'),
                         places: [...document.querySelectorAll('#places img')].map((i) => i.getAttribute('src')) }; })()""")
            check(got['nes'] == '/icons/NES.png' and got['snes'] == '/icons/SNES.png', f"consoles wear the shelf's art: {got}")
            check(got['aw'] == '/icons/AW.png', 'Atomiswave, with no games, has its art')
            check(got['bgm'] == '/icons/BGM.png', 'BGM has the cassette')
            check(bool(got['tile']), "a console's tile is in its color")
            check(got['places'][:3] == ['/icons/ROMS.png', '/icons/MUSE.png', '/icons/BOOKS.png'],
                  f"the card's folders wear theirs: {got['places']}")
            # Lazy, so the ones on screen: those below come as they scroll in
            check(page.until("[...document.querySelectorAll('#list .sico img, #places img')]"
                             ".filter((i) => i.getBoundingClientRect().top < innerHeight)"
                             ".every((i) => i.complete && i.naturalWidth > 0)"),
                  'every picture on screen came')
            page.js("go('roms/NES', 'none')")
            check(page.until("cwd === 'roms/NES'") and
                  not page.js("[...document.querySelectorAll('#list .name')].some((n) => n.textContent === '.media')"),
                  '.media is not listed')
            page.js("document.getElementById('showhidden').click(); go('roms/NES', 'none')")
            check(page.until("[...document.querySelectorAll('#list .name')].some((n) => n.textContent === '.media')"),
                  'Show hidden folders lists it')
            page.js("document.getElementById('showhidden').click()")

            print('room on the card:')
            page.js("go('roms/NES', 'none')")
            page.js("""(() => { const f = (n, s) => { const x = new File(['x'], n); Object.defineProperty(x, 'size', { value: s }); return x; };
                window.__fit = fitting([f('a.nes', 1e15), f('b.nes', 1000), f('c.nes', 2000)]); })()""")
            check(page.until("!document.getElementById('dialog').hidden"), 'a batch that does not fit is asked about')
            words = page.js("[document.getElementById('dlghead').textContent, document.getElementById('dlgyes').textContent]")
            check(words[0] == 'Not enough room on the card' and words[1] == 'Send the 2 that fit',
                  f'it offers the ones that fit: {words}')
            page.js("document.getElementById('dlgyes').click()")
            check(page.js("window.__fit.then((f) => f.map((x) => x.name).join(','))") == 'b.nes,c.nes',
                  'the files that fit, in their order')

            print('the time left, and the handheld stopping after the file on its way:')
            page.dt.call('Network.emulateNetworkConditions', offline=False, latency=0,
                         downloadThroughput=-1, uploadThroughput=262144)
            page.js("enqueue([new File([new Uint8Array(1500000)], 'Lizard.nes'), "
                    "new File([new Uint8Array(400000)], 'Micro Mages.nes'), "
                    "new File([new Uint8Array(400000)], 'Alter Ego.nes')])")
            check(page.until("/ left · about \\d+ s$/.test(document.getElementById('queueleft').textContent)", 8),
                  'the queue says what is left and the time: ' +
                  str(page.js("document.getElementById('queueleft').textContent")))
            server.say('after')
            check(page.until("document.getElementById('queuehead').textContent === '1 sent, 2 not sent'", 20),
                  'the file on its way finished and the rest stayed: ' +
                  str(page.js("document.getElementById('queuehead').textContent")))
            check('closing' in (page.js("document.getElementById('queueleft').textContent") or ''),
                  'the panel says the handheld closed it')
            check((card / 'Roms/NES/Lizard.nes').is_file() and not (card / 'Roms/NES/Micro Mages.nes').exists(),
                  'on the card: the first file, whole, and nothing of the rest')
            page.dt.call('Network.emulateNetworkConditions', offline=False, latency=0,
                         downloadThroughput=-1, uploadThroughput=-1)
            server.stop()

            print('Settings, and the update over the cable:')
            server = Server(card, web, 'usb', 'update=off')
            check(signed_in(page, server, 'settings'), 'Settings opened from its place in the address')
            check(page.until("/no release key/.test(document.getElementById('updbody').textContent)"),
                  'an image with no release key says updates are off, and why')
            server.stop()
            server = Server(card, web, 'usb', 'update=on')
            fresh(page, f'{server.origin}/#settings')
            page.until("document.readyState === 'complete' && typeof sendPin === 'function'")
            page.js("window.ZYPEROS_RELEASES = 'data:application/json,' + encodeURIComponent(" +
                    json.dumps(json.dumps(RELEASE)) + ")")
            page.js(f"sendPin({json.dumps(server.pin)})")
            check(page.until("/0\\.4\\.1 is out/.test(document.getElementById('updbody').textContent)"),
                  'the newest release is offered, with the files it takes')
            page.js("upTake([new File(['zyperos-update 1\\n'], 'ZyperOS-pixel2-update.txt'), "
                    "new File([new Uint8Array(300000)], 'ZyperOS-v0.4.1-pixel2-update (1).zpk')])")
            check(page.until("/is ready/.test(document.getElementById('updbody').textContent)", 30),
                  'sent, checked and installed: ' + str(page.js("document.getElementById('updbody').innerText.split('\\n')[0]")))
            cable = card / '.userdata/pixel2/update/cable'
            check((cable / 'ZyperOS-pixel2-update.txt').is_file() and
                  (cable / 'ZyperOS-v0.4.1-pixel2-update.zpk').stat().st_size == 300000,
                  'the files landed under their own names, a browser\'s "(1)" off')
            server.stop()
        finally:
            page.close()
    if failures:
        print(f'\n{failures} check(s) failed')
        return 1
    print('\nok: the page wears its art, hides dot folders, counts the room, says the time, '
          'follows the handheld\'s stop, and updates over the cable')
    return 0


if __name__ == '__main__':
    sys.exit(main())
