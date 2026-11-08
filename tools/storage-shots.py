#!/usr/bin/env python3
"""MENU > Storage's screens, every state in both themes and every language.

    make native
    python3 tools/storage-shots.py [--out DIR] [--lang en,fr|all] [--theme light,dark] [NAME ...]

Writes DIR/<theme>/<lang>/<name>.png (default build/storage-shots) from the
launcher's own --storage shots (src/main.c, storage_shot) on the README's
demonstration card (tools/readme-shots.py): the page on its stand-in card of
128 GB nearly full, a system's page, the two recommendations' questions,
Delete Game's question from a system's page, the page after Unused Art, and
the first walk. For looking at, and for the README's and the site's shot:
the checks that hold what is drawn are check-storage (the numbers) and
check-i18n-shots (what does not fit, in every language).
"""
import argparse
import importlib.util
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import i18n  # noqa: E402

_spec = importlib.util.spec_from_file_location("readme_shots", ROOT / "tools" / "readme-shots.py")
R = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(R)

SHOTS = {
    "main": ["main"],
    "system": ["system"],
    "art": ["art"],
    "states": ["states"],
    "delete": ["delete"],
    "done": ["done"],
    "first": ["first"],
    "zyperos": ["main", "9"],
}


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--out", type=Path, default=ROOT / "build/storage-shots")
    ap.add_argument("--lang", default="en", help="language codes with commas, or all (xx is the pseudo-locale)")
    ap.add_argument("--theme", default="light,dark")
    ap.add_argument("only", nargs="*")
    a = ap.parse_args()
    langs = i18n.CODES if a.lang == "all" else a.lang.split(",")
    a.theme = a.theme.split(",")
    binary = ROOT / "build-native/zyperos"
    if not binary.is_file():
        ap.error("run make native first")
    bad = 0
    with tempfile.TemporaryDirectory(prefix="zyperos-storage-") as t:
        card = Path(t) / "demo"
        R.demo_card(card, None, {"artwork": {}})
        (card / "ZyperOS/.last").write_text("PS1\n" + R.FOCUS + ".chd\n")
        for theme in a.theme:
            for lang in langs:
                dest = a.out.resolve() / theme / lang
                dest.mkdir(parents=True, exist_ok=True)
                for name, flags in SHOTS.items():
                    if a.only and name not in a.only:
                        continue
                    target = dest / (name + ".png")
                    cmd = [str(binary), "--shot", str(target), "--theme", theme, "--lang", lang,
                           "--screen", "systems", "--status", "10:08 24h wifi=off batt=82",
                           "--storage", *flags]
                    r = subprocess.run(cmd, cwd=ROOT, env=R.env_for(card), text=True,
                                       stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
                    (dest / (name + ".log")).write_text(r.stdout)
                    cut = [l for l in r.stdout.splitlines() if l.startswith("i18n:")]
                    if r.returncode or not target.is_file():
                        print(f"FAIL {theme}/{lang}/{name}: {r.stdout[-400:]}")
                        bad += 1
                    elif cut:
                        print(f"{theme}/{lang}/{name}: " + "; ".join(cut))
                    else:
                        print(f"{theme}/{lang}/{name}")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
