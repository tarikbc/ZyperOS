#!/usr/bin/env python3
"""Save reviewed browser-rendered PNG atlases and bounded frame metadata."""
import argparse
import base64
import hashlib
import json
from pathlib import Path
import re
import struct

ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / 'res/system-art/original/animations'
KEY = re.compile(r'[A-Z0-9_]{1,32}\Z')


def png_data(value, expected_size):
    prefix = 'data:image/png;base64,'
    if not isinstance(value, str) or not value.startswith(prefix):
        raise ValueError('expected a PNG data URL')
    data = base64.b64decode(value[len(prefix):], validate=True)
    if not 24 <= len(data) <= 16 * 1024 * 1024:
        raise ValueError('PNG byte limit exceeded')
    if data[:8] != b'\x89PNG\r\n\x1a\n' or struct.unpack('>II', data[16:24]) != expected_size:
        raise ValueError('PNG size differs from metadata')
    return data


def save_batch(path):
    data = json.loads(path.read_text())
    if data.get('schema') != 1 or not isinstance(data.get('assets'), list):
        raise ValueError('unsupported export schema')
    writes = []
    for asset in data['assets']:
        key = asset.get('key', '')
        if not KEY.fullmatch(key):
            raise ValueError('invalid asset key')
        cell, cols, rows, unique = (asset.get(k) for k in ('cell', 'cols', 'rows', 'unique'))
        if cell != 256 or not all(type(n) is int for n in (cols, rows, unique)):
            raise ValueError(f'{key}: invalid atlas dimensions')
        minimum = 1 if asset.get('static') else 2
        if not (1 <= cols <= 8 and 1 <= rows <= 8 and minimum <= unique <= 64 and unique <= cols * rows):
            raise ValueError(f'{key}: atlas budget exceeded')
        frames = asset.get('frames', [])
        if not 1 <= len(frames) <= 72:
            raise ValueError(f'{key}: invalid frame count')
        for frame in frames:
            tile, ms = frame.get('tile'), frame.get('ms')
            if type(tile) is not int or type(ms) is not int or not 0 <= tile < unique or not 10 <= ms <= 2000:
                raise ValueError(f'{key}: invalid timeline frame')
        duration = sum(frame['ms'] for frame in frames)
        if not 0 < duration <= 60000 or duration != asset.get('duration_ms'):
            raise ValueError(f'{key}: invalid duration')
        atlas = png_data(asset['atlas'], (cols * cell, rows * cell))
        still = png_data(asset['still'], (cell, cell))
        meta = {k: v for k, v in asset.items() if k not in ('atlas', 'still')}
        inputs = {}
        source_root = (OUTPUT.parent/'source').resolve()
        for name in asset.get('inputs', []):
            source = (OUTPUT.parent/name).resolve()
            if source_root not in source.parents or source.suffix != '.png':
                raise ValueError(f'{key}: invalid source path')
            inputs[name] = hashlib.sha256(source.read_bytes()).hexdigest()
        meta['input_sha256'] = inputs
        renderers = {}
        for name in asset.get('renderers', []):
            source = (OUTPUT.parent/name).resolve()
            if OUTPUT.parent.resolve() not in source.parents or source.suffix != '.js':
                raise ValueError(f'{key}: invalid renderer path')
            renderers[name] = hashlib.sha256(source.read_bytes()).hexdigest()
        meta['renderer_sha256'] = renderers
        meta.update(atlas=f'{key}.png', still=f'{key}-still.png',
                    atlas_sha256=hashlib.sha256(atlas).hexdigest(),
                    still_sha256=hashlib.sha256(still).hexdigest())
        writes.extend([(OUTPUT / f'{key}.png', atlas), (OUTPUT / f'{key}-still.png', still),
                       (OUTPUT / f'{key}.json', (json.dumps(meta, indent=2) + '\n').encode())])
    OUTPUT.mkdir(parents=True, exist_ok=True)
    for path, content in writes:
        path.write_bytes(content)
    return len(data['assets'])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('batches', nargs='+', type=Path)
    args = parser.parse_args()
    count = sum(save_batch(path) for path in args.batches)
    assets = [json.loads(path.read_text()) for path in sorted(OUTPUT.glob('*.json')) if path.name != 'catalogue.json']
    catalogue = {'schema': 1, 'cell': 256, 'assets': assets,
                 'total_png_bytes': sum(path.stat().st_size for path in OUTPUT.glob('*.png')),
                 'largest_decoded_atlas_bytes': max(a['decoded_atlas_bytes'] for a in assets)}
    (OUTPUT / 'catalogue.json').write_text(json.dumps(catalogue, indent=2) + '\n')
    print(f'original-art: saved {count} animations; catalogue has {len(assets)} assets')


if __name__ == '__main__':
    main()
