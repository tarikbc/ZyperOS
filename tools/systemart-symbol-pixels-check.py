#!/usr/bin/env python3
"""Check isolated, grid-aligned PICO-8 and TIC-80 animation layers."""
import argparse
import base64
import io
import json
from pathlib import Path
from PIL import Image

ROOT=Path(__file__).resolve().parents[1]/'res/system-art/original/animations'


def check(images):
    failures=[]
    pico=images['PICO8']
    a=pico.crop((148,125,158,180));b=pico.crop((404,125,414,180))
    changed=sum(x!=y for x,y in zip(a.getdata(),b.getdata()))
    if changed:failures.append(f'PICO-8: {changed} lettering pixels move with the flower')
    tic=images['TIC80']
    if tic.crop((0,138,256,256)).tobytes()!=tic.crop((256,138,512,256)).tobytes():
        failures.append('TIC-80: the wordmark changes with the code symbol')
    for key,rect in [('PICO8',(152,95,196,137)),('TIC80',(107,76,151,127))]:
        pixels=images[key].load();tested=bad=0
        for y in range(rect[1],rect[3]):
            for x in range(rect[0],rect[2]):
                source=pixels[x,y]
                if source[3]>200 and source[0]>150 and 40<source[1]<220 and source[2]<140:
                    tested+=1
                    bad+=source!=pixels[x+256,y-4]
        if tested<500:failures.append(f'{key}: too few opaque symbol pixels to check')
        if bad:failures.append(f'{key}: {bad} symbol pixels do not retain their color and grid position')
    assert not failures,'; '.join(failures)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--batch',type=Path,help='check a saved browser export instead of current PNGs')
    args=parser.parse_args()
    if args.batch:
        rows=json.loads(args.batch.read_text())['assets']
        images={a['key']:Image.open(io.BytesIO(base64.b64decode(a['atlas'].split(',',1)[1]))).convert('RGBA') for a in rows if a['key'] in ('PICO8','TIC80')}
    else:
        images={key:Image.open(ROOT/(key+'.png')).convert('RGBA') for key in ('PICO8','TIC80')}
    check(images)
    print('symbol-pixels: both symbols preserve their pixels; neighboring lettering stays fixed')


if __name__=='__main__':
    main()
