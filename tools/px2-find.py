#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""px2-find: the GKD Pixel 2 on this computer's networks, for PX2_HOST.

    tools/px2 find                      (or tools/px2-find.py)
    tools/px2-find.py --net 192.168.1.0/24 [--port 22]

Over Wi-Fi the Pixel answers to zyperos.local while SSH listens there
(src/mdns.h): Developer Mode's SSH over Wi-Fi on a release image, and a
development image's own SSH whenever its Wi-Fi has an address. That name is
tried first. Where it does not resolve (a network that drops multicast, or
the Pixel's lossy radio losing its probes), the Pixel is looked for as a
person would look: every address on this computer's own IPv4 networks (or
--net's), up to a /22, that takes a connection on port 22 and greets it as
dropbear does, the SSH server on the Pixel; then on each of those, with the
Pixel's key (PX2_KEY, ~/.ssh/zyperos_pixel2), two reads, the card's size and
ZyperOS's version. Nothing is written, nothing else is run, and a machine
whose SSH is not dropbear only ever sees one connection that reads its
greeting and closes.

Prints a line for each Pixel found, then the PX2_HOST= line for the first;
exits 1 when none answers. PX2_SSH stands in for ssh (make check-px2).
"""
import argparse
import asyncio
import ipaddress
import os
import re
import socket
import subprocess
import sys
import threading

NAME = 'zyperos.local'
OPTS = ['-o', 'ConnectTimeout=8', '-o', 'BatchMode=yes', '-o', 'StrictHostKeyChecking=no',
        '-o', 'UserKnownHostsFile=/dev/null', '-o', 'LogLevel=ERROR']
# The card's size, then ZyperOS's version: what tools/px2 reads to know it
ASK = 'cat /sys/block/mmcblk0/size 2> /dev/null || echo ?; cat /mnt/SDCARD/ZyperOS/VERSION 2> /dev/null || echo ?'
CARDS = {'62333952': 'the 32 GB test card', '249737216': "the owner's own 128 GB card"}


def by_name(seconds=4.0):
    """zyperos.local's IPv4 address, or None: the system's resolver (mDNS on
    a Mac), in a thread, as getaddrinfo has no timeout of its own"""
    got = []

    def ask():
        try:
            got.extend(a[4][0] for a in socket.getaddrinfo(NAME, 22, socket.AF_INET, socket.SOCK_STREAM))
        except OSError:
            pass
    t = threading.Thread(target=ask, daemon=True)
    t.start()
    t.join(seconds)
    return got[0] if got else None


def own_networks():
    """This computer's IPv4 networks, from ifconfig (macOS) or ip (Linux):
    no loopback, no link-local, none wider than a /22"""
    nets = []
    try:
        out = subprocess.run(['ifconfig'], capture_output=True, text=True, timeout=5).stdout
        for m in re.finditer(r'inet (\d+\.\d+\.\d+\.\d+) netmask (0x[0-9a-f]+)', out):
            nets.append(ipaddress.ip_interface(f'{m.group(1)}/{bin(int(m.group(2), 16)).count("1")}'))
    except (OSError, subprocess.SubprocessError):
        pass
    if not nets:
        try:
            out = subprocess.run(['ip', '-4', '-o', 'addr'], capture_output=True, text=True, timeout=5).stdout
            nets += [ipaddress.ip_interface(m.group(1)) for m in re.finditer(r'inet (\d+\.\d+\.\d+\.\d+/\d+)', out)]
        except (OSError, subprocess.SubprocessError):
            pass
    keep = []
    for i in nets:
        if i.ip.is_loopback or i.ip.is_link_local or i.network.prefixlen < 22:
            continue
        if i.network not in [k.network for k in keep]:
            keep.append(i)
    return keep


async def greeting(host, port, wait):
    """The SSH server's first line at host:port, b'' when it refuses, None
    when nothing answered in time"""
    try:
        r, w = await asyncio.wait_for(asyncio.open_connection(host, port), wait)
    except asyncio.TimeoutError:
        return None
    except OSError:
        return b''
    # A connection taken and no greeting in time is nothing answered in time
    # too, so the second pass asks it again: it was b'', a refusal, never
    # asked again, and a dropbear slow to greet (the check's own on a loaded
    # Mac, 2026-11-24; a Pixel on a bad radio) was not found
    try:
        line = await asyncio.wait_for(r.readline(), wait)
    except asyncio.TimeoutError:
        line = None
    except OSError:
        line = b''
    w.close()
    return line


async def sweep(hosts, port):
    """The hosts that greet as dropbear does. Twice for the ones that said
    nothing: the Pixel's radio loses a third of its packets on a bad day"""
    found, quiet = [], list(hosts)
    gate = asyncio.Semaphore(128)

    async def one(h, wait):
        async with gate:
            return h, await greeting(h, port, wait)
    for wait in (2.5, 4.0):
        if not quiet:
            break
        res = await asyncio.gather(*(one(h, wait) for h in quiet))
        quiet = [h for h, g in res if g is None]
        found += [h for h, g in res if g and g.startswith(b'SSH-2.0-dropbear')]
    return found


def pixel(host, port, key, ssh):
    """(card, version) when host takes the Pixel's key, else None"""
    cmd = [ssh] + OPTS + ['-p', str(port), '-i', key, f'root@{host}', ASK]
    try:
        p = subprocess.run(cmd, capture_output=True, text=True, timeout=30, stdin=subprocess.DEVNULL)
    except (OSError, subprocess.SubprocessError):
        return None
    lines = p.stdout.split('\n')
    if p.returncode != 0 or len(lines) < 2:
        return None
    return lines[0].strip(), lines[1].strip()


def main():
    ap = argparse.ArgumentParser(description='The Pixel 2 on this computer\'s networks, for PX2_HOST.')
    ap.add_argument('--net', action='append', help='a network to look in (CIDR), in place of this computer\'s own')
    ap.add_argument('--port', type=int, default=int(os.environ.get('PX2_PORT', '22')))
    ap.add_argument('--no-name', action='store_true', help='skip zyperos.local')
    a = ap.parse_args()
    key = os.environ.get('PX2_KEY', os.path.expanduser('~/.ssh/zyperos_pixel2'))
    ssh = os.environ.get('PX2_SSH', 'ssh')

    tried = []
    if not a.no_name:
        ip = by_name()
        if ip:
            print(f'px2-find: {NAME} is {ip}')
            tried.append(ip)
        else:
            print(f'px2-find: {NAME} does not resolve; looking on the networks')
    if a.net:
        nets = [ipaddress.ip_network(n, strict=False) for n in a.net]
    else:
        nets = [i.network for i in own_networks()]
        if not nets:
            print('px2-find: this computer has no IPv4 network of /22 or smaller to look in', file=sys.stderr)
    hosts = [str(h) for n in nets for h in (n.hosts() if n.num_addresses > 1 else [n.network_address])]
    if nets:
        print(f'px2-find: looking for dropbear on port {a.port} in {", ".join(map(str, nets))} ({len(hosts)} addresses)')
    cands = tried + [h for h in asyncio.run(sweep(hosts, a.port)) if h not in tried] if hosts else tried
    found = []
    for h in cands:
        got = pixel(h, a.port, key, ssh)
        if got is None:
            print(f'  {h}: dropbear, but not the Pixel\'s key (or no answer)')
            continue
        card, ver = got
        print(f'  {h}: ZyperOS {ver}, {CARDS.get(card, f"a card of {card} sectors")}')
        found.append(h)
    if not found:
        print('px2-find: no Pixel answered. Is SSH on there? (A release image: Developer Mode, SSH over Wi-Fi on, a key on the card)', file=sys.stderr)
        return 1
    print(f'PX2_HOST={found[0]}' + (f' PX2_PORT={a.port}' if a.port != 22 else ''))
    return 0


if __name__ == '__main__':
    sys.exit(main())
