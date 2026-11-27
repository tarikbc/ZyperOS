#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""check-link-core: the Link Cable on a Game Boy game running on mGBA, and a
game's own core (Tarik's decision of 2026-11-27, DESIGN.md 11.72), in desk
mode's launcher on a card of its own, driven as a person would:

  - A game on the Game Boy shelf, which runs on mgba (the shelf's choice),
    has the Link Cable row; A on it asks Restart on Gambatte, Cancel first
    and B the same: nothing changes.
  - Restart: the game stops and starts again on gambatte, and the Link
    Cable page opens over it at once; the database holds gambatte for that
    game alone (core.GB.<file>), the shelf's core is still mgba.
  - Another Game Boy game still starts on mgba.
  - The first game starts on gambatte again, on its own core.
  - Its details page (X) says gambatte on its Core row; Right steps it to
    the shelf's ("mgba (Shelf's)"), the key gone, and it starts on mgba.

The cores here are Gambatte built for this computer (tools/gblink-core.sh)
under both names: the launcher's choice is what is checked (its "launch:"
line, and Diatom's dlopen), not mGBA itself. Skips without that core or
SDL2.
"""
import importlib.util, os, shutil, socket, sqlite3, subprocess, sys, tempfile, time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
_sp = importlib.util.spec_from_file_location("gblink_desk", ROOT / "tools/gblink-desk.py")
gk = importlib.util.module_from_spec(_sp)
_sp.loader.exec_module(gk)
desk, gbrom = gk.desk, gk.gbrom

fails = []


def ck(ok, what, got=None):
    print(("  ok    " if ok else "  FAIL  ") + what + ("" if ok or got is None else f"  ({got})"))
    if not ok:
        fails.append(what)


def main():
    core = None
    for ext in ("dylib", "so"):
        p = ROOT / f"build-native/gblink/gambatte_libretro.{ext}"
        if p.exists():
            core = p
    if not core:
        print("check-link-core: skipped, no Gambatte built for this computer (tools/gblink-core.sh)")
        return 0
    diatom = Path(os.environ.get("GBLINK_DIATOM") or ROOT / "diatom/build/linkcore/diatom")
    if not (ROOT / "build-native/zyperos").exists() or not diatom.exists():
        print(f"check-link-core: skipped, no build-native/zyperos or {diatom} (make check-link-core builds it)")
        return 0
    os.environ["GBLINK_DIATOM"] = str(diatom)
    tmp = Path(tempfile.mkdtemp(prefix="link-core-"))
    (tmp / "cores").mkdir()
    for name in ("gambatte", "mgba"):
        shutil.copyfile(core, tmp / f"cores/{name}_libretro.so")
    card = tmp / "pixel2"
    desk.make_card(card, "pixel2", False)
    gb = card / "Roms/Game Boy"
    gb.mkdir(parents=True, exist_ok=True)
    for f in ("Link One.gb", "Link Two.gb"):
        (gb / f).write_bytes(gbrom.rom())
    env = desk.card_env(card, None)
    env.update(ZYPEROS_DESK_FORCE="1", ZYPEROS_DESK_LINK_CABLE="1", SDL_VIDEODRIVER="dummy",
               SDL_AUDIODRIVER="dummy")
    # A host nobody answers on: nothing in this check says hello
    env.update(ZYPEROS_DESK_LINK_HOST="127.0.0.1", ZYPEROS_DESK_LINK_PORT="56397")
    dp = gk.start_diatom(card, env, tmp)
    sock = Path(tempfile.gettempdir()) / f"link-core-{os.getpid()}.sock"
    ctl = Path(tempfile.gettempdir()) / f"link-core-{os.getpid()}-ctl"
    ctl.mkdir(mode=0o700)
    (tmp / "flags").mkdir()
    env.update(ZYPEROS_DESK_SOCK=str(sock), ZYPEROS_FLAG_DIR=str(tmp / "flags"), ZYPEROS_CTL_DIR=str(ctl))
    env.pop("ZYPEROS_HEADLESS", None)
    logp = tmp / "launcher.log"
    log = open(logp, "w")
    lp = subprocess.Popen([str(ROOT / "build-native/zyperos"), "--device", "pixel2"], env=env,
                          stdout=log, stderr=subprocess.STDOUT)
    mark = [0]

    def text():
        log.flush()
        return logp.read_text(errors="replace")

    def since():
        return text()[mark[0]:]

    def here():
        mark[0] = len(text())

    def ask(line):
        s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        s.settimeout(20)
        s.connect(str(sock))
        s.sendall((line + "\n").encode())
        s.shutdown(socket.SHUT_WR)
        out = b""
        while True:
            b = s.recv(4096)
            if not b:
                break
            out += b
        return out.decode()

    def ctlcmd(line):
        s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        s.settimeout(20)
        s.connect(str(ctl / "ctl.sock"))
        s.sendall((line + "\n").encode())
        return s.makefile().readline().strip()

    def tap(key, wait=0.5):
        ask(f"tap {key} 100")
        time.sleep(wait)

    def wait_for(needle, secs):
        end = time.time() + secs
        while time.time() < end:
            if needle in since():
                return True
            time.sleep(0.1)
        return False

    def status():
        return ctlcmd("status")

    def until_shelf(secs=10):
        end = time.time() + secs
        while time.time() < end:
            if "game=" not in status() and "running" not in status():
                return True
            time.sleep(0.2)
        return False

    def db_core(key):
        with sqlite3.connect(card / "userdata/zyperos.db") as d:
            r = d.execute("SELECT value FROM settings WHERE key=?", (key,)).fetchone()
        return r[0] if r else None

    one, two = f"{gb}/Link One.gb", f"{gb}/Link Two.gb"
    try:
        for _ in range(200):
            if sock.exists():
                break
            time.sleep(0.05)
        time.sleep(2)
        here()
        ck(ctlcmd(f"open\tpath={one}").startswith("ok"), "Link One opens")
        ck(wait_for("launch: Game Boy/Link One.gb on mgba", 8), "it starts on mgba, the shelf's choice",
           since()[-300:])
        time.sleep(3)
        # MENU, Link Cable (after Quick Slot), A: the question
        tap("menu")
        for _ in range(gk.ROW_DOWNS):
            tap("down", 0.3)
        tap("a", 1)
        ask(f"shot {tmp}/ask.png")
        ck("link: " not in since() or "now runs on" not in since(), "A asks first: nothing restarted yet")
        tap("b", 1)   # B: Cancel
        ck("now runs on" not in since() and db_core("core.GB.Link One.gb") is None,
           "B on the question: Cancel, nothing kept, no restart")
        tap("a", 1)          # the question again, from the row
        tap("right", 0.3)
        here()
        tap("a", 1)          # Restart
        ck(wait_for("link: Link One.gb now runs on gambatte", 6), "Restart: Gambatte this game's own")
        ck(wait_for("launch: Game Boy/Link One.gb on gambatte", 12), "it starts again on gambatte",
           since()[-400:])
        ck(wait_for("On the USB-C cable", 10), "and the Link Cable page opens over it at once",
           since()[-400:])
        ck(db_core("core.GB.Link One.gb") == "gambatte", "the database: core.GB.Link One.gb is gambatte",
           db_core("core.GB.Link One.gb"))
        ck(db_core("core.GB") in (None, "mgba"), "the shelf's core untouched", db_core("core.GB"))
        tap("b", 1)          # the page closes, the game goes on
        ctlcmd("quit")
        until_shelf()
        here()
        ck(ctlcmd(f"open\tpath={two}").startswith("ok"), "Link Two opens")
        ck(wait_for("launch: Game Boy/Link Two.gb on mgba", 8), "another Game Boy game still starts on mgba",
           since()[-300:])
        time.sleep(2)
        ctlcmd("quit")
        until_shelf()
        here()
        ctlcmd(f"open\tpath={one}")
        ck(wait_for("launch: Game Boy/Link One.gb on gambatte", 8), "Link One starts on its own gambatte",
           since()[-300:])
        time.sleep(2)
        ctlcmd("quit")
        until_shelf()
        time.sleep(1)
        here()
        # Its details page: X on it on the shelf, the Core row first (no scrape)
        tap("x", 1)
        ask(f"shot {tmp}/details-own.png")
        tap("right", 0.6)
        ask(f"shot {tmp}/details-shelf.png")
        ck(wait_for("core: Game Boy/Link One.gb goes by its shelf", 3), "Right on its Core row: the shelf's",
           since()[-300:])
        ck(db_core("core.GB.Link One.gb") is None, "the key is gone")
        tap("x", 1)
        here()
        ctlcmd(f"open\tpath={one}")
        ck(wait_for("launch: Game Boy/Link One.gb on mgba", 8), "and it starts on the shelf's mgba again",
           since()[-300:])
        time.sleep(1)
        ctlcmd("quit")
        keep = os.environ.get("LINK_CORE_KEEP")
        if keep:
            os.makedirs(keep, exist_ok=True)
            for f in ("ask.png", "details-own.png", "details-shelf.png", "launcher.log"):
                if (tmp / f).exists():
                    shutil.copy(tmp / f, keep)
    finally:
        for p in (lp, dp):
            if p.poll() is None:
                p.terminate()
                try:
                    p.wait(5)
                except subprocess.TimeoutExpired:
                    p.kill()
        shutil.rmtree(ctl, ignore_errors=True)
        if fails:
            print(f"check-link-core: the launcher's log in {logp}")
        else:
            shutil.rmtree(tmp, ignore_errors=True)
    if fails:
        print(f"check-link-core: {len(fails)} failed")
        return 1
    print("check-link-core: the Link Cable on mGBA asks, restarts the game on Gambatte for it alone and "
          "opens its page; the game's Core row takes it back to the shelf's")
    return 0


if __name__ == "__main__":
    sys.exit(main())
