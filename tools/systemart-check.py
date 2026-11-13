#!/usr/bin/env python3
"""Focused checks of tools/systemart.py, the launcher's system art. Requires
Pillow; no SDL or launcher build (the C contract needs a C compiler)."""
import importlib.util
import io
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

from PIL import Image

sys.dont_write_bytecode = True
SPEC = importlib.util.spec_from_file_location("systemart", Path(__file__).with_name("systemart.py"))
sa = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = sa
SPEC.loader.exec_module(sa)
CELL = sa.CELL


def export(directory, key, poses, frames, bounds=None, still=None):
    """A design as tools/systemart-original-pack.py writes it: `poses` are
    256-pixel RGBA images, `frames` (tile, ms) pairs"""
    directory.mkdir(parents=True, exist_ok=True)
    cols = min(8, len(poses))
    rows = -(-len(poses) // cols)
    atlas = Image.new("RGBA", (cols * CELL, rows * CELL))
    for t, pose in enumerate(poses):
        atlas.paste(pose, (t % cols * CELL, t // cols * CELL))
    atlas.save(directory / f"{key}.png")
    (still or poses[0]).save(directory / f"{key}-still.png")
    meta = {"key": key, "cell": CELL, "cols": cols, "rows": rows, "unique": len(poses),
            "duration_ms": sum(ms for _, ms in frames),
            "frames": [{"tile": t, "ms": ms} for t, ms in frames]}
    if bounds:
        meta["motion_bounds"] = list(bounds)
    (directory / f"{key}.json").write_text(json.dumps(meta))


def pose(box=(40, 40, 200, 200), color=(200, 60, 60, 255), mark=None):
    """A square on a clear canvas, with a small `mark` (x, y, w, h, color)"""
    image = Image.new("RGBA", (CELL, CELL))
    image.paste(color, box)
    if mark:
        x, y, w, h, c = mark
        image.paste(c, (x, y, x + w, y + h))
    return image


def compose(asset, tile):
    """Pose `tile` as the launcher draws it: the base, its region rewritten"""
    with Image.open(io.BytesIO(asset.atlas)) as atlas:
        atlas = atlas.convert("RGBA")
        image = atlas.crop((0, 0, CELL, CELL))
        rx, ry, rw, rh = asset.region
        if rw:
            x, y = tile % asset.cols * rw, CELL + tile // asset.cols * rh
            image.paste(atlas.crop((x, y, x + rw, y + rh)), (rx, ry))
        return image


class CompilerTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix="systemart-check-")
        self.dir = Path(self.tmp.name)

    def tearDown(self):
        self.tmp.cleanup()

    def test_region_is_where_poses_differ_and_poses_rebuild_exactly(self):
        poses = [pose(mark=(100, 60, 10, 8, (0, 0, 0, 255))),
                 pose(mark=(104, 64, 10, 8, (0, 0, 0, 255))),
                 pose(mark=(100, 60, 14, 12, (20, 200, 20, 128)))]
        export(self.dir, "GB", poses, [(0, 40), (1, 70), (0, 110), (2, 30)], bounds=(90, 50, 40, 40))
        asset = sa.compile_asset(self.dir, "GB")
        self.assertEqual(asset.region, (100, 60, 14, 12))
        self.assertEqual(asset.frames, ((0, 40), (1, 70), (0, 110), (2, 30)))
        self.assertEqual(asset.duration_ms, 250)
        for t, expected in enumerate(poses):
            self.assertEqual(compose(asset, t).tobytes(), sa.canonical(expected).tobytes(), f"pose {t}")

    def test_the_loop_starts_on_the_still_s_frame(self):
        poses = [pose(), pose(mark=(100, 60, 4, 4, (0, 0, 0, 255))), pose(mark=(100, 64, 4, 4, (0, 0, 0, 255)))]
        export(self.dir, "GB", poses, [(0, 100), (1, 200), (2, 300)])
        meta = json.loads((self.dir / "GB.json").read_text())
        for still_at, frames in ((0, ((0, 100), (1, 200), (2, 300))), (150, ((1, 200), (2, 300), (0, 100))),
                                 (300, ((2, 300), (0, 100), (1, 200)))):
            with self.subTest(still_at=still_at):
                meta["still_time_ms"] = still_at
                (self.dir / "GB.json").write_text(json.dumps(meta))
                self.assertEqual(sa.compile_asset(self.dir, "GB").frames, frames)
        meta["still_time_ms"] = 600
        (self.dir / "GB.json").write_text(json.dumps(meta))
        with self.assertRaises(sa.ArtError):
            sa.compile_asset(self.dir, "GB")

    def test_hidden_color_does_not_widen_the_region(self):
        first = pose(mark=(100, 60, 4, 4, (0, 0, 0, 255)))
        second = pose(mark=(100, 64, 4, 4, (0, 0, 0, 255)))
        second.paste((255, 0, 255, 0), (0, 0, 20, 20))     # invisible, another color
        export(self.dir, "GB", [first, second], [(0, 400), (1, 400)])
        self.assertEqual(sa.compile_asset(self.dir, "GB").region, (100, 60, 4, 8))

    def test_a_change_outside_the_motion_bounds_fails(self):
        export(self.dir, "GB", [pose(), pose(mark=(150, 150, 4, 4, (0, 0, 0, 255)))],
               [(0, 400), (1, 400)], bounds=(0, 0, 100, 100))
        with self.assertRaises(sa.ArtError):
            sa.compile_asset(self.dir, "GB")

    def test_a_design_that_does_not_move_is_one_pose(self):
        export(self.dir, "FOLDER_SMALL", [pose()], [(0, 1000)])
        asset = sa.compile_asset(self.dir, "FOLDER_SMALL")
        self.assertEqual((asset.region, asset.frames, asset.duration_ms, asset.cols), ((0, 0, 0, 0), ((0, 0),), 0, 0))
        with Image.open(io.BytesIO(asset.atlas)) as atlas:
            self.assertEqual(atlas.size, (CELL, CELL))

    def test_still_is_128_and_keeps_its_colors(self):
        export(self.dir, "GB", [pose(), pose(mark=(100, 60, 4, 4, (0, 0, 0, 255)))], [(0, 400), (1, 400)])
        asset = sa.compile_asset(self.dir, "GB")
        with Image.open(io.BytesIO(asset.still)) as still:
            self.assertEqual(still.size, (sa.STILL, sa.STILL))
            self.assertEqual(still.convert("RGBA").getpixel((64, 64)), (200, 60, 60, 255))
            self.assertEqual(still.convert("RGBA").getpixel((2, 2))[3], 0)

    def test_invalid_exports_fail(self):
        good = [pose(), pose(mark=(100, 60, 4, 4, (0, 0, 0, 255)))]
        cases = {
            "unknown tile": ([(0, 400), (2, 400)], good),
            "too short": ([(0, 5), (1, 400)], good),
            "pose never shown": ([(0, 400), (0, 400)], good),
            "all poses alike": ([(0, 400), (1, 400)], [pose(), pose()]),
            "no clear ground": ([(0, 400), (1, 400)], [Image.new("RGBA", (CELL, CELL), (1, 2, 3, 255)), pose()]),
            "empty first pose": ([(0, 400), (1, 400)], [Image.new("RGBA", (CELL, CELL)), pose()]),
        }
        for name, (frames, poses) in cases.items():
            with self.subTest(name):
                export(self.dir, "GB", poses, frames)
                with self.assertRaises(sa.ArtError):
                    sa.compile_asset(self.dir, "GB")
        export(self.dir, "GB", good, [(0, 400), (1, 400)])
        meta = json.loads((self.dir / "GB.json").read_text())
        meta["duration_ms"] = 900
        (self.dir / "GB.json").write_text(json.dumps(meta))
        with self.assertRaises(sa.ArtError):
            sa.compile_asset(self.dir, "GB")

    def test_atlas_budget(self):
        poses = [pose(mark=(0, 0, 256, 256, (i * 3, 0, 0, 255))) for i in range(1, 40)]
        for image in poses:
            image.putpixel((0, 0), (0, 0, 0, 0))
        export(self.dir, "GB", poses, [(i, 100) for i in range(len(poses))])
        with self.assertRaises(sa.ArtError):
            sa.compile_asset(self.dir, "GB")


class BuildTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix="systemart-build-check-")
        self.root = Path(self.tmp.name)
        (self.root / "config").mkdir()
        (self.root / "config/systems.cfg").write_text("sys|Game Boy|Game Boy|mgba|GB|GB.png|7E9B47|gb\n")
        art = self.root / sa.ART
        moving = [pose(), pose(mark=(100, 60, 10, 8, (0, 0, 0, 255)))]
        export(art, "GB", moving, [(0, 40), (1, 70), (0, 110)])
        export(art, "FAVORITES", moving, [(0, 400), (1, 400)])
        export(art, "MUSE", moving, [(0, 400), (1, 400)])
        export(art, "FOLDER_SMALL", [pose()], [(0, 1000)])

    def tearDown(self):
        self.tmp.cleanup()

    def test_c_contract_and_exact_paths(self):
        sa.build(self.root)
        program = r'''
#include "systemart_data.h"
#include <string.h>
_Static_assert(SA_ASSET_COUNT == 4, "every shelf and the folder, nothing else");
_Static_assert(SA_CELL == 256 && SA_STILL == 128, "cell and still sizes");
int main(void) {
    const sa_asset *a = &SA_ASSETS[2], *f = &SA_ASSETS[1];
    if (strcmp(a->key, "GB") || strcmp(a->atlas, "system-icons/GB.png") ||
        strcmp(a->still, "system-icons/GB-still.png")) return 1;
    if (a->cell != 256 || a->cols != 2 || a->rows != 1 || a->nframes != 3 ||
        a->duration_ms != 220) return 2;
    if (a->frames[0].tile != 0 || a->frames[1].tile != 1 || a->frames[2].tile != 0 ||
        a->frames[0].ms != 40 || a->frames[1].ms != 70 || a->frames[2].ms != 110) return 3;
    if (a->rx != 100 || a->ry != 60 || a->rw != 10 || a->rh != 8) return 4;
    if (strcmp(f->key, "FOLDER") || f->rw || f->nframes != 1 || f->duration_ms) return 5;
    return 0;
}
'''
        binary = self.root / "contract"
        subprocess.run(["cc", "-x", "c", "-std=c11", "-Wall", "-Wextra", "-Werror",
                        "-I" + str(self.root / "src"), "-o", str(binary), "-"],
                       input=program, text=True, check=True, capture_output=True)
        subprocess.run([str(binary)], check=True)

    def test_output_limit_fails_before_writes(self):
        with patch.object(sa, "MAX_OUTPUT_BYTES", 100):
            with self.assertRaises(sa.ArtError):
                sa.build(self.root)
        self.assertFalse((self.root / sa.OUTPUT).exists())
        self.assertFalse((self.root / sa.HEADER).exists())

    def test_rebuild_is_deterministic_and_stale_output_fails(self):
        _, first = sa.build(self.root)
        _, second = sa.build(self.root, check=True)
        self.assertEqual(first, second)
        (self.root / sa.OUTPUT / "GB.png").write_bytes(b"broken")
        with self.assertRaises(sa.ArtError):
            sa.build(self.root, check=True)

    def test_a_shelf_without_a_design_and_a_stray_output_fail(self):
        (self.root / sa.ART / "MUSE.json").unlink()
        with self.assertRaises(sa.ArtError):
            sa.build(self.root)
        export(self.root / sa.ART, "MUSE", [pose(), pose(mark=(1, 1, 2, 2, (0, 0, 0, 255)))], [(0, 400), (1, 400)])
        sa.build(self.root)
        (self.root / sa.OUTPUT / "ROMS.png").write_bytes(b"")
        with self.assertRaises(sa.ArtError):
            sa.build(self.root, check=True)


class CheckedInTests(unittest.TestCase):
    ROOT = Path(__file__).resolve().parents[1]

    def test_checked_in_outputs_cover_every_shelf(self):
        assets, outputs = sa.build(self.ROOT, check=True)
        keys = {key for key, _ in assets}
        self.assertEqual(keys, set(sa.catalogue(self.ROOT)))
        self.assertEqual(len(outputs), len(assets) * 2 + 1)
        self.assertEqual(set(p.name for p in (self.ROOT / sa.OUTPUT).glob("*.png")),
                         {f"{k}{s}.png" for k in keys for s in ("", "-still")})

    def test_every_pose_rebuilds_exactly_from_its_export(self):
        directory = self.ROOT / sa.ART
        for key, source in sa.catalogue(self.ROOT).items():
            with self.subTest(key):
                asset = sa.compile_asset(directory, source)
                meta = json.loads((directory / f"{source}.json").read_text())
                with Image.open(directory / f"{source}.png") as atlas:
                    for t in range(asset.unique):
                        x, y = t % meta["cols"] * CELL, t // meta["cols"] * CELL
                        expected = sa.canonical(atlas.crop((x, y, x + CELL, y + CELL)))
                        self.assertEqual(compose(asset, t).tobytes(), expected.tobytes(), f"{key} pose {t}")


if __name__ == "__main__":
    unittest.main()
