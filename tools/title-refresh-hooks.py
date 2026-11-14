#!/usr/bin/env python3
"""Compile the production refresh functions against small headless view stubs.

This checks the real hooks instead of a second implementation of the refresh.
A missing or changed function signature fails generation rather than skipping.
"""
from pathlib import Path
import re
import sys

source = Path(sys.argv[1]).read_text()
functions = []
for name in ('sort_shelf', 'refresh_game_titles', 'scraper_page'):
    # sort_shelf is not static: src/shelfbuild.c sorts too (src/app.h)
    pattern = rf'^(?:static )?(?:void|menu_result) {name}\([^\n]*(?:\n[^{{]*)?\n?\{{\n.*?^\}}'
    match = re.search(pattern, source, re.M | re.S)
    if not match:
        raise SystemExit(f'Cannot extract production function: {name}')
    functions.append(match.group())
Path(sys.argv[2]).write_text('\n\n'.join(functions) + '\n')
