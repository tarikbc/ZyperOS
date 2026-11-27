#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""No device guessed: the launcher asks the profile what the handheld has and
the platform what the system has, never a file's presence or a stub's answer
(docs/building.md, Devices).

In src/*.c and src/*.h, outside src/device/ and src/profile.c, it fails on:
  - access("/usr/sbin/...") - what the system has is plat_system_has()'s;
  - plat_usb_peer() compared with USB_PEER_UNKNOWN as a capability (a port
    that cannot tell what is plugged in is not a device's identity);
  - the string literals "pixel2", "brick" and "smartpro" - a device by name;
  - device_profile_pixel2, _brick, _smartpro or _desktop - a device's profile by name,
    which a device build does not even link (it has its own alone);
  - td_model_pixel2 and the like - a handheld's drawing by name, for the
    same reason (plat_profile()->model is the one in force).
src/usblink.c may still compare with USB_PEER_UNKNOWN: it acts on the peer
it is told, not on which device it runs on.

    python3 tools/devices-lint.py        (make check-devices runs it)
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / 'src'

RULES = [
    (re.compile(r'access\(\s*("/usr/sbin/|HOSTAPD\b)'), 'a system program by its file: use plat_system_has()'),
    (re.compile(r'plat_usb_peer\(\)\s*[!=]=\s*USB_PEER_UNKNOWN|USB_PEER_UNKNOWN\s*[!=]=\s*plat_usb_peer\(\)'),
     'the USB peer as a device guess: use the profile (usb_shared_port, wifi)'),
    (re.compile(r'"(pixel2|brick|smartpro)"'), 'a device by name: ask the profile'),
    (re.compile(r'\bdevice_profile_(pixel2|brick|smartpro|desktop)\b'),
     'a device\'s profile by name (a device build links its own alone): use plat_profile() or plat_profile_at()'),
    (re.compile(r'\btd_model_(pixel2|brick|smartpro|desktop)\b'),
     'a handheld\'s model by name (a device build links its own alone): use plat_profile()->model'),
]
# Files a rule does not apply to, each with its reason
ALLOW = {
    ('src/usblink.c', 1): 'acts on the peer it is told about',
}


def strip_comments(text):
    """C comments out, newlines kept, so line numbers stay true"""
    out, i, n = [], 0, len(text)
    while i < n:
        if text.startswith('/*', i):
            j = text.find('*/', i + 2)
            j = n if j < 0 else j + 2
            out.append(re.sub(r'[^\n]', ' ', text[i:j]))
            i = j
        elif text.startswith('//', i):
            j = text.find('\n', i)
            j = n if j < 0 else j
            out.append(' ' * (j - i))
            i = j
        elif text[i] == "'":
            # a character literal, '"' among them, kept as it is
            j = i + 1
            while j < n and text[j] != "'":
                j += 2 if text[j] == '\\' else 1
            out.append(text[i:j + 1])
            i = j + 1
        elif text[i] == '"':
            j = i + 1
            while j < n and text[j] != '"':
                j += 2 if text[j] == '\\' else 1
            out.append(text[i:j + 1])
            i = j + 1
        else:
            out.append(text[i])
            i += 1
    return ''.join(out)


def main():
    bad = 0
    for path in sorted(SRC.glob('*.[ch]')):
        rel = path.relative_to(ROOT).as_posix()
        if rel == 'src/profile.c':
            continue
        text = strip_comments(path.read_text(errors='replace'))
        for k, (rx, why) in enumerate(RULES):
            if (rel, k) in ALLOW:
                continue
            for m in rx.finditer(text):
                line = text.count('\n', 0, m.start()) + 1
                print(f'FAIL {rel}:{line}: {why}')
                bad += 1
    if bad:
        print(f'devices-lint: {bad} device guess(es)')
        return 1
    print('devices-lint: no device guessed in src/')
    return 0


if __name__ == '__main__':
    sys.exit(main())
