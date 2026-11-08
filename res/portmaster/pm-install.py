#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""pm-install: puts the official PortMaster on the card, for the Ports shelf.

    python3 -I pm-install.py --ports /mnt/SDCARD/Roms/Ports

The launcher (src/main.c, pm_install_screen) starts it when PortMaster is not
on the card and the player asks for it, and draws what it writes to the
status file (src/pminstall.h reads it). Nothing is drawn here.

What it does, as PortMaster's own Install.PortMaster.sh does on a system it
does not know (tools/installer.sh in PortsMaster/PortMaster-GUI), with the
download added:

1. Reads version.json from PortMaster's latest GitHub release: the stable
   release's version, PortMaster.zip's address and its MD5. Stable, as
   PortMaster's installers and ROCKNIX ship; PortMaster then updates itself
   on the channel that release names.
2. Asks GitHub's API for that release's PortMaster.zip: its size and the
   SHA-256 GitHub keeps for it. When the API does not answer (it allows 60
   requests an hour from one address), the MD5 alone is checked.
3. Downloads it over HTTPS, checked against the system's root certificates,
   into a folder of its own under Roms/Ports, and checks the size, the
   SHA-256 and the MD5. A download that matches none of what was published is
   thrown away, never unpacked.
4. Unpacks it there, beside the Ports, and only then moves PortMaster/ into
   place: a cut-off install never leaves half a PortMaster where the shelf
   would run it. A PortMaster/ already there without its program (a copied
   folder of runtimes) keeps its runtimes, libs/*.squashfs, as PortMaster's
   own installer keeps them.
5. Puts PortMaster.sh at the top of Roms/Ports, where the Ports shelf lists
   it, as the generic installer does (it copies; PortMaster/ keeps its own).

PortMaster's control.txt and its per-system files for ZyperOS are not
written here: launch.sh copies them in before every port, PortMaster's own
window among them, because PortMaster's updates write their own.

The status file, a "key value" line each, replaced whole as it changes:

    state checking|downloading|verifying|unpacking|done|error
    version 2026.06.23-0015      the release, once known
    total N                      bytes: the download, or what unpacks
    done N                       of them so far
    error network|download|checksum|space|unpack|release|internal
    need N                       with error space: the bytes it needed free
    time T                       unix seconds

Exit status: 0 installed (or already there), 2 failed (the status file says
why), 3 another copy is running.
"""
import argparse
import contextlib
import fcntl
import hashlib
import json
import os
import shutil
import signal
import ssl
import sys
import time
import urllib.error
import urllib.request
import zipfile

SOURCE = "https://github.com/PortsMaster/PortMaster-GUI/releases/latest/download/version.json"
API = "https://api.github.com/repos/PortsMaster/PortMaster-GUI/releases/tags/"
# Where a stable release's zip may come from: PortMaster's own releases. A
# version.json naming anywhere else is not followed (unless --source was
# given, for the checks).
RELEASES = "https://github.com/PortsMaster/PortMaster-GUI/releases/download/"
ZIP_NAME = "PortMaster.zip"
CACERT = "/etc/ssl/certs/ca-certificates.crt"
STAGE = ".zyperos-portmaster"       # under Roms/Ports: dot folders are no port
# Room on the card beyond the zip and what it unpacks to: PortMaster unpacks
# its own pylibs.zip (11 MB, about 30 once out) the first time it runs.
MARGIN = 64 << 20
TIMEOUT = 30                        # seconds without a byte: the network is gone
AGENT = "ZyperOS-PortMaster-installer"

status = {}
status_path = None
last_write = 0.0


def log(msg):
    print("pm-install: " + msg, file=sys.stderr, flush=True)


def write_status(force=False, **kv):
    """The status file, replaced whole, at most five times a second unless
    the state changes: the launcher reads it four times a second."""
    global last_write
    status.update(kv)
    status["time"] = int(time.time())
    now = time.monotonic()
    if not force and now - last_write < 0.2:
        return
    last_write = now
    tmp = status_path + ".tmp"
    with open(tmp, "w") as f:
        for k in ("state", "version", "total", "done", "error", "need", "time"):
            if k in status:
                f.write("%s %s\n" % (k, status[k]))
    os.replace(tmp, status_path)


def state(name, **kv):
    for k in ("total", "done", "error", "need"):
        status.pop(k, None)
    write_status(force=True, state=name, **kv)


class Failed(Exception):
    def __init__(self, code, detail, need=None):
        super().__init__(detail)
        self.code, self.need = code, need


class Opener:
    """HTTPS only, against the system's root certificates, redirects
    included (GitHub's downloads redirect to its object store). --source
    lets a check serve plain http or file:// instead."""

    def __init__(self, cacert, testing):
        if testing:
            ctx = ssl.create_default_context()
        elif os.path.exists(cacert):
            ctx = ssl.create_default_context(cafile=cacert)
        else:
            ctx = ssl.create_default_context()
        self.testing = testing
        parent = self

        class Redirect(urllib.request.HTTPRedirectHandler):
            def redirect_request(self, req, fp, code, msg, headers, newurl):
                if not parent.testing and not newurl.startswith("https://"):
                    raise Failed("download", "redirected off HTTPS: " + newurl)
                return super().redirect_request(req, fp, code, msg, headers, newurl)

        self.opener = urllib.request.build_opener(
            urllib.request.HTTPSHandler(context=ctx), Redirect)

    def open(self, url, accept=None):
        if not self.testing and not url.startswith("https://"):
            raise Failed("release", "not HTTPS: " + url)
        req = urllib.request.Request(url, headers={"User-Agent": AGENT})
        if accept:
            req.add_header("Accept", accept)
        return self.opener.open(req, timeout=TIMEOUT)

    def json(self, url, accept=None, code="network"):
        try:
            with self.open(url, accept) as r:
                return json.loads(r.read(1 << 20).decode("utf-8"))
        except Failed:
            raise
        except (urllib.error.URLError, OSError) as e:
            raise Failed(code, "%s: %s" % (url, e))
        except ValueError as e:
            raise Failed("release", "%s: not JSON: %s" % (url, e))


def release_info(op, source, api, channel, testing):
    """The release to install: version, zip address, MD5, and the size and
    SHA-256 when GitHub's API tells them."""
    data = op.json(source)
    rel = data.get(channel) if isinstance(data, dict) else None
    if not isinstance(rel, dict):
        raise Failed("release", "version.json has no %s release" % channel)
    version, url, md5 = rel.get("version"), rel.get("url"), rel.get("md5")
    if not (isinstance(version, str) and isinstance(url, str) and isinstance(md5, str)):
        raise Failed("release", "version.json's %s release lacks a version, url or md5" % channel)
    version, md5 = version.strip(), md5.strip().lower()
    if not version or len(version) > 31 or any(c.isspace() for c in version):
        raise Failed("release", "an odd version: %r" % version)
    if len(md5) != 32 or any(c not in "0123456789abcdef" for c in md5):
        raise Failed("release", "an odd md5: %r" % md5)
    if not testing and not (url.startswith(RELEASES) and url.endswith("/" + ZIP_NAME)):
        raise Failed("release", "the zip is not PortMaster's own: " + url)
    info = {"version": version, "url": url, "md5": md5, "size": None, "sha256": None}
    if api:
        try:
            tag = op.json(api + urllib.request.quote(version), "application/vnd.github+json")
            for a in tag.get("assets", []) if isinstance(tag, dict) else []:
                if a.get("name") != ZIP_NAME:
                    continue
                if isinstance(a.get("size"), int) and a["size"] > 0:
                    info["size"] = a["size"]
                d = a.get("digest")
                if isinstance(d, str) and d.startswith("sha256:") and len(d) == 71:
                    info["sha256"] = d[7:].lower()
        except Failed as e:
            log("GitHub's API did not answer (%s): the MD5 alone is checked" % e)
    log("PortMaster %s, %s, md5 %s, sha256 %s, %s bytes" %
        (version, url, md5, info["sha256"] or "unknown", info["size"] or "unknown"))
    return info


def free_bytes(path):
    return shutil.disk_usage(path).free


def download(op, info, dest):
    """The zip, into `dest`, with the size, SHA-256 and MD5 checked."""
    md5, sha = hashlib.md5(), hashlib.sha256()
    got = 0
    try:
        with op.open(info["url"]) as r, open(dest, "wb") as f:
            length = r.headers.get("Content-Length")
            length = int(length) if length and length.isdigit() else None
            total = info["size"] or length
            if info["size"] and length and length != info["size"]:
                raise Failed("checksum", "the server sends %d bytes, the release has %d" %
                             (length, info["size"]))
            if total:
                need = total + MARGIN
                if free_bytes(os.path.dirname(dest)) < need:
                    raise Failed("space", "not room for the download", need)
            state("downloading", version=info["version"], total=total or 0, done=0)
            while True:
                chunk = r.read(64 << 10)
                if not chunk:
                    break
                f.write(chunk)
                md5.update(chunk)
                sha.update(chunk)
                got += len(chunk)
                if total and got > total:
                    raise Failed("checksum", "more than the %d bytes published" % total)
                write_status(done=got)
            f.flush()
            os.fsync(f.fileno())
    except Failed:
        raise
    except (urllib.error.URLError, OSError) as e:
        raise Failed("download", "%s after %d bytes: %s" % (info["url"], got, e))
    write_status(force=True, done=got)
    state("verifying", version=info["version"])
    want = info["size"] or length
    if want and got != want:
        raise Failed("download", "%d of %d bytes" % (got, want))
    if info["sha256"] and sha.hexdigest() != info["sha256"]:
        raise Failed("checksum", "sha256 %s, published %s" % (sha.hexdigest(), info["sha256"]))
    if md5.hexdigest() != info["md5"]:
        raise Failed("checksum", "md5 %s, published %s" % (md5.hexdigest(), info["md5"]))
    log("downloaded %d bytes, %s" % (got, "sha256 and md5 match" if info["sha256"] else "md5 matches"))


def safe_member(name):
    """A zip member's path as parts under PortMaster/, or None when it would
    land anywhere else (absolute, .., or outside PortMaster/)."""
    if "\\" in name or name.startswith("/") or "\0" in name:
        return None
    parts = [p for p in name.split("/") if p]
    if not parts or parts[0] != "PortMaster" or any(p in (".", "..") for p in parts):
        return None
    return parts


def unpack(zpath, into, version):
    """PortMaster.zip into `into`/PortMaster, every member checked first."""
    try:
        z = zipfile.ZipFile(zpath)
    except (zipfile.BadZipFile, OSError) as e:
        raise Failed("unpack", "not a zip: %s" % e)
    with z:
        members = z.infolist()
        for m in members:
            if safe_member(m.filename) is None:
                raise Failed("unpack", "a member outside PortMaster/: %r" % m.filename)
        if not any(m.filename == "PortMaster/pugwash" for m in members):
            raise Failed("unpack", "no PortMaster/pugwash in the zip")
        total = sum(m.file_size for m in members)
        need = total + MARGIN
        if free_bytes(into) < need:
            raise Failed("space", "not room to unpack %d bytes" % total, need)
        state("unpacking", version=version, total=total, done=0)
        done = 0
        for m in members:
            path = os.path.join(into, *safe_member(m.filename))
            if m.is_dir():
                os.makedirs(path, exist_ok=True)
                continue
            os.makedirs(os.path.dirname(path), exist_ok=True)
            try:
                with z.open(m) as src, open(path, "wb") as dst:
                    while True:
                        chunk = src.read(256 << 10)
                        if not chunk:
                            break
                        dst.write(chunk)
                        done += len(chunk)
                        write_status(done=done)
            except (zipfile.BadZipFile, OSError, RuntimeError) as e:
                raise Failed("unpack", "%s: %s" % (m.filename, e))
        write_status(force=True, done=done)
    log("unpacked %d files, %d bytes" % (sum(1 for m in members if not m.is_dir()), total))


def remove_tree(path):
    if os.path.isdir(path) and not os.path.islink(path):
        shutil.rmtree(path, ignore_errors=True)
    elif os.path.lexists(path):
        with contextlib.suppress(OSError):
            os.unlink(path)


def put_in_place(ports, stage):
    """The unpacked PortMaster/ where the shelf finds it, keeping the
    runtimes of a PortMaster/ that was there, and PortMaster.sh beside it."""
    new = os.path.join(stage, "unpack", "PortMaster")
    live = os.path.join(ports, "PortMaster")
    old = os.path.join(stage, "old")
    try:
        if os.path.isdir(live):
            libs, keep = os.path.join(live, "libs"), os.path.join(new, "libs")
            if os.path.isdir(libs):
                os.makedirs(keep, exist_ok=True)
                for n in os.listdir(libs):
                    if n.lower().endswith(".squashfs") and not os.path.exists(os.path.join(keep, n)):
                        os.replace(os.path.join(libs, n), os.path.join(keep, n))
                        log("kept the runtime " + n)
            remove_tree(old)
            os.replace(live, old)
        os.replace(new, live)
        launcher(ports)
    except OSError as e:
        raise Failed("unpack", "putting PortMaster in place: %s" % e)
    remove_tree(old)


def launcher(ports):
    """PortMaster.sh at the top of Roms/Ports, written whole then renamed."""
    src = os.path.join(ports, "PortMaster", "PortMaster.sh")
    dst = os.path.join(ports, "PortMaster.sh")
    shutil.copyfile(src, dst + ".tmp")
    os.replace(dst + ".tmp", dst)
    with contextlib.suppress(OSError):
        os.chmod(dst, 0o755)


def installed(ports):
    return os.path.isfile(os.path.join(ports, "PortMaster", "pugwash"))


def version_of(ports):
    with contextlib.suppress(OSError):
        with open(os.path.join(ports, "PortMaster", "version")) as f:
            return f.read(64).strip() or "unknown"
    return "unknown"


def run(args):
    ports = args.ports
    testing = args.source is not None
    if not os.path.isdir(ports):
        raise Failed("unpack", "no Ports folder at " + ports)
    stage = os.path.join(ports, STAGE)
    # Whatever a cut-off run left: its download, its half-unpacked folder
    remove_tree(stage)
    if installed(ports):
        # The folder is whole (a card that has PortMaster/ and lost the
        # script, or a second press): only the script is put back
        if not os.path.isfile(os.path.join(ports, "PortMaster.sh")):
            try:
                launcher(ports)
            except OSError as e:
                raise Failed("unpack", "PortMaster.sh: %s" % e)
            log("PortMaster was on the card; PortMaster.sh put back")
        state("done", version=version_of(ports))
        return
    state("checking")
    op = Opener(args.cacert, testing)
    info = release_info(op, args.source or SOURCE, args.api if testing else API,
                        args.channel, testing)
    os.makedirs(os.path.join(stage, "unpack"), exist_ok=True)
    zpath = os.path.join(stage, ZIP_NAME)
    download(op, info, zpath)
    unpack(zpath, os.path.join(stage, "unpack"), info["version"])
    with contextlib.suppress(OSError):
        os.unlink(zpath)
    put_in_place(ports, stage)
    remove_tree(stage)
    log("PortMaster %s installed in %s" % (info["version"], ports))
    state("done", version=info["version"])


def main():
    global status_path
    ap = argparse.ArgumentParser(description="Install the official PortMaster.")
    ap.add_argument("--ports", required=True, help="the Ports folder, Roms/Ports")
    ap.add_argument("--status", default="/tmp/zyperos-pm/status")
    ap.add_argument("--cacert", default=CACERT)
    ap.add_argument("--channel", default="stable", choices=("stable", "beta", "alpha"))
    # For the checks (tools/pminstall-run.sh): a version.json and a tag API
    # served from elsewhere, plain http and file:// allowed, any zip address
    ap.add_argument("--source", default=None)
    ap.add_argument("--api", default="")
    args = ap.parse_args()

    status_path = args.status
    os.makedirs(os.path.dirname(status_path) or ".", exist_ok=True)
    lock = open(os.path.join(os.path.dirname(status_path) or ".", "lock"), "w")
    # The page asks whether a run holds this lock (a shared lock taken and
    # let go at once, four times a second): two seconds are room for that
    # moment, and still a quick no to a second run
    for _ in range(40):
        try:
            fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
            break
        except OSError:
            time.sleep(0.05)
    else:
        log("another install is running")
        return 3
    # The launcher may be gone (a game, a port) and come back; a TERM from
    # launch.sh at power-off ends the run, and the next one starts clean
    signal.signal(signal.SIGTERM, lambda *_: sys.exit(2))
    try:
        run(args)
        return 0
    except Failed as e:
        log("failed (%s): %s" % (e.code, e))
        kv = {"error": e.code}
        if e.need:
            kv["need"] = e.need
        version = status.get("version")
        state("error", **({"version": version} if version else {}), **kv)
        remove_tree(os.path.join(args.ports, STAGE))
        return 2
    except Exception as e:      # noqa: BLE001 - said in the status, not a trace on screen
        log("failed: %r" % e)
        state("error", error="internal")
        remove_tree(os.path.join(args.ports, STAGE))
        return 2


if __name__ == "__main__":
    sys.exit(main())
