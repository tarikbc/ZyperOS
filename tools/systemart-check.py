#!/usr/bin/env python3
"""Focused system-art checks. Requires Pillow; no SDL or launcher build."""
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

from PIL import Image, ImageChops

sys.dont_write_bytecode = True
SPEC = importlib.util.spec_from_file_location("systemart", Path(__file__).with_name("systemart.py"))
sa = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = sa
SPEC.loader.exec_module(sa)


def gif(path, frames, size=(3, 1), loop=0):
    """Tiny independent GIF fixture: one pixel per image descriptor.

    Each item is (x, palette index, disposal, milliseconds). Clear/index/end
    LZW codes fit in two bytes. Palette 0 is transparent; 1/2/3 are RGB.
    Partial rectangles exercise disposal without relying on Pillow's encoder.
    """
    data = bytearray(b"GIF89a" + struct.pack("<HHBBB", *size, 0x81, 0, 0))
    data += bytes((0, 0, 0, 255, 0, 0, 0, 255, 0, 0, 0, 255))
    data += b"\x21\xff\x0bNETSCAPE2.0\x03\x01" + struct.pack("<H", loop) + b"\x00"
    for x, color, disposal, ms in frames:
        data += b"\x21\xf9\x04" + struct.pack("<BHB", disposal * 4 + 1, ms // 10, 0) + b"\x00"
        data += b"\x2c" + struct.pack("<HHHHB", x, 0, 1, 1, 0)
        data += b"\x02\x02" + struct.pack("<H", 4 | color << 3 | 5 << 6) + b"\x00"
    path.write_bytes(data + b"\x3b")


def tile(asset, index):
    import io
    with Image.open(io.BytesIO(asset.atlas)) as atlas:
        x, y = index % asset.cols * 128, index // asset.cols * 128
        return atlas.crop((x, y, x + 128, y + 128))


class CompilerTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix="systemart-check-")
        self.root = Path(self.tmp.name)
        self.source = self.root / "sprite.gif"

    def tearDown(self):
        self.tmp.cleanup()

    def test_repeated_images_keep_source_order_and_delays(self):
        gif(self.source, [(0, 1, 2, 40), (1, 2, 2, 70), (0, 1, 2, 110)])
        result = sa.compile_asset(self.source)
        self.assertIsNotNone(result, "the compiler must produce an atlas")
        self.assertEqual(result.frames, ((0, 40), (1, 70), (0, 110)))
        self.assertEqual((result.unique, result.cols, result.rows, result.duration_ms), (2, 2, 1, 220))
        self.assertEqual(tile(result, 0).getpixel((20, 64)), (255, 0, 0, 255))
        self.assertEqual(tile(result, 1).getpixel((64, 64)), (0, 255, 0, 255))
        import io
        with Image.open(io.BytesIO(result.still)) as still:
            self.assertEqual(still.tobytes(), tile(result, 0).tobytes())

    def test_disposal_background_and_restore_previous(self):
        for disposal, expected in [(2, (0, 0, 0, 0)), (3, (255, 0, 0, 255))]:
            with self.subTest(disposal=disposal):
                gif(self.source, [(0, 1, 1, 20), (0, 2, disposal, 30), (2, 3, 1, 40)])
                result = sa.compile_asset(self.source)
                last = tile(result, result.frames[2][0])
                self.assertEqual(last.getpixel((20, 64)), expected)
                self.assertEqual(last.getpixel((108, 64)), (0, 0, 255, 255))

    def test_static_png_is_centered_nearest_and_unchanged(self):
        source = self.root / "still.png"
        image = Image.new("RGBA", (2, 4))
        image.putpixel((0, 1), (13, 37, 91, 128))
        image.save(source)
        before = source.read_bytes()
        result = sa.compile_asset(source)
        self.assertEqual(result.frames, ((0, 0),))
        self.assertEqual(result.duration_ms, 0)
        self.assertEqual(tile(result, 0).getpixel((40, 40)), (13, 37, 91, 128))
        self.assertEqual(tile(result, 0).getpixel((0, 40)), (0, 0, 0, 0))
        self.assertEqual(source.read_bytes(), before)

    def test_float_keeps_first_pose_and_translates_rgba_without_loss(self):
        source = self.root / "generated.png"
        image = Image.new("RGBA", (128, 128))
        # Black outline, partial alpha, and edge pixels must all survive.
        for x in range(20, 24):
            for y in range(4, 8):
                image.putpixel((x, y), (0, 0, 0, 255))
        image.putpixel((19, 4), (7, 11, 19, 96))
        image.putpixel((0, 2), (0, 0, 0, 255))
        image.putpixel((127, 2), (0, 0, 0, 255))
        image.putpixel((40, 127), (255, 0, 0, 255))
        image.save(source)
        before = source.read_bytes()
        still = sa.compile_asset(source)
        moving = sa.compile_asset(source, motion="float", generated=True)
        self.assertEqual(moving.frames, ((0, 400), (1, 400)))
        self.assertEqual((moving.cols, moving.rows, moving.duration_ms), (2, 1, 800))
        self.assertEqual(moving.still, still.still)
        self.assertEqual(tile(moving, 0).tobytes(), image.tobytes())
        expected = image.tobytes()[128 * 2 * 4:] + bytes(128 * 2 * 4)
        self.assertEqual(tile(moving, 1).tobytes(), expected)
        self.assertEqual(source.read_bytes(), before)

    def test_float_rejects_cropping_even_partial_alpha(self):
        source = self.root / "edge.png"
        for y, alpha in [(0, 255), (1, 255), (1, 1)]:
            with self.subTest(y=y, alpha=alpha):
                image = Image.new("RGBA", (128, 128))
                image.putpixel((64, y), (0, 0, 0, alpha))
                image.save(source)
                with self.assertRaises(sa.ArtError):
                    sa.compile_asset(source, motion="float", generated=True)

    def test_float_is_generated_png_only(self):
        source = self.root / "still.png"
        image = Image.new("RGBA", (128, 128))
        image.putpixel((64, 64), (0, 0, 0, 255))
        image.save(source)
        for options in [{"motion": "float"}, {"motion": "spin", "generated": True}]:
            with self.subTest(options=options):
                with self.assertRaises(sa.ArtError):
                    sa.compile_asset(source, **options)
        gif(source, [(0, 1, 2, 40), (1, 2, 2, 70)])  # GIF bytes with a PNG suffix
        with self.assertRaises(sa.ArtError):
            sa.compile_asset(source, motion="float", generated=True)

    def test_invalid_or_unbounded_sources_fail(self):
        for count, ms, loop in [(73, 100, 0), (2, 0, 0), (2, 2010, 0), (2, 100, 1), (31, 2000, 0)]:
            with self.subTest(count=count, ms=ms, loop=loop):
                gif(self.source, [(i % 3, 1, 2, ms) for i in range(count)], loop=loop)
                with self.assertRaises(sa.ArtError):
                    sa.compile_asset(self.source)
        gif(self.source, [(0, 1, 2, 100)], size=(2049, 1))
        with self.assertRaises(sa.ArtError):
            sa.compile_asset(self.source)
        self.source.write_bytes(b"not an image")
        with self.assertRaises(sa.ArtError):
            sa.compile_asset(self.source)
        source = self.root / "opaque.png"
        Image.new("RGB", (8, 8), "red").save(source)
        with self.assertRaises(sa.ArtError):
            sa.compile_asset(source)

    def test_source_byte_and_decoded_pixel_budgets(self):
        gif(self.source, [(0, 1, 2, 100), (1, 2, 2, 100)])
        with patch.object(sa, "MAX_SOURCE_BYTES", 10):
            with self.assertRaises(sa.ArtError):
                sa.compile_asset(self.source)
        with patch.object(sa, "MAX_SOURCE_PIXELS", 5):
            with self.assertRaises(sa.ArtError):
                sa.compile_asset(self.source)

    def test_first_frame_must_remain_visible_after_resize(self):
        source = self.root / "subpixel.png"
        image = Image.new("RGBA", (256, 256))
        image.putpixel((0, 0), (255, 0, 0, 255))
        image.save(source)
        with self.assertRaises(sa.ArtError):
            sa.compile_asset(source)

    def test_unique_frame_cap_and_partial_atlas_row(self):
        source = self.root / "many.gif"
        # One moving opaque pixel, rather than a color quantizer's guesses.
        gif(source, [(i, 1, 2, 100) for i in range(9)], size=(128, 1))
        result = sa.compile_asset(source)
        self.assertEqual((result.unique, result.cols, result.rows), (9, 8, 2))
        self.assertEqual(tile(result, 15).getbbox(), None)
        gif(source, [(i, 1, 2, 100) for i in range(65)], size=(128, 1))
        with self.assertRaises(sa.ArtError):
            sa.compile_asset(source)


class BuildTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix="systemart-build-check-")
        self.root = Path(self.tmp.name)
        (self.root / "config").mkdir()
        (self.root / "config/systems.cfg").write_text("sys|Game Boy|Game Boy|mgba|GB|GB.png|7E9B47|gb\n")
        source = self.root / "res/system-art/source/GB.gif"
        source.parent.mkdir(parents=True)
        gif(source, [(0, 1, 2, 40), (1, 2, 2, 70), (0, 1, 2, 110)])
        self.data = {
            "schema": 1, "origins": {"fixture": {"credit": "Test fixture"}},
            "assets": [
                {"key": "GB", "tag": "GB", "name": "Game Boy", "status": "ready",
                 "source": "source/GB.gif", "origin": "fixture", "sha256": sa.digest(source)},
                {"key": "FAVORITES", "tag": "FAV", "name": "Favorites", "status": "missing", "reason": "No art"},
                {"key": "MUSE", "tag": "MUSE", "name": "Muse", "status": "missing", "reason": "No art"},
            ],
        }
        self.save_manifest()

    def tearDown(self):
        self.tmp.cleanup()

    def save_manifest(self):
        (self.root / sa.MANIFEST).write_text(json.dumps(self.data))

    def test_c_contract_and_exact_paths(self):
        sa.build(self.root)
        program = r'''
#include "systemart_data.h"
#include <string.h>
_Static_assert(SA_ASSET_COUNT == 1, "missing art must have no fake entry");
int main(void) {
    const sa_asset *a = &SA_ASSETS[0];
    if (strcmp(a->key, "GB") || strcmp(a->atlas, "system-icons/GB.png") ||
        strcmp(a->still, "system-icons/GB-still.png")) return 1;
    if (a->cell != 128 || a->cols != 2 || a->rows != 1 || a->nframes != 3 ||
        a->duration_ms != 220) return 2;
    if (a->frames[0].tile != 0 || a->frames[1].tile != 1 || a->frames[2].tile != 0 ||
        a->frames[0].ms != 40 || a->frames[1].ms != 70 || a->frames[2].ms != 110) return 3;
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
        _, _, first = sa.build(self.root)
        _, _, second = sa.build(self.root, check=True)
        self.assertEqual(first, second)
        (self.root / sa.OUTPUT / "GB.png").write_bytes(b"broken")
        with self.assertRaises(sa.ArtError):
            sa.build(self.root, check=True)

    def test_catalogue_coverage_and_source_identity(self):
        original = json.dumps(self.data)
        for mistake in ("duplicate", "missing", "wrong tag", "unknown origin", "wrong hash", "outside source"):
            with self.subTest(mistake=mistake):
                self.data = json.loads(original)
                row = self.data["assets"][0]
                if mistake == "duplicate": self.data["assets"][1] = row.copy()
                if mistake == "missing": self.data["assets"].pop()
                if mistake == "wrong tag": row["tag"] = "OTHER"
                if mistake == "unknown origin": row["origin"] = "unknown"
                if mistake == "wrong hash": row["sha256"] = "0" * 64
                if mistake == "outside source": row["source"] = "../../config/systems.cfg"
                self.save_manifest()
                with self.assertRaises(sa.ArtError):
                    sa.build(self.root)

    def test_manifest_motion_requires_generated_png_and_credit(self):
        source = self.root / "res/system-art/source/GB.png"
        image = Image.new("RGBA", (128, 128))
        image.putpixel((64, 4), (0, 0, 0, 255))
        image.save(source)
        row = self.data["assets"][0]
        row.update(source="source/GB.png", sha256=sa.digest(source), motion="float",
                   motion_credit="Controller-selected two-pose motion")
        self.data["origins"]["fixture"]["kind"] = "generated"
        valid = json.dumps(self.data)
        for mistake in ("imported origin", "no credit", "unknown motion", "null motion", "gif source", "missing art"):
            with self.subTest(mistake=mistake):
                self.data = json.loads(valid)
                row = self.data["assets"][0]
                if mistake == "imported origin": self.data["origins"]["fixture"]["kind"] = "imported"
                if mistake == "no credit": row.pop("motion_credit")
                if mistake == "unknown motion": row["motion"] = "spin"
                if mistake == "null motion": row["motion"] = None
                if mistake == "gif source":
                    row.update(source="source/GB.gif", sha256=sa.digest(source.with_suffix(".gif")))
                if mistake == "missing art": self.data["assets"][1]["motion"] = "float"
                self.save_manifest()
                with self.assertRaises(sa.ArtError):
                    sa.build(self.root)
        self.data = json.loads(valid)
        self.save_manifest()
        _, assets, _ = sa.build(self.root)
        self.assertEqual(assets[0][1].frames, ((0, 400), (1, 400)))
        self.assertEqual(assets[0][1].duration_ms, 800)


class SuppliedTests(unittest.TestCase):
    ROOT = Path(__file__).resolve().parents[1]

    def test_generated_home_art_animates_without_pixel_loss(self):
        # Home-art policy requires motion for generated additions. The
        # compiler's standalone PNG API still defaults to static (tested above).
        manifest = json.loads((self.ROOT / sa.MANIFEST).read_text())
        checked = 0
        for row in manifest["assets"]:
            if row["status"] != "ready" or manifest["origins"][row["origin"]].get("kind") != "generated":
                continue
            source = self.ROOT / sa.MANIFEST.parent / row["source"]
            if source.suffix.lower() == ".gif":
                # Drawn in the pack's own motion (tools/systemart-draw.py):
                # two frames of 400 ms, the second a different picture.
                with self.subTest(key=row["key"]):
                    self.assertNotIn("motion", row)
                    moving = sa.compile_asset(source)
                    self.assertEqual(moving.frames, ((0, 400), (1, 400)))
                    self.assertNotEqual(tile(moving, 0).tobytes(), tile(moving, 1).tobytes())
                    checked += 1
                continue
            with self.subTest(key=row["key"]):
                self.assertEqual(row.get("motion"), "float")
                self.assertTrue(row.get("motion_credit"))
                still = sa.compile_asset(source)
                moving = sa.compile_asset(source, motion=row["motion"], generated=True)
                first = tile(still, 0).tobytes()
                self.assertEqual(moving.frames, ((0, 400), (1, 400)))
                self.assertEqual(moving.duration_ms, 800)
                self.assertEqual(moving.still, still.still)
                self.assertEqual(tile(moving, 0).tobytes(), first)
                self.assertEqual(tile(moving, 1).tobytes(), first[128 * 2 * 4:] + bytes(128 * 2 * 4))
                self.assertGreaterEqual(tile(still, 0).getchannel("A").getbbox()[1], 2)
                checked += 1
        self.assertGreater(checked, 0)

    def test_gb_gba_all_resized_alpha_and_visible_rgb(self):
        # The expected images come directly from each supplied GIF, never
        # from a compiler helper, its atlas or its transparency decisions.
        # This catches forced palette-index transparency and alpha-squared
        # pastes, including palette changes and later disposal operations.
        for key, expected_count in [("GB", 48), ("GBA", 69)]:
            source = self.ROOT / "res/system-art/source" / f"{key}.gif"
            result = sa.compile_asset(source)
            self.assertEqual(len(result.frames), expected_count)
            opaque_black_later = 0
            with Image.open(source) as original:
                for index in range(expected_count):
                    original.seek(index)
                    expected = original.convert("RGBA").resize((128, 128), Image.Resampling.NEAREST)
                    actual = tile(result, result.frames[index][0])
                    with self.subTest(key=key, frame=index):
                        self.assertEqual(result.frames[index][1], original.info["duration"])
                        self.assertIsNone(ImageChops.difference(expected.getchannel("A"), actual.getchannel("A")).getbbox())
                        visible = expected.getchannel("A").point(lambda a: 255 if a else 0).convert("RGB")
                        difference = ImageChops.difference(expected.convert("RGB"), actual.convert("RGB"))
                        self.assertIsNone(ImageChops.multiply(difference, visible).getbbox())
                    if index:
                        raw = expected.tobytes()
                        opaque_black_later += sum(raw[i:i + 4] == b"\x00\x00\x00\xff" for i in range(0, len(raw), 4))
            self.assertGreater(opaque_black_later, 0, f"{key}: test must cover opaque black in later frames")

    def test_checked_in_outputs_and_source_hashes(self):
        rows, assets, outputs = sa.build(self.ROOT, check=True)
        self.assertEqual({row["key"] for row in rows}, set(sa.catalogue(self.ROOT)))
        self.assertEqual(len(assets), len([row for row in rows if row["status"] == "ready"]))
        self.assertEqual(len(outputs), len(assets) * 2 + 1)
        manifest = json.loads((self.ROOT / sa.MANIFEST).read_text())
        favorite = next(row for row in manifest["assets"] if row["key"] == "FAVORITES")
        self.assertEqual(favorite["archive_path"], "favorites/icon.gif")
        wsc = self.ROOT / "res/system-art/source/WSC.png"
        self.assertEqual(hashlib.sha256(wsc.read_bytes()).hexdigest(),
                         "cbb02ce84683c0c1b2e040535311e6e679e87f31d4db5a302f1415a1c62fe456")

if __name__ == "__main__":
    unittest.main()
