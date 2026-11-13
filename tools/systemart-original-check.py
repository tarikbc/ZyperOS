#!/usr/bin/env python3
"""Check original art coverage, PNG hashes, dimensions, and frame budgets."""
import argparse
import hashlib
import json
from pathlib import Path

from PIL import Image, ImageChops

ROOT = Path(__file__).resolve().parents[1]
FOLDERS = {'ROMS','BIOS','SAVES','STATES','BGM','VIDEOS','BOOKS','FOLDER'}
SUPPLEMENTAL = {'PORTMASTER','FOLDER','FOLDER_SMALL'}
TWO_POSE = {'FAVORITES','MUSE','ARCADE','SCUMMVM','MAME','PICO8','OPENBOR','PORTS','DOOM','TIC80','PORTMASTER'}
LIDS = {'SATURN','DC','NEOCD','SEGACD','NES'}
CARTRIDGES = {'32X','A2600','A5200','A7800','GENESIS','N64','NEOGEO','PCE','SMS','VB','COLECO','MSX2'}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--partial', action='store_true', help='allow a work-in-progress catalogue')
    args = parser.parse_args()
    expected = ({row['key'] for row in json.loads((ROOT/'res/system-art/manifest.json').read_text())['assets']} - FOLDERS) | SUPPLEMENTAL
    directory = ROOT/'res/system-art/original/animations'
    data = json.loads((directory/'catalogue.json').read_text())
    assets = data['assets']
    actual = {a['key'] for a in assets}
    assert len(actual) == len(assets), 'duplicate animation key'
    assert actual <= expected, f'unknown systems: {actual-expected}'
    if not args.partial:
        assert actual == expected, f'missing systems: {sorted(expected-actual)}'
    for asset in assets:
        key = asset['key']
        if not args.partial:
            assert asset.get('input_sha256'), f'{key}: source hashes are missing'
            assert asset.get('renderer_sha256'), f'{key}: renderer hashes are missing'
        for name, digest in asset.get('input_sha256', {}).items():
            assert hashlib.sha256((directory.parent/name).read_bytes()).hexdigest() == digest, f'{key}: source changed after export'
        for name, digest in asset.get('renderer_sha256', {}).items():
            assert hashlib.sha256((directory.parent/name).read_bytes()).hexdigest() == digest, f'{key}: renderer changed after export'
        assert asset['cell'] == 256
        assert (1 if asset.get('static') else 2) <= asset['unique'] <= 64
        if asset.get('expected_poses'):
            assert asset['unique'] == asset['expected_poses'], f'{key}: wrong pose count'
        assert 1 <= len(asset['frames']) <= 72
        assert sum(f['ms'] for f in asset['frames']) == asset['duration_ms']
        assert 0 < asset['duration_ms'] <= 60000
        if key == 'GBA' and not args.partial:
            assert asset['duration_ms'] == 3900, 'GBA: old placeholder timeline remains'
            assert 'gba-startup.js' in asset['renderer_sha256'], 'GBA: completed startup module is missing'
        if not args.partial:
            if key in TWO_POSE:
                assert asset['unique']==2 and asset.get('expected_poses')==2, f'{key}: expected two object poses'
                assert asset['frames']==[{'tile':0,'ms':400},{'tile':1,'ms':400}], f'{key}: wrong two-frame timing'
            if key in LIDS:
                assert 'groups/console-lids.js' in asset['renderer_sha256'], f'{key}: mechanical lid/tray action is missing'
            if key in CARTRIDGES:
                assert 'groups/console-cartridges.js' in asset['renderer_sha256'], f'{key}: cartridge loading is missing'
            if key=='MUSE':
                assert asset['name']=='MP4', 'MP4 has the wrong display name'
        for frame in asset['frames']:
            assert 0 <= frame['tile'] < asset['unique']
            assert 10 <= frame['ms'] <= 2000
        for kind in ('atlas','still'):
            path=directory/asset[kind]
            assert hashlib.sha256(path.read_bytes()).hexdigest() == asset[kind+'_sha256'], f'{key}: hash mismatch'
            with Image.open(path) as image:
                size=(asset['cols']*256,asset['rows']*256) if kind=='atlas' else (256,256)
                assert image.size == size, f'{key}: wrong {kind} size'
                assert image.mode == 'RGBA', f'{key}: transparency is missing'
                assert image.getchannel('A').getextrema()[0] == 0, f'{key}: no transparent background'
                if kind=='atlas':
                    assert image.width*image.height*4 == asset['decoded_atlas_bytes']
                    assert asset['decoded_atlas_bytes'] <= 16*1024*1024
                    hashes=[]
                    baseline=image.crop((0,0,256,256))
                    mx,my,mw,mh=asset.get('motion_bounds',[0,0,256,256])
                    for i in range(asset['unique']):
                        x=(i%asset['cols'])*256;y=(i//asset['cols'])*256
                        tile=image.crop((x,y,x+256,y+256))
                        assert tile.getchannel('A').getextrema()[1] > 0, f'{key}: empty animation frame'
                        solid=tile.getchannel('A').point(lambda value:255 if value>127 else 0).getbbox()
                        assert solid and solid[0]>0 and solid[1]>0 and solid[2]<256 and solid[3]<256, f'{key}: solid pixels touch the frame edge'
                        diff=ImageChops.difference(tile,baseline)
                        for channel in diff.split():
                            bounds=channel.getbbox()
                            if bounds:
                                assert bounds[0]>=mx and bounds[1]>=my and bounds[2]<=mx+mw and bounds[3]<=my+mh, f'{key}: pixels changed outside motion bounds'
                        hashes.append(hashlib.sha256(tile.tobytes()).digest())
                    assert len(set(hashes)) == len(hashes), f'{key}: duplicated unique frames'
    if not args.partial:
        utility=ROOT/'res/system-art/original/utility'
        record=json.loads((utility/'manifest.json').read_text())
        assert len(record['images'])==9, 'utility artwork is incomplete'
        for category in ('sourceFiles','rendererFiles'):
            for name,digest in record[category].items():
                assert hashlib.sha256((utility.parent/name).read_bytes()).hexdigest()==digest, f'utility: changed input {name}'
        for entry in record['images']:
            path=utility/entry['file']
            assert hashlib.sha256(path.read_bytes()).hexdigest()==entry['sha256']
            with Image.open(path) as image:
                assert image.size==(entry['width'],entry['height']) and image.mode=='RGBA'
                assert image.getchannel('A').getextrema()[0]==0
                if entry['file'].startswith('folder-small-'):
                    solid=image.getchannel('A').point(lambda v:255 if v>127 else 0).getbbox()
                    assert solid and solid[0]>0 and solid[1]>0 and solid[2]<image.width and solid[3]<image.height
    print(f'original-art: {len(assets)}/{len(expected)} designs; hashes, alpha, frames, and budgets verified')


if __name__ == '__main__':
    main()
