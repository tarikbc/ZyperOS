#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""An R36S panel's overlay, from the device tree its own firmware boots.

    python3 plastron/scripts/r36s-panel.py STOCK.dtb [OUT.dtbo]

R36S units ship with several panels (ZyperOS's docs/devices/r36s.md,
"Panels"), and the one ZyperOS's device tree drives is only the first. The
firmware a unit came with knows its own: its device tree's panel node holds
the panel's init sequence, its timings, its delays and its DSI settings.
This reads them out of that file (STOCK.dtb, from the unit's own card, or a
.dts) and writes a device tree overlay that puts them on ZyperOS's panel for
ROCKNIX's generic-dsi driver, which plastron's R36S kernel has built in:
OUT.dtbo, overlays/mipi-panel.dtbo by default. Copy it to the R36S card's
boot partition (R36SBOOT) as overlays/mipi-panel.dtbo, where its boot script
lays it over the device tree (board/r36s/boot-prelude.cmd).

Needs dtc (Device Tree Compiler: `brew install dtc`, `apt install
device-tree-compiler`). Written from the formats alone: the Rockchip BSP's
panel-init-sequence (each command its data type, its delay in ms and its
length, then its bytes) and generic-dsi's panel_description lines (G for
the panel's globals, M for a mode, I for an init command), as plastron's
kernel patch 4-rocknix-panel-generic-dsi.patch reads them.
"""
import os
import re
import subprocess
import sys


def dts_of(path):
    """The device tree as text: a .dts as it is, a .dtb through dtc"""
    with open(path, 'rb') as f:
        head = f.read(4)
    if head == b'\xd0\x0d\xfe\xed':
        return subprocess.run(['dtc', '-q', '-I', 'dtb', '-O', 'dts', path], check=True,
                              capture_output=True, text=True).stdout
    with open(path, encoding='utf-8', errors='replace') as f:
        return f.read()


def node_around(text, at):
    """The node that holds position `at`: its body, from its { to its }"""
    depth, i = 0, at
    while i > 0:
        i -= 1
        if text[i] == '}':
            depth += 1
        elif text[i] == '{':
            if depth == 0:
                break
            depth -= 1
    start, depth, j = i, 0, i
    while j < len(text):
        if text[j] == '{':
            depth += 1
        elif text[j] == '}':
            depth -= 1
            if depth == 0:
                return text[start:j + 1]
        j += 1
    sys.exit('r36s-panel: the panel node does not end')


def cells(body, name, default=None):
    """A property's <cells> as integers, from the node's own lines only"""
    m = re.search(r'^\s*' + re.escape(name) + r'\s*=\s*<([^>]*)>;', body, re.M)
    if not m:
        return default
    return [int(v, 0) for v in m.group(1).split()]


def one(body, name, default=None):
    v = cells(body, name)
    return v[0] if v else default


def own_lines(body):
    """The node's own properties, without its children's"""
    out, depth = [], 0
    for line in body.splitlines()[1:]:
        if '{' in line:
            depth += 1
        if depth == 0:
            out.append(line)
        if '}' in line:
            depth -= 1
    return '\n'.join(out)


def timings(body):
    """Each mode under display-timings: (name, phandle, its numbers)"""
    m = re.search(r'display-timings\s*\{', body)
    if not m:
        sys.exit('r36s-panel: the panel node has no display-timings')
    dt = node_around(body, m.end())
    native = one(own_lines(dt), 'native-mode')
    modes = []
    # A mode node, with its label when dtc -@ kept one ("timing0: original {")
    for mm in re.finditer(r'\n\s*(?:[\w-]+:\s*)?([\w@,.-]+)\s*\{', dt[1:]):
        sub = node_around(dt, mm.end() + 1)
        get = lambda n: one(sub, n, 0)
        modes.append((mm.group(1), get('phandle'), {
            'clock': get('clock-frequency') // 1000,
            'h': (get('hactive'), get('hfront-porch'), get('hsync-len'), get('hback-porch')),
            'v': (get('vactive'), get('vfront-porch'), get('vsync-len'), get('vback-porch')),
        }))
    if not modes:
        sys.exit('r36s-panel: display-timings has no mode')
    return modes, native


def init_lines(seq):
    """The BSP's panel-init-sequence as generic-dsi's I lines: each command
    the bytes it sends (seq=), and the wait after it (wait=, in hex, as the
    driver reads it)"""
    out, i = [], 0
    while i + 3 <= len(seq):
        kind, delay, n = seq[i], seq[i + 1], seq[i + 2]
        data = seq[i + 3:i + 3 + n]
        if len(data) < n:
            sys.exit('r36s-panel: panel-init-sequence ends inside a command')
        if kind not in (0x05, 0x15, 0x39, 0x03, 0x13, 0x23, 0x29):
            sys.exit('r36s-panel: unknown command type 0x%02x in panel-init-sequence' % kind)
        line = 'I seq=' + ''.join('%02x' % b for b in data)
        if delay:
            line += ' wait=%x' % delay
        out.append(line)
        i += 3 + n
    return out


def main():
    if len(sys.argv) not in (2, 3):
        sys.exit(__doc__.split('\n\n')[1])
    src = sys.argv[1]
    dst = sys.argv[2] if len(sys.argv) == 3 else 'overlays/mipi-panel.dtbo'
    text = dts_of(src)
    m = re.search(r'panel-init-sequence\s*=\s*\[([0-9a-fA-F\s]*)\]', text)
    if not m:
        sys.exit('r36s-panel: no panel-init-sequence in %s: not an R36S firmware\'s device tree?' % src)
    body = node_around(text, m.start())
    props = own_lines(body)
    seq = [int(b, 16) for b in m.group(1).split()]
    modes, native = timings(body)
    width, height = one(props, 'width-mm', 52), one(props, 'height-mm', 70)
    # The panel's power-up waits and DSI flags as the overlay that lit
    # board R36S-V12's panel 4 has them (ZyperOS #478): prepare 20 ms, reset
    # 150, init 20, enable 120, ready 20, and the DSI clock off between
    # transfers (MIPI_DSI_CLOCK_NON_CONTINUOUS, 0x400) over the stock tree's
    # flags. The stock trees' own delay properties are the BSP driver's, which
    # counts its waits differently, and generic-dsi's would reset for 1 ms.
    delays = (20, 150, 20, 120, 20)
    fmt = {0: 'rgb888', 1: 'rgb666', 2: 'rgb666_packed', 3: 'rgb565'}.get(one(props, 'dsi,format', 0), 'rgb888')
    lines = ['G size=%d,%d delays=%s format=%s lanes=%d flags=0x%x' % (
        width, height, ','.join(str(d) for d in delays), fmt, one(props, 'dsi,lanes', 4),
        one(props, 'dsi,flags', 0x3) | 0x400)]
    # One mode, the one nearest 60 Hz: several stock trees make the native
    # one 68 to 78 Hz ("original") and keep a 60 Hz one beside it
    # ("adjust"), which games want; the others' extra modes keep the clock
    # with porches that do not give their rates
    rate = lambda t: t['clock'] * 1000.0 / (sum(t['h']) * sum(t['v']))
    t = min((t for _, _, t in modes), key=lambda t: abs(rate(t) - 60))
    modes = [(None, None, t)]
    lines.append('M clock=%d horizontal=%s vertical=%s default=1' % (
        t['clock'], ','.join(map(str, t['h'])), ','.join(map(str, t['v']))))
    lines += init_lines(seq)
    dts = ['/dts-v1/;', '/plugin/;', '',
           '/* An R36S panel, from %s, by plastron/scripts/r36s-panel.py */' % os.path.basename(src),
           '&internal_display {', '\tcompatible = "rocknix,generic-dsi";',
           '\tpanel_description =']
    dts += ['\t\t"%s"%s' % (l, ',' if k < len(lines) - 1 else ';') for k, l in enumerate(lines)]
    dts += ['};', '']
    os.makedirs(os.path.dirname(dst) or '.', exist_ok=True)
    out = dst[:-5] + '.dts' if dst.endswith('.dtbo') else dst + '.dts'
    with open(out, 'w') as f:
        f.write('\n'.join(dts))
    subprocess.run(['dtc', '-q', '-@', '-I', 'dts', '-O', 'dtb', '-o', dst, out], check=True)
    print('r36s-panel: %s: %d init commands, %d mode(s), %dx%d %s' % (
        dst, len(lines) - 1 - len(modes), len(modes), modes[0][2]['h'][0], modes[0][2]['v'][0], fmt))
    print('r36s-panel: copy it to the R36S card\'s boot partition as overlays/mipi-panel.dtbo')


if __name__ == '__main__':
    main()
