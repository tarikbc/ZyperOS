#!/usr/bin/env python3
"""Compile transparent system sprites; Python 3.9+ and Pillow 9.1+.

Initial import: systemart.py --import-pack /path/to/extracted/pack
Rebuild:        systemart.py --report /private/tmp/zyperos-systemart-report.md
Verify only:   systemart.py --check

The manifest is the source catalogue. To add approved art, place its source
under res/system-art/source, record its attribution and SHA-256, and change
that catalogue row from missing to ready. This tool never invents missing art.
Generated PNGs can opt into motion="float", with motion_credit on the row and
kind="generated" on their attribution origin. The default PNG remains static.
Source artwork retains its own attribution and is not relicensed under ZyperOS's license.
"""
import argparse
from dataclasses import dataclass
import hashlib
import io
import json
from pathlib import Path
import re
import sys
import warnings

from PIL import Image

CELL = 128
MAX_COLS = 8
MAX_FRAMES = 72
MAX_UNIQUE = 64
MAX_SOURCE_SIDE = 2048
MAX_SOURCE_PIXELS = 64 * 1024 * 1024  # all decoded source frames, per clip
MAX_SOURCE_BYTES = 8 * 1024 * 1024
MIN_DELAY_MS, MAX_DELAY_MS, MAX_DURATION_MS = 10, 2000, 60000
MAX_ASSETS = 64
MAX_OUTPUT_BYTES = 16 * 1024 * 1024  # all atlases, stills and the C header
MANIFEST = Path("res/system-art/manifest.json")
HEADER = Path("src/systemart_data.h")
OUTPUT = Path("res/system-icons")
KEY = re.compile(r"[A-Z0-9_]{1,32}\Z")
# Not shelves: the File Transfer page shows these beside the card's folders
# (ROMs, BIOS, Saves, Save States, Roms/BGM, Videos, Audiobooks, and any
# other folder); Music uses the MUSE shelf's walkman. A manifest may list each
# once, as it must list each shelf.
FOLDERS = [("ROMS", "ROMS", "ROMs"), ("BIOS", "BIOS", "BIOS"), ("SAVES", "SAVES", "Saves"),
           ("STATES", "STATES", "Save States"), ("BGM", "BGM", "Menu music"),
           ("VIDEOS", "VIDEOS", "Videos"), ("BOOKS", "BOOKS", "Audiobooks"),
           ("FOLDER", "FOLDER", "Folder")]
FLOAT_CREDIT = ("Controller choice: two poses at 400 ms each, with a 2 px upward move. "
                "This follows the pack's two-frame bob. It does not record user design approval.")

# Keys are card stems, not tags: MD uses GENESIS, SFC uses SNES, PRT uses PORTS.
PACK_MAP = {
    "A2600": "atari2600", "A5200": "atari5200", "A7800": "atari7800",
    "NES": "nes", "SMS": "mastersystem", "GB": "gb", "GENESIS": "genesis",
    "PCE": "tg16", "LYNX": "atarilynx", "GG": "gamegear", "NEOGEO": "neogeo",
    "SNES": "snes", "SEGACD": "segacd", "32X": "sega32x", "VB": "virtualboy",
    "PS1": "psx", "N64": "n64", "NGP": "ngp", "GBC": "gbc", "WS": "wonderswan",
    "NGPC": "ngpc", "DC": "dreamcast", "GBA": "gba", "PKMINI": "pokemini",
    "NDS": "nds", "PICO8": "pico8", "ARCADE": "fbneo", "MAME": "mame", "PSP": "psp",
}


class ArtError(ValueError):
    pass


@dataclass(frozen=True)
class Compiled:
    width: int
    height: int
    unique: int
    cols: int
    rows: int
    frames: tuple
    duration_ms: int
    atlas: bytes
    still: bytes


def png(image):
    stream = io.BytesIO()
    image.save(stream, format="PNG", optimize=True, compress_level=9)
    return stream.getvalue()


def compile_asset(path, *, motion=None, generated=False):
    """Read at most 72 composited frames; retain RGBA, including opaque black.

    Pillow seek/convert applies each GIF's original transparency index and
    disposal. No GIF is re-encoded and no transparency index is overridden.
    The fixed source canvas, not per-frame content bounds, controls resizing.
    Only an explicitly generated PNG can opt into the controller's float.
    """
    path = Path(path)
    if motion not in (None, "float"):
        raise ArtError(f"{path}: unsupported motion {motion!r}")
    if motion is not None and not generated:
        raise ArtError(f"{path}: motion is allowed only for generated PNG sources")
    if not 0 < path.stat().st_size <= MAX_SOURCE_BYTES:
        raise ArtError(f"{path}: source exceeds the byte limit or is empty")
    tiles, ids, timeline = [], {}, []
    elapsed = 0
    try:
        with warnings.catch_warnings():
            warnings.simplefilter("error", Image.DecompressionBombWarning)
            with Image.open(path) as image:
                if image.format not in ("GIF", "PNG"):
                    raise ArtError(f"{path}: only GIF and PNG sources are supported")
                if motion is not None and image.format != "PNG":
                    raise ArtError(f"{path}: imported GIF timing and pixels cannot use motion")
                width, height = image.size
                if not 0 < width <= MAX_SOURCE_SIDE or not 0 < height <= MAX_SOURCE_SIDE:
                    raise ArtError(f"{path}: source canvas exceeds {MAX_SOURCE_SIDE} pixels")
                is_gif = image.format == "GIF"
                if is_gif and image.info.get("loop") != 0:
                    raise ArtError(f"{path}: finite or unspecified GIF loops cannot use this metadata API")
                ratio = min(CELL / width, CELL / height)
                size = (max(1, round(width * ratio)), max(1, round(height * ratio)))
                offset = ((CELL - size[0]) // 2, (CELL - size[1]) // 2)
                for index in range(MAX_FRAMES + 1):
                    try:
                        image.seek(index)
                    except EOFError:
                        break
                    if index == MAX_FRAMES or width * height * (index + 1) > MAX_SOURCE_PIXELS:
                        raise ArtError(f"{path}: source frame/pixel budget exceeded")
                    if not is_gif and index:
                        raise ArtError(f"{path}: animated PNG is not a supported input")
                    if image.size != (width, height):
                        raise ArtError(f"{path}: frame extends outside the original canvas")
                    ms = image.info.get("duration") if is_gif else 0
                    if is_gif and (type(ms) is not int or not MIN_DELAY_MS <= ms <= MAX_DELAY_MS):
                        raise ArtError(f"{path}: invalid frame duration {ms}")
                    elapsed += ms
                    if elapsed > MAX_DURATION_MS:
                        raise ArtError(f"{path}: animation exceeds the duration budget")
                    rgba = image.convert("RGBA")
                    alpha = rgba.getchannel("A")
                    if alpha.getextrema()[0] != 0:
                        raise ArtError(f"{path}: frame {index} has no transparent background")
                    if index == 0 and alpha.getextrema()[1] == 0:
                        raise ArtError(f"{path}: first frame is empty")
                    resized = rgba.resize(size, Image.Resampling.NEAREST)
                    if index == 0 and resized.getchannel("A").getextrema()[1] == 0:
                        raise ArtError(f"{path}: first frame disappears at {CELL} pixels")
                    canvas = Image.new("RGBA", (CELL, CELL))
                    canvas.paste(resized, offset)  # no mask: retain original alpha, not alpha squared
                    # Canonicalize only invisible RGB. Opaque black is never a color key.
                    invisible = canvas.getchannel("A").point(lambda value: 255 if value == 0 else 0)
                    canvas.paste((0, 0, 0, 0), mask=invisible)
                    raw = canvas.tobytes()
                    if raw not in ids:
                        if len(tiles) == MAX_UNIQUE:
                            raise ArtError(f"{path}: more than {MAX_UNIQUE} unique frames")
                        ids[raw] = len(tiles)
                        tiles.append(canvas)
                    timeline.append((ids[raw], ms))
    except ArtError:
        raise
    except (OSError, ValueError, SyntaxError, Image.DecompressionBombError,
            Image.DecompressionBombWarning) as error:
        raise ArtError(f"{path}: invalid source image: {error}") from error
    if not tiles:
        raise ArtError(f"{path}: no frames")
    if motion == "float":
        # getbbox includes partial alpha. Do not trim, shift the first pose,
        # or add padding to make a source fit: those would change the art.
        bounds = tiles[0].getchannel("A").getbbox()
        if bounds is None or bounds[1] < 2:
            raise ArtError(f"{path}: float needs two transparent top rows at {CELL}px; refusing to crop")
        raised = Image.new("RGBA", (CELL, CELL))
        raised.paste(tiles[0], (0, -2))  # direct RGBA copy; no mask, recoloring or interpolation
        tiles.append(raised)
        timeline = [(0, 400), (1, 400)]
        elapsed = 800
    cols = min(MAX_COLS, len(tiles))
    rows = (len(tiles) + cols - 1) // cols
    atlas = Image.new("RGBA", (cols * CELL, rows * CELL))
    for index, frame in enumerate(tiles):
        atlas.paste(frame, (index % cols * CELL, index // cols * CELL))
    return Compiled(width, height, len(tiles), cols, rows, tuple(timeline), elapsed,
                    png(atlas), png(tiles[0]))


def catalogue(root):
    entries = {}
    for line in (root / "config/systems.cfg").read_text().splitlines():
        if not line.startswith("sys|"):
            continue
        fields = line.split("|")
        key = Path(fields[5]).stem
        if not KEY.fullmatch(key) or key in entries:
            raise ArtError(f"invalid or duplicate card stem: {key}")
        entries[key] = {"key": key, "tag": fields[4], "name": fields[1]}
    for key, tag, name in [("FAVORITES", "FAV", "Favorites"), ("MUSE", "MUSE", "Muse")]:
        if key in entries:
            raise ArtError(f"synthetic shelf duplicates a configured key: {key}")
        entries[key] = {"key": key, "tag": tag, "name": name}
    for key, tag, name in FOLDERS:
        if key in entries:
            raise ArtError(f"folder icon duplicates a shelf key: {key}")
        entries[key] = {"key": key, "tag": tag, "name": name}
    if len(entries) > MAX_ASSETS:
        raise ArtError("system count exceeds the metadata table limit")
    return entries


def digest(path):
    if not 0 < path.stat().st_size <= MAX_SOURCE_BYTES:
        raise ArtError(f"{path}: source byte limit exceeded")
    return hashlib.sha256(path.read_bytes()).hexdigest()


def import_pack(root, pack):
    """One explicit, repeatable import. Never replace an existing source."""
    if (root / MANIFEST).exists():
        raise ArtError("manifest already exists; update approved sources there instead of re-importing")
    entries = catalogue(root)
    origins = {
        "smart-folders-v1.4": {
            "kind": "imported",
            "archive": "pixel-ui-smart-folders.zip", "version": "1.4",
            "metadata_identifier": "animated-pixel-console-icons-smart-folders-icons-8fd837",
            "original_artist": "BenJelter",
            "original_url": "https://benjelter.itch.io/console-icon-pack",
            "additions_and_animation": "YOjeremyRaps",
            "license": "No separate license text was present in the supplied archive. Artwork is not relicensed under ZyperOS's license.",
        },
        "zyperos-wsc": {
            "kind": "generated",
            "method": "Built-in image tool, 2026-11-04, from the user's hardware photograph",
            "description": "Approved corrected Crystal Blue WonderSwan Color illustration",
            "credit": "ZyperOS generated addition; not artwork by BenJelter or YOjeremyRaps",
            "license": "Artwork is separate from the code. No hardware maker endorsement is implied.",
        },
    }
    copies = []
    for key, row in entries.items():
        upstream = f"by_platform/{PACK_MAP[key]}/logo.gif" if key in PACK_MAP else None
        if key == "FAVORITES":
            upstream = "favorites/icon.gif"  # the animated heart, not favorites/logo.gif
        if upstream:
            source = pack / upstream
            compile_asset(source)  # reject unsupported sources before any repository writes
            target = root / "res/system-art/source" / f"{key}.gif"
            if target.exists() and digest(target) != digest(source):
                raise ArtError(f"refusing to replace existing source: {target}")
            row.update(status="ready", source=f"source/{key}.gif", origin="smart-folders-v1.4",
                       archive_path=upstream, sha256=digest(source))
            copies.append((source, target))
        elif key == "WSC":
            source = root / "res/system-art/source/WSC.png"
            compile_asset(source, motion="float", generated=True)
            row.update(status="ready", source="source/WSC.png", origin="zyperos-wsc", sha256=digest(source),
                       motion="float", motion_credit=FLOAT_CREDIT)
        else:
            row.update(status="missing", reason="No approved matching source is imported. No substitute is assigned.")
    for source, target in copies:
        target.parent.mkdir(parents=True, exist_ok=True)
        if not target.exists():
            target.write_bytes(source.read_bytes())
    manifest = {"schema": 1, "origins": origins,
                "assets": [entries[key] for key in sorted(entries)]}
    (root / MANIFEST).write_text(json.dumps(manifest, indent=2, ensure_ascii=False) + "\n")


def load_manifest(root):
    path = root / MANIFEST
    if path.stat().st_size > 256 * 1024:
        raise ArtError("manifest exceeds 256 KiB")
    data = json.loads(path.read_text())
    if data.get("schema") != 1:
        raise ArtError("unsupported manifest schema")
    expected = catalogue(root)
    rows = data.get("assets", [])
    shelves = set(expected) - {key for key, _, _ in FOLDERS}
    if not isinstance(rows, list) or not all(isinstance(row, dict) for row in rows) or \
            not shelves <= {row.get("key") for row in rows}:
        raise ArtError("manifest must cover each configured and synthetic shelf exactly once")
    seen = set()
    source_root = (root / "res/system-art/source").resolve()
    for row in rows:
        key = row.get("key")
        if key not in expected or key in seen:
            raise ArtError(f"unknown or duplicate manifest key: {key}")
        seen.add(key)
        if row.get("name") != expected[key]["name"] or row.get("tag") != expected[key]["tag"]:
            raise ArtError(f"{key}: catalogue name or tag differs from systems.cfg")
        if row.get("status") == "missing":
            if "source" in row or "motion" in row or not row.get("reason"):
                raise ArtError(f"{key}: missing artwork must have a reason and no substitute")
            continue
        if row.get("status") != "ready" or row.get("origin") not in data.get("origins", {}):
            raise ArtError(f"{key}: ready artwork needs an attribution origin")
        relative = Path(row.get("source", ""))
        source = (root / MANIFEST.parent / relative).resolve()
        if relative.is_absolute() or source_root not in source.parents or source.suffix.lower() not in (".gif", ".png"):
            raise ArtError(f"{key}: source must be a GIF or PNG inside system-art/source")
        if "motion" in row:
            origin = data["origins"][row["origin"]]
            if row["motion"] != "float" or origin.get("kind") != "generated" or source.suffix.lower() != ".png":
                raise ArtError(f"{key}: float is supported only for explicitly generated PNG sources")
            if not isinstance(row.get("motion_credit"), str) or not row["motion_credit"].strip():
                raise ArtError(f"{key}: motion needs an explicit motion_credit")
        if digest(source) != row.get("sha256"):
            raise ArtError(f"{key}: source hash changed; review its attribution and update the manifest")
    return sorted(rows, key=lambda row: row["key"])


def c_header(assets):
    lines = [
        "/* Generated by tools/systemart.py. Do not edit.",
        " * Artwork attribution: res/system-art/manifest.json. Artwork is not relicensed with the code.",
        " * Paths are relative to runtime P_ROOT. PNGs default to one static frame, ms=0.",
        " * Generated PNG motion is opt-in and credited in the manifest.",
        " * GIFs and generated motion loop. Preserve each ms value. Tile rect: (tile % cols, tile / cols) * cell.",
        " * Empty grid cells are transparent. Only load the selected atlas; neighbors use stills.",
        " */", "#ifndef ZYPEROS_SYSTEMART_DATA_H", "#define ZYPEROS_SYSTEMART_DATA_H",
        "#include <stdint.h>", "",
        "typedef struct { uint16_t tile, ms; } sa_frame;",
        "typedef struct {",
        "    const char *key, *atlas, *still;",
        "    uint16_t cell, cols, rows, nframes;",
        "    uint32_t duration_ms;",
        "    const sa_frame *frames;",
        "} sa_asset;", "",
        "#define SA_CELL 128u", "#define SA_MAX_ATLAS_RGBA_BYTES 4194304u", "",
    ]
    for key, asset in assets:
        lines.append(f"static const sa_frame SA_FRAMES_{key}[] = {{")
        for start in range(0, len(asset.frames), 8):
            lines.append("    " + ", ".join(f"{{ {tile}, {ms} }}" for tile, ms in asset.frames[start:start + 8]) + ",")
        lines.extend(["};", ""])
    lines.append("static const sa_asset SA_ASSETS[] = {")
    for key, asset in assets:
        lines.append(f'    {{ "{key}", "system-icons/{key}.png", "system-icons/{key}-still.png", '
                     f'{CELL}, {asset.cols}, {asset.rows}, {len(asset.frames)}, {asset.duration_ms}, SA_FRAMES_{key} }},')
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
                    and old.mode == new.mode
                    and old.convert("RGBA").tobytes() == new.convert("RGBA").tobytes())
    except (OSError, ValueError):  # a damaged or foreign file is stale, not an error
        return False


def build(root, check=False):
    root = Path(root)
    rows = load_manifest(root)
    origins = json.loads((root / MANIFEST).read_text())["origins"]
    assets, outputs = [], {}
    for row in rows:
        if row["status"] != "ready":
            continue
        key = row["key"]
        asset = compile_asset(root / MANIFEST.parent / row["source"], motion=row.get("motion"),
                              generated=origins[row["origin"]].get("kind") == "generated")
        assets.append((key, asset))
        outputs[OUTPUT / f"{key}.png"] = asset.atlas
        outputs[OUTPUT / f"{key}-still.png"] = asset.still
    if not assets:
        raise ArtError("no ready assets; refusing to emit an empty C table")
    outputs[HEADER] = c_header(assets)
    if sum(map(len, outputs.values())) > MAX_OUTPUT_BYTES:
        raise ArtError("total generated output exceeds 16 MiB")
    # Fail before writes if old/untracked output names could hide missing art.
    extras = set((root / OUTPUT).glob("*.png")) - {root / path for path in outputs if path.suffix == ".png"}
    if extras:
        raise ArtError("unexpected output PNGs; review them before rebuilding: " + ", ".join(p.name for p in sorted(extras)))
    for relative, data in outputs.items():
        destination = root / relative
        if check:
            if not destination.exists() or not same_output(destination, data):
                raise ArtError(f"generated output is missing or stale: {relative}")
        else:
            destination.parent.mkdir(parents=True, exist_ok=True)
            destination.write_bytes(data)
    return rows, assets, outputs


def report_text(rows, assets, outputs):
    missing = [row for row in rows if row["status"] == "missing"]
    png_bytes = sum(len(data) for path, data in outputs.items() if path.suffix == ".png")
    decoded = [(key, asset.cols * asset.rows * CELL * CELL * 4) for key, asset in assets]
    largest_key, largest = max(decoded, key=lambda item: item[1])
    lines = [
        "# System art compiler report", "",
        f"The compiler generated {len(assets)} atlases and {len(assets)} first-frame PNGs.",
        "The C table uses card stems and paths relative to P_ROOT.",
        "Source artwork retains its attribution in res/system-art/manifest.json.",
        "The compiler did not change WSC.png or import unapproved generated drafts.", "",
        "## Missing artwork", "",
    ]
    lines += [f"- {row['key']}: {row['name']}. {row['reason']}" for row in missing]
    lines += ["", "## Optional generated motion", "",
              "Only generated PNG sources can select `motion: float` in the manifest.",
              "Their origin must declare `kind: generated`. Each motion row must include `motion_credit`.",
              "The first pose stays unchanged. The second pose moves upward by two pixels within the 128-pixel cell.",
              "Each pose lasts 400 ms. The loop lasts 800 ms.",
              "The compiler rejects cropping, including loss of partial-alpha pixels.",
              "Controller chose this motion to match the pack's two-frame, 400 ms bob.",
              "This choice does not record user design approval. PNGs without this option remain static.",
              "Imported GIF timing and pixels remain unchanged.", ""]
    lines += [f"- {row['key']}: {row['motion_credit']}" for row in rows if row.get("motion") == "float"]
    lines += ["", "## Bounds and measured sizes", "",
              "Each cell is 128×128 pixels. Atlases use at most eight columns and eight rows.",
              "Each source has at most 72 frames, 64 unique output frames, and a 2048×2048 canvas.",
              "Each source file is at most 8 MiB. The decoded source budget is 67,108,864 pixels per asset.",
              "GIF delays range from 10 to 2,000 ms. Each loop lasts at most 60,000 ms.",
              "The compiler preserves source delays. It rejects invalid timing instead of changing it.",
              "The catalogue has at most 64 entries. All generated output has a 16 MiB limit.",
              f"PNG output totals {png_bytes:,} bytes. The C header uses {len(outputs[HEADER]):,} bytes.",
              f"Total generated output is {sum(map(len, outputs.values())):,} bytes.",
              f"The largest current atlas is {largest_key}: {largest:,} decoded RGBA bytes ({largest / 1048576:.3f} MiB).",
              f"All current atlases together need {sum(size for _, size in decoded):,} decoded RGBA bytes.",
              "The hard atlas limit is 4,194,304 RGBA bytes. Each neighbor still needs 65,536 RGBA bytes.",
              f"One current atlas and five still textures need at most {largest + 5 * 65536:,} bytes, before driver overhead.",
              "One maximum-size atlas and five still textures need 4,521,984 bytes, before driver overhead.",
              "A CPU decode surface adds one atlas-sized buffer during loading.",
              "Selection transitions that retain two atlases need a separate memory budget.",
              "The renderer must enforce residency. Metadata alone does not limit loaded textures.",
              "No device decode or presentation timing was measured.", "",
              "## Per-asset output", "",
              "| Key | Frames | Unique | Atlas pixels | Loop ms | Atlas bytes | Still bytes |",
              "|---|---:|---:|---|---:|---:|---:|"]
    for key, asset in assets:
        lines.append(f"| {key} | {len(asset.frames)} | {asset.unique} | {asset.cols * CELL}×{asset.rows * CELL} | "
                     f"{asset.duration_ms} | {len(asset.atlas)} | {len(asset.still)} |")
    lines += ["", "## Verification", "",
              "Run the focused check with Python and Pillow:", "",
              "```sh", "python3 tools/systemart-check.py", "python3 tools/systemart.py --check", "```", "",
              "The check covers timing, disposal, deduplication, alpha, bounds, catalogue coverage, and the C metadata contract.",
              "GB and GBA checks compare resized source alpha and visible RGB for every frame.",
              "The check does not build the launcher or run the full suite.", ""]
    return "\n".join(lines)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--import-pack", type=Path)
    parser.add_argument("--check", action="store_true")
    parser.add_argument("--report", type=Path)
    args = parser.parse_args()
    if args.check and (args.import_pack or args.report):
        parser.error("--check does not import or write reports")
    try:
        if args.import_pack:
            import_pack(args.root, args.import_pack)
        rows, assets, outputs = build(args.root, check=args.check)
        if args.report:
            args.report.write_text(report_text(rows, assets, outputs))
        print(f"systemart: {len(assets)} assets, {len(rows) - len(assets)} missing, "
              f"{sum(map(len, outputs.values()))} output bytes; {'verified' if args.check else 'generated'}")
    except (ArtError, OSError, KeyError, TypeError, json.JSONDecodeError) as error:
        print(f"systemart: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
