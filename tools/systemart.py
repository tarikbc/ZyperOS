#!/usr/bin/env python3
"""Compile ZyperOS's system art for the launcher; Python 3.9+ and Pillow 9.1+.

The art is ZyperOS's own (res/system-art/original, its README): one design a
system, 256 pixels square, each pose of its motion a tile of an atlas, with
the frame table beside it (res/system-art/original/animations/<KEY>.json).
tools/systemart-original-pack.py writes those and
tools/systemart-original-check.py holds them. This writes what the launcher,
the payload and the File Transfer page read:

  res/system-icons/<KEY>.png        the first pose whole (256 square), then
                                    each pose's moving region, packed under it
  res/system-icons/<KEY>-still.png  the design's still, 128 square: the home's
                                    row, the Storage page and the File
                                    Transfer page
  src/systemart_data.h              the frame tables and each moving region

A design moves only inside its region (the export's motion_bounds), so the
launcher keeps the first pose as a texture and writes the region of the pose
due into it (src/home.c): a 256-pixel motion in what a 128-pixel atlas took.

    systemart.py           write them
    systemart.py --check   fail when they differ from what it writes
"""
import argparse
from dataclasses import dataclass
import io
import json
from pathlib import Path
import re
import sys

from PIL import Image, ImageChops

CELL = 256
STILL = 128
MAX_FRAMES = 72
MAX_UNIQUE = 64
MIN_DELAY_MS, MAX_DELAY_MS, MAX_DURATION_MS = 10, 60000, 60000
MAX_ATLAS_RGBA_BYTES = 4 * 1024 * 1024    # one decoded atlas, the base and its regions
MAX_ATLAS_SIDE = 4096
MAX_ASSETS = 64
MAX_OUTPUT_BYTES = 16 * 1024 * 1024       # all atlases, stills and the C header
ART = Path("res/system-art/original/animations")
HEADER = Path("src/systemart_data.h")
OUTPUT = Path("res/system-icons")
KEY = re.compile(r"[A-Z0-9_]{1,32}\Z")
# The launcher's key for a design that is not a shelf's: FOLDER, a folder of
# no system on the File Transfer page and the Storage page, is the small
# folder drawn for rows (res/system-art/original/README.md).
EXTRA = {"FOLDER": "FOLDER_SMALL"}
# Shelves that are not in config/systems.cfg
SYNTHETIC = [("FAVORITES", "FAV", "Favorites"), ("MUSE", "MUSE", "Muse")]


class ArtError(ValueError):
    pass


@dataclass(frozen=True)
class Compiled:
    unique: int
    cols: int
    rows: int
    region: tuple          # x, y, w, h in the cell; (0, 0, 0, 0) when nothing moves
    frames: tuple          # (tile, ms)
    duration_ms: int
    atlas: bytes
    still: bytes


def png(image):
    stream = io.BytesIO()
    image.save(stream, format="PNG", optimize=True, compress_level=9)
    return stream.getvalue()


def canonical(image):
    """RGBA with every invisible pixel's color cleared: two poses that look the
    same compare the same, and no hidden color widens a region."""
    image = image.convert("RGBA")
    hidden = image.getchannel("A").point(lambda value: 255 if value == 0 else 0)
    image.paste((0, 0, 0, 0), mask=hidden)
    return image


def layout(unique, width, height):
    """Columns for the regions under the base: the smallest atlas by area."""
    best = None
    for cols in range(1, max(1, min(unique, 8)) + 1):
        rows = -(-unique // cols)
        w, h = max(CELL, cols * width), CELL + rows * height
        if best is None or w * h < best[0]:
            best = (w * h, cols, rows, w, h)
    return best[1:]


def compile_asset(directory, key):
    """One design's atlas, still and frame table, from its export."""
    meta_path, atlas_path, still_path = (directory / f"{key}{suffix}"
                                         for suffix in (".json", ".png", "-still.png"))
    try:
        meta = json.loads(meta_path.read_text())
    except (OSError, json.JSONDecodeError) as error:
        raise ArtError(f"{key}: no readable frame table: {error}") from error
    cell, cols, rows, unique = (meta.get(name) for name in ("cell", "cols", "rows", "unique"))
    if cell != CELL or not all(type(n) is int for n in (cols, rows, unique)):
        raise ArtError(f"{key}: the export is not {CELL}-pixel cells")
    if not (1 <= unique <= MAX_UNIQUE and unique <= cols * rows):
        raise ArtError(f"{key}: unique pose count out of bounds")
    timeline = meta.get("frames") or []
    if not 1 <= len(timeline) <= MAX_FRAMES:
        raise ArtError(f"{key}: frame count out of bounds")
    frames = []
    for frame in timeline:
        tile, ms = frame.get("tile"), frame.get("ms")
        if type(tile) is not int or type(ms) is not int or not 0 <= tile < unique \
                or not MIN_DELAY_MS <= ms <= MAX_DELAY_MS:
            raise ArtError(f"{key}: invalid frame {frame}")
        frames.append((tile, ms))
    duration = sum(ms for _, ms in frames)
    if duration != meta.get("duration_ms") or duration > MAX_DURATION_MS:
        raise ArtError(f"{key}: the loop's length differs from its frames")
    if {tile for tile, _ in frames} != set(range(unique)):
        raise ArtError(f"{key}: a pose is never shown, or a frame names no pose")
    # The loop starts on the frame its still was taken from (the export's
    # still_time_ms), turned round whole frames: a design that takes the
    # focus goes on from the picture the row showed, with no jump
    still_at = meta.get("still_time_ms", 0)
    if type(still_at) is not int or not 0 <= still_at < max(duration, 1):
        raise ArtError(f"{key}: still_time_ms is outside the loop")
    at, first = 0, 0
    for first, (_, ms) in enumerate(frames):
        if still_at < at + ms:
            break
        at += ms
    frames = frames[first:] + frames[:first]
    try:
        with Image.open(atlas_path) as atlas, Image.open(still_path) as still:
            if atlas.size != (cols * CELL, rows * CELL) or still.size != (CELL, CELL):
                raise ArtError(f"{key}: atlas or still size differs from the frame table")
            tiles = [canonical(atlas.crop((t % cols * CELL, t // cols * CELL,
                                           t % cols * CELL + CELL, t // cols * CELL + CELL)))
                     for t in range(unique)]
            still = canonical(still)
    except ArtError:
        raise
    except (OSError, ValueError, SyntaxError, Image.DecompressionBombError) as error:
        raise ArtError(f"{key}: invalid image: {error}") from error
    if tiles[0].getchannel("A").getextrema()[1] == 0:
        raise ArtError(f"{key}: the first pose is empty")
    for t, image in enumerate(tiles):
        if image.getchannel("A").getextrema()[0] != 0:
            raise ArtError(f"{key}: pose {t} has no transparent background")
    # Where any pose differs from the first: the region the launcher rewrites
    box = None
    for image in tiles[1:]:
        diff = ImageChops.difference(image, tiles[0]).getbbox(alpha_only=False)
        if diff:
            box = diff if box is None else (min(box[0], diff[0]), min(box[1], diff[1]),
                                            max(box[2], diff[2]), max(box[3], diff[3]))
    if unique > 1 and box is None:
        raise ArtError(f"{key}: its poses are all the same")
    bounds = meta.get("motion_bounds") or [0, 0, CELL, CELL]
    if box and not (box[0] >= bounds[0] and box[1] >= bounds[1] and
                    box[2] <= bounds[0] + bounds[2] and box[3] <= bounds[1] + bounds[3]):
        raise ArtError(f"{key}: a pose changes outside its motion bounds {bounds}")
    region = (box[0], box[1], box[2] - box[0], box[3] - box[1]) if box else (0, 0, 0, 0)
    if box:
        rcols, rrows, width, height = layout(unique, region[2], region[3])
    else:
        rcols, rrows, width, height = 0, 0, CELL, CELL
    if width > MAX_ATLAS_SIDE or height > MAX_ATLAS_SIDE or width * height * 4 > MAX_ATLAS_RGBA_BYTES:
        raise ArtError(f"{key}: atlas {width}x{height} exceeds the decoded budget")
    out = Image.new("RGBA", (width, height))
    out.paste(tiles[0], (0, 0))
    if box:
        for t, image in enumerate(tiles):
            out.paste(image.crop(box), (t % rcols * region[2], CELL + t // rcols * region[3]))
    small = still.convert("RGBa").resize((STILL, STILL), Image.Resampling.BOX).convert("RGBA")
    small = canonical(small)
    if small.getchannel("A").getextrema()[1] == 0:
        raise ArtError(f"{key}: the still disappears at {STILL} pixels")
    if not box:      # a design that does not move: one pose, shown for good
        frames, duration = [(0, 0)], 0
    return Compiled(unique if box else 1, rcols, rrows, region, tuple(frames), duration,
                    png(out), png(small))


def catalogue(root):
    """Every shelf's key: config/systems.cfg's card stems and the synthetic
    shelves, then the designs that are not shelves."""
    keys = {}
    for line in (root / "config/systems.cfg").read_text().splitlines():
        if not line.startswith("sys|"):
            continue
        fields = line.split("|")
        key = Path(fields[5]).stem
        if not KEY.fullmatch(key) or key in keys:
            raise ArtError(f"invalid or duplicate card stem: {key}")
        keys[key] = key
    for key, _, _ in SYNTHETIC:
        if key in keys:
            raise ArtError(f"synthetic shelf duplicates a configured key: {key}")
        keys[key] = key
    for key, source in EXTRA.items():
        if key in keys:
            raise ArtError(f"design duplicates a shelf key: {key}")
        keys[key] = source
    if len(keys) > MAX_ASSETS:
        raise ArtError("design count exceeds the metadata table limit")
    return dict(sorted(keys.items()))


def c_header(assets):
    lines = [
        "/* Generated by tools/systemart.py. Do not edit.",
        " * ZyperOS's own system art (res/system-art/original). Paths are relative to runtime P_ROOT.",
        " * An atlas is the first pose whole (cell x cell at 0,0), then each pose's moving region",
        " * (rw x rh) packed under it: tile t at ((t % cols) * rw, cell + (t / cols) * rh). A design",
        " * that does not move has no region (rw 0) and one frame. Preserve each ms value.",
        " * The still is SA_STILL square. Only load the selected atlas; neighbors use stills.",
        " */", "#ifndef ZYPEROS_SYSTEMART_DATA_H", "#define ZYPEROS_SYSTEMART_DATA_H",
        "#include <stdint.h>", "",
        "typedef struct { uint16_t tile, ms; } sa_frame;",
        "typedef struct {",
        "    const char *key, *atlas, *still;",
        "    uint16_t cell, cols, rows, nframes;",
        "    uint16_t rx, ry, rw, rh;",
        "    uint32_t duration_ms;",
        "    const sa_frame *frames;",
        "} sa_asset;", "",
        f"#define SA_CELL {CELL}u", f"#define SA_STILL {STILL}u",
        f"#define SA_MAX_ATLAS_RGBA_BYTES {MAX_ATLAS_RGBA_BYTES}u", "",
    ]
    for key, asset in assets:
        lines.append(f"static const sa_frame SA_FRAMES_{key}[] = {{")
        for start in range(0, len(asset.frames), 8):
            lines.append("    " + ", ".join(f"{{ {tile}, {ms} }}" for tile, ms in asset.frames[start:start + 8]) + ",")
        lines.extend(["};", ""])
    lines.append("static const sa_asset SA_ASSETS[] = {")
    for key, asset in assets:
        rx, ry, rw, rh = asset.region
        lines.append(f'    {{ "{key}", "system-icons/{key}.png", "system-icons/{key}-still.png", '
                     f'{CELL}, {asset.cols}, {asset.rows}, {len(asset.frames)}, {rx}, {ry}, {rw}, {rh}, '
                     f'{asset.duration_ms}, SA_FRAMES_{key} }},')
    lines.extend(["};", "#define SA_ASSET_COUNT (sizeof SA_ASSETS / sizeof SA_ASSETS[0])", "", "#endif", ""])
    return "\n".join(lines).encode()


def same_output(path, data):
    """PNGs match by decoded pixels: Pillow releases encode the same image to
    different bytes, so a byte compare fails on any Python but the author's."""
    current = path.read_bytes()
    if current == data or path.suffix != ".png":
        return current == data
    try:
        with Image.open(io.BytesIO(current)) as old, Image.open(io.BytesIO(data)) as new:
            return (old.format == new.format == "PNG" and old.size == new.size
                    and old.convert("RGBA").tobytes() == new.convert("RGBA").tobytes())
    except (OSError, ValueError):  # a damaged or foreign file is stale, not an error
        return False


def build(root, check=False):
    root = Path(root)
    assets, outputs = [], {}
    for key, source in catalogue(root).items():
        asset = compile_asset(root / ART, source)
        assets.append((key, asset))
        outputs[OUTPUT / f"{key}.png"] = asset.atlas
        outputs[OUTPUT / f"{key}-still.png"] = asset.still
    outputs[HEADER] = c_header(assets)
    if sum(map(len, outputs.values())) > MAX_OUTPUT_BYTES:
        raise ArtError("total generated output exceeds 16 MiB")
    # Fail before writes if other files in the output folder could hide a missing design.
    extras = set((root / OUTPUT).glob("*.png")) - {root / path for path in outputs if path.suffix == ".png"}
    if extras:
        raise ArtError("unexpected output PNGs: " + ", ".join(p.name for p in sorted(extras)))
    for relative, data in outputs.items():
        destination = root / relative
        if check:
            if not destination.exists() or not same_output(destination, data):
                raise ArtError(f"generated output is missing or stale: {relative}")
        else:
            destination.parent.mkdir(parents=True, exist_ok=True)
            destination.write_bytes(data)
    return assets, outputs


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    try:
        assets, outputs = build(args.root, check=args.check)
        largest = max(len(a.atlas) for _, a in assets)
        print(f"systemart: {len(assets)} designs, {sum(map(len, outputs.values()))} output bytes "
              f"(largest atlas file {largest}); {'verified' if args.check else 'generated'}")
    except (ArtError, OSError, KeyError, TypeError, json.JSONDecodeError) as error:
        print(f"systemart: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
