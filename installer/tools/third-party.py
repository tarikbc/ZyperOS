#!/usr/bin/env python3
"""ZyperOS Installer's THIRD-PARTY.md: what the apps are built on that is not
their own, printed from the build itself.

    cd installer && python3 tools/third-party.py > THIRD-PARTY.md

The crates come from cargo, in src-tauri, and from Cargo.lock as it is:
`cargo tree --locked -e normal`, once for each app's target, says which
crates that app is compiled from (normal dependencies, procedural macros
among them; not build or dev dependencies), and `cargo metadata --locked`
says each one's license and repository. Both ask offline first, which works
once a build or a test has put the registry on this computer, and online
when that fails. The fonts are read from ui/fonts, each named by its own
name table.

tools/release.sh runs this into a temporary file and stops when it is not
the committed THIRD-PARTY.md, so the list always names the crates of the
Cargo.lock it is released with. Standard library only.
"""
import json
import os
import re
import struct
import subprocess
import sys

INSTALLER = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CRATE = os.path.join(INSTALLER, "src-tauri")
FONTS = os.path.join(INSTALLER, "ui", "fonts")

# Each app and the targets it is compiled for. The macOS app is one app for
# Apple Silicon and Intel (tools/release.sh builds universal-apple-darwin),
# so it is both halves; today they have the same crates.
APPS = [
    ("macOS", ["aarch64-apple-darwin", "x86_64-apple-darwin"]),
    ("Windows", ["x86_64-pc-windows-msvc"]),
    ("Linux", ["x86_64-unknown-linux-gnu"]),
]


def die(msg):
    sys.stderr.write("third-party.py: %s\n" % msg)
    sys.exit(1)


def cargo(args):
    """cargo's stdout for `args`, offline if it can be, with Cargo.lock locked."""
    last = ""
    for net in (["--offline"], []):
        try:
            r = subprocess.run(["cargo"] + args + ["--locked"] + net, cwd=CRATE,
                               stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                               universal_newlines=True)
        except FileNotFoundError:
            die("no cargo here: it needs Rust (rustup) to read src-tauri")
        if r.returncode == 0:
            return r.stdout
        last = r.stderr.strip()
    die("cargo %s failed:\n%s" % (" ".join(args), last))


# ---- the crates ---------------------------------------------------------------


def crates_of(target):
    """(name, version) of every crate one target's build is made from."""
    out = cargo(["tree", "-e", "normal", "--target", target,
                 "--prefix", "none", "--format", "{p}"])
    got = set()
    for line in out.splitlines():
        # "name v1.2.3", then maybe " (proc-macro)", a path, or " (*)" for
        # one already shown
        parts = line.split()
        if len(parts) >= 2 and parts[1].startswith("v"):
            got.add((parts[0], parts[1][1:]))
    if not got:
        die("cargo tree listed nothing for %s" % target)
    return got


def semver(v):
    """A version as numbers, so 0.10 comes after 0.9, then as text."""
    return tuple(int(x) for x in re.findall(r"\d+", v.split("+")[0])), v


def cell(s):
    """Text for a Markdown table cell: one line, no column break in it."""
    return " ".join(str(s).split()).replace("|", "\\|")


def link(p):
    url = p.get("repository") or p.get("homepage")
    return url.strip() if url else "https://crates.io/crates/%s" % p["name"]


def license_of(p):
    if p.get("license"):
        return p["license"]
    if p.get("license_file"):
        return "its own, in %s" % os.path.basename(p["license_file"])
    return "none given"


# ---- the fonts ----------------------------------------------------------------


def font_names(path):
    """A TrueType or OpenType font's name table: name ID -> text, Windows
    Unicode names first, Macintosh Roman ones where a font has only those."""
    with open(path, "rb") as f:
        data = f.read()
    tables = {}
    for i in range(struct.unpack(">H", data[4:6])[0]):
        tag, _, off, length = struct.unpack(">4sIII", data[12 + 16 * i:28 + 16 * i])
        tables[tag] = (off, length)
    if b"name" not in tables:
        return {}
    base = tables[b"name"][0]
    count, strings = struct.unpack(">HH", data[base + 2:base + 6])
    names, mac = {}, {}
    for i in range(count):
        pid, _, _, nid, length, off = struct.unpack(
            ">HHHHHH", data[base + 6 + 12 * i:base + 18 + 12 * i])
        raw = data[base + strings + off:base + strings + off + length]
        if pid == 3:
            names.setdefault(nid, raw.decode("utf-16-be", "replace"))
        elif pid == 1:
            mac.setdefault(nid, raw.decode("latin-1"))
    for nid, text in mac.items():
        names.setdefault(nid, text)
    return names


def fonts():
    """Lines for each font in ui/fonts, and the license file(s) beside them."""
    files = sorted(os.listdir(FONTS))
    lines = []
    for name in files:
        if not name.lower().endswith((".ttf", ".otf")):
            continue
        n = font_names(os.path.join(FONTS, name))
        full = n.get(4) or n.get(1) or name
        m = re.match(r"Version\s+([\d.]+)", n.get(5, ""))
        what = "%s %s" % (full, m.group(1)) if m else full
        lines.append("  - `%s`: %s, %s" % (name, what, n.get(0, "no copyright given")))
    licenses = []
    for name in files:
        if name.lower().endswith((".txt", ".md")) or name.upper().startswith("LICENSE"):
            with open(os.path.join(FONTS, name), encoding="utf-8") as f:
                text = f.read()
            m = re.search(r"SIL OPEN FONT LICENSE\s+Version\s+([\d.]+)", text, re.I)
            what = "the SIL Open Font License %s" % m.group(1) if m else "the license"
            licenses.append("%s, in `ui/fonts/%s`" % (what, name))
    if not lines:
        die("no fonts in ui/fonts")
    if not licenses:
        die("the fonts in ui/fonts have no license file beside them")
    return lines, " and ".join(licenses)


# ---- the page -----------------------------------------------------------------


def main():
    meta = json.loads(cargo(["metadata", "--format-version", "1"]))
    members = set(meta["workspace_members"])
    own = {(p["name"], p["version"]) for p in meta["packages"] if p["id"] in members}
    pkgs = {(p["name"], p["version"]): p for p in meta["packages"] if p["id"] not in members}

    carried = {}                 # (name, version) -> the apps it is in
    count = {}
    for app, targets in APPS:
        got = set()
        for t in targets:
            got |= crates_of(t)
        got -= own               # zyperos-installer itself
        missing = sorted(k for k in got if k not in pkgs)
        if missing:
            die("cargo tree named crates cargo metadata does not have: %s"
                % ", ".join("%s %s" % k for k in missing))
        count[app] = len(got)
        for k in got:
            carried.setdefault(k, []).append(app)

    tauri = [p for (name, _), p in pkgs.items() if name == "tauri"]
    if not tauri:
        die("no tauri crate in Cargo.lock")
    tauri = tauri[0]
    font_lines, font_license = fonts()

    out = []
    w = out.append
    w("# ZyperOS Installer: third-party notices")
    w("")
    w("Made by `tools/third-party.py` from `src-tauri/Cargo.lock`; run it from "
      "`installer/` rather than editing this file. `tools/release.sh` stops when "
      "the two differ.")
    w("")
    w("ZyperOS Installer's own code is under the PolyForm Shield License 1.0.0: "
      "see the repository's `LICENSE` and `NOTICE`. The TortOS Installer code it "
      "started from keeps Eric Reinsmidt's MIT notice, in "
      "`LICENSES/MIT-TortOS-Installer.txt`.")
    w("")
    w("## What it is built on")
    w("")
    w("- **Tauri** %s (https://tauri.app), under %s: the framework the app is "
      "made with. Its crates are in the list below with the rest."
      % (tauri["version"], license_of(tauri)))
    w("- **The system's web view**, which draws the app's page and is not part of "
      "the app: WKWebView on macOS, WebView2 on Windows, WebKitGTK on Linux. Each "
      "comes with the operating system or from its packages, under its own "
      "license.")
    w("- **The fonts** in `ui/fonts`, under %s:" % font_license)
    out.extend(font_lines)
    w("")
    w("## Rust crates")
    w("")
    w("Every crate compiled into the apps: the normal dependencies of "
      "`src-tauri`, procedural macros among them, and not its build or "
      "development dependencies, as `cargo tree --locked -e normal` lists them "
      "for each app's target (%s). Each license and repository is the crate's "
      "own, from `cargo metadata`."
      % "; ".join("%s: %s" % (app, ", ".join(t)) for app, t in APPS))
    w("")
    w("%d crates: %s." % (len(carried), ", ".join(
        "%d in the %s app" % (count[app], app) for app, _ in APPS)))
    w("")
    w("| Crate | Version | License | Repository | Apps |")
    w("|---|---|---|---|---|")
    order = [app for app, _ in APPS]
    for name, version in sorted(carried, key=lambda k: (k[0].lower(), k[0], semver(k[1]))):
        p = pkgs[(name, version)]
        apps = sorted(carried[(name, version)], key=order.index)
        w("| %s | %s | %s | %s | %s |" % (cell(name), cell(version), cell(license_of(p)),
                                          cell(link(p)), ", ".join(apps)))
    sys.stdout.write("\n".join(out) + "\n")


main()
