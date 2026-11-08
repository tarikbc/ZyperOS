#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""check-pmhelper: res/portmaster/pm-install.py against a made-up PortMaster
release served from a folder (file:// addresses, which the installer takes
only with --source, as it does in these checks alone). No network, no device.

Each case starts from an empty Ports folder of its own:

- an install: PortMaster/ and PortMaster.sh in place, the status "done",
  nothing left of the staging folder
- a second run: done at once, nothing fetched
- PortMaster/ whole without its script: only the script put back
- an MD5, a SHA-256 or a size that does not match what is published: an
  error, nothing installed, nothing left behind
- a zip with a member outside PortMaster/: refused before anything is written
- a version.json that makes no sense, and one that is not there
- a PortMaster/ already there without its program keeps its runtimes
- a run while another holds the lock: turned away
"""
import fcntl
import hashlib
import json
import os
import shutil
import subprocess
import sys
import tempfile
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
HELPER = os.path.join(HERE, "..", "res", "portmaster", "pm-install.py")
VERSION = "2026.06.23-0015"

fails = 0


def check(cond, what):
    global fails
    if not cond:
        print("  FAIL " + what)
        fails += 1


def make_zip(path, members):
    with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED) as z:
        for name, data in members.items():
            if name.endswith("/"):
                z.writestr(zipfile.ZipInfo(name), "")
            else:
                z.writestr(name, data)


GOOD = {
    "PortMaster/": "",
    "PortMaster/pugwash": "#!/usr/bin/env python3\nprint('pugwash')\n",
    "PortMaster/PortMaster.sh": "#!/bin/bash\necho PortMaster\n",
    "PortMaster/version": VERSION + "\n",
    "PortMaster/libs/.gitkeep": "",
    "PortMaster/control.txt": "# PortMaster's own\n" * 200,
}


class Release:
    """version.json, the tag's API answer and PortMaster.zip, in a folder"""

    def __init__(self, root, members=GOOD, md5=None, sha=None, size=None, vjson=None):
        self.dir = os.path.join(root, "release")
        os.makedirs(os.path.join(self.dir, "api"), exist_ok=True)
        zpath = os.path.join(self.dir, "PortMaster.zip")
        make_zip(zpath, members)
        data = open(zpath, "rb").read()
        url = "file://" + zpath
        rel = {"version": VERSION, "url": url, "md5": md5 or hashlib.md5(data).hexdigest()}
        with open(os.path.join(self.dir, "version.json"), "w") as f:
            f.write(vjson if vjson is not None else
                    json.dumps({"stable": rel, "beta": rel, "alpha": rel}))
        tag = {"tag_name": VERSION, "assets": [
            {"name": "PortMaster.zip.md5", "size": 33, "digest": "sha256:" + "0" * 64},
            {"name": "PortMaster.zip", "size": size or len(data),
             "digest": "sha256:" + (sha or hashlib.sha256(data).hexdigest())}]}
        with open(os.path.join(self.dir, "api", VERSION), "w") as f:
            json.dump(tag, f)
        self.source = "file://" + os.path.join(self.dir, "version.json")
        self.api = "file://" + os.path.join(self.dir, "api") + "/"


def run(root, rel, ports=None, source=None):
    ports = ports or os.path.join(root, "Ports")
    os.makedirs(ports, exist_ok=True)
    status = os.path.join(root, "pm", "status")
    p = subprocess.run([sys.executable, "-I", HELPER, "--ports", ports, "--status", status,
                        "--source", source or rel.source, "--api", rel.api],
                       capture_output=True, text=True, timeout=60)
    st = {}
    if os.path.exists(status):
        for line in open(status):
            k, _, v = line.rstrip("\n").partition(" ")
            st[k] = v
    return p.returncode, st, p.stderr


def nothing_installed(ports):
    return (not os.path.exists(os.path.join(ports, "PortMaster")) and
            not os.path.exists(os.path.join(ports, "PortMaster.sh")) and
            not os.path.exists(os.path.join(ports, ".zyperos-portmaster")))


def case(name):
    root = tempfile.mkdtemp(prefix="pmhelper-")
    return root


def main():
    print("check-pmhelper:")

    # An install
    root = case("install")
    rel = Release(root)
    rc, st, err = run(root, rel)
    ports = os.path.join(root, "Ports")
    check(rc == 0 and st.get("state") == "done" and st.get("version") == VERSION,
          "an install ends done, with the version: rc %s %s %s" % (rc, st, err[-300:]))
    check(os.path.isfile(os.path.join(ports, "PortMaster", "pugwash")), "PortMaster/pugwash in place")
    check(open(os.path.join(ports, "PortMaster.sh")).read() == GOOD["PortMaster/PortMaster.sh"],
          "PortMaster.sh at the top of Ports, PortMaster's own")
    check(os.path.isfile(os.path.join(ports, "PortMaster", "PortMaster.sh")),
          "and PortMaster/ keeps its own copy")
    check(not os.path.exists(os.path.join(ports, ".zyperos-portmaster")), "no staging folder left")
    check("sha256 and md5 match" in err, "the SHA-256 and the MD5 were both checked")

    # A second run: done at once, nothing fetched (the release is gone)
    shutil.rmtree(rel.dir)
    rc, st, err = run(root, rel)
    check(rc == 0 and st.get("state") == "done", "a second run is done at once")

    # The script lost, the folder whole: only the script put back
    os.unlink(os.path.join(ports, "PortMaster.sh"))
    rc, st, err = run(root, rel)
    check(rc == 0 and st.get("state") == "done" and
          os.path.isfile(os.path.join(ports, "PortMaster.sh")), "a lost PortMaster.sh is put back")
    shutil.rmtree(root)

    # What was published does not match
    for what, kw in (("an MD5", {"md5": "0" * 32}), ("a SHA-256", {"sha": "1" * 64}),
                     ("a size", {"size": 12345})):
        root = case(what)
        rel = Release(root, **kw)
        rc, st, err = run(root, rel)
        ports = os.path.join(root, "Ports")
        check(rc == 2 and st.get("state") == "error" and st.get("error") == "checksum",
              "%s that does not match is a checksum error: %s %s" % (what, rc, st))
        check(nothing_installed(ports), "%s that does not match installs nothing" % what)
        shutil.rmtree(root)

    # A member outside PortMaster/
    for bad in ("evil.sh", "PortMaster/../evil.sh", "/etc/evil"):
        root = case("member")
        members = dict(GOOD)
        members[bad] = "rm -rf /\n"
        rel = Release(root, members=members)
        rc, st, err = run(root, rel)
        ports = os.path.join(root, "Ports")
        check(rc == 2 and st.get("error") == "unpack", "a member %r is refused: %s %s" % (bad, rc, st))
        check(nothing_installed(ports) and not os.path.exists(os.path.join(root, "evil.sh")),
              "and nothing of it is written (%r)" % bad)
        shutil.rmtree(root)

    # No program in the zip
    root = case("noprog")
    members = {k: v for k, v in GOOD.items() if k != "PortMaster/pugwash"}
    rel = Release(root, members=members)
    rc, st, err = run(root, rel)
    check(rc == 2 and st.get("error") == "unpack" and nothing_installed(os.path.join(root, "Ports")),
          "a zip without PortMaster's program is refused")
    shutil.rmtree(root)

    # A release list that makes no sense, and one that is not there
    for what, vjson, code in (("not JSON", "<html>", "release"),
                              ("no stable", json.dumps({"beta": {}}), "release"),
                              ("an odd md5", json.dumps({"stable": {"version": VERSION,
                               "url": "file:///x/PortMaster.zip", "md5": "nope"}}), "release")):
        root = case(what)
        rel = Release(root, vjson=vjson)
        rc, st, err = run(root, rel)
        check(rc == 2 and st.get("error") == code, "a version.json %s is a %s error: %s" % (what, code, st))
        shutil.rmtree(root)
    root = case("missing")
    rel = Release(root)
    rc, st, err = run(root, rel, source="file://" + os.path.join(root, "nothing.json"))
    check(rc == 2 and st.get("error") == "network", "no version.json is a network error: %s" % st)
    shutil.rmtree(root)

    # A PortMaster/ without its program: its runtimes are kept
    root = case("runtimes")
    rel = Release(root)
    ports = os.path.join(root, "Ports")
    os.makedirs(os.path.join(ports, "PortMaster", "libs"))
    with open(os.path.join(ports, "PortMaster", "libs", "mono-6.12.squashfs"), "w") as f:
        f.write("a runtime")
    with open(os.path.join(ports, "PortMaster", "stale.txt"), "w") as f:
        f.write("old")
    rc, st, err = run(root, rel)
    check(rc == 0 and st.get("state") == "done", "an install over a folder of runtimes: %s" % st)
    check(open(os.path.join(ports, "PortMaster", "libs", "mono-6.12.squashfs")).read() == "a runtime",
          "its runtime kept")
    check(not os.path.exists(os.path.join(ports, "PortMaster", "stale.txt")),
          "and the rest of the old folder gone")
    shutil.rmtree(root)

    # Another run holds the lock
    root = case("lock")
    rel = Release(root)
    os.makedirs(os.path.join(root, "pm"))
    with open(os.path.join(root, "pm", "lock"), "w") as held:
        fcntl.flock(held, fcntl.LOCK_EX)
        rc, st, err = run(root, rel)
    check(rc == 3 and nothing_installed(os.path.join(root, "Ports")), "a second run at once is turned away")
    shutil.rmtree(root)

    if fails:
        print("check-pmhelper: %d failure(s)" % fails)
        return 1
    print("check-pmhelper: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
