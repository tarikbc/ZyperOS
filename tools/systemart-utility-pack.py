#!/usr/bin/env python3
"""Save the browser-rendered PortMaster covers and folder size variants."""
import base64
import hashlib
import json
from pathlib import Path
import struct
import sys

ROOT=Path(__file__).resolve().parents[1]/'res/system-art/original'
SIZES={
    'portmaster-icon.png':(256,256),
    'portmaster-cover-light.png':(512,656),
    'portmaster-cover-dark.png':(512,656),
    'folder-large.png':(256,256),
    **{f'folder-small-{size}.png':(size,size) for size in (24,32,40,48,64)}
}
data=json.loads(Path(sys.argv[1]).read_text())
assert data['schema']==1
assert {row['file'] for row in data['images']}==set(SIZES)
assert len(data['images'])==len(SIZES)
writes=[]
for row in data['images']:
    assert (row['width'],row['height'])==SIZES[row['file']]
    prefix='data:image/png;base64,'
    assert row['png'].startswith(prefix)
    raw=base64.b64decode(row['png'][len(prefix):],validate=True)
    assert raw[:8]==b'\x89PNG\r\n\x1a\n'
    assert struct.unpack('>II',raw[16:24])==SIZES[row['file']]
    row['sha256']=hashlib.sha256(raw).hexdigest()
    del row['png'];writes.append((row['file'],raw))
for category in ('sourceFiles','rendererFiles'):
    hashes={}
    for name in data[category]:
        path=(ROOT/name).resolve();assert ROOT.resolve() in path.parents
        hashes[name]=hashlib.sha256(path.read_bytes()).hexdigest()
    data[category]=hashes
out=ROOT/'utility';out.mkdir(exist_ok=True)
for name,raw in writes:(out/name).write_bytes(raw)
(out/'manifest.json').write_text(json.dumps(data,indent=2)+'\n')
print(f'utility-art: saved {len(writes)} images')
