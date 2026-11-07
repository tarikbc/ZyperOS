#!/usr/bin/env python3
"""Compile the actual portkeys reader and loop with deterministic syscall doubles."""
from pathlib import Path
import os
import shlex
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'tools/portkeys.c').read_text()
# Fail loudly if these boundaries move. No copied production statements live here.
start = source.index('/* Devices:')
main = source.index('int main(int argc, char **argv)')
loop = source.index('\n\tfor (;;) {\n\t\tstatic pk_key keys[PK_PASS_MAX];', main)
with tempfile.TemporaryDirectory(prefix='portkeys-flow-') as tmp:
    generated = Path(tmp) / 'portkeys-flow.inc'
    generated.write_text(
        f'#line {source[:start].count(chr(10)) + 1} "tools/portkeys.c"\n'
        + source[start:main]
        + '\nstatic int run_loop(void)\n{\n\tint i;\n'
        + f'#line {source[:loop].count(chr(10)) + 1} "tools/portkeys.c"\n'
        + source[loop:])
    binary = str(Path(tmp) / 'check')
    command = shlex.split(os.environ.get('CC', 'cc'))
    command += shlex.split(os.environ.get('PK_FLOW_CFLAGS', '-O2 -Wall -Wextra -Werror'))
    command += ['-std=gnu11', '-I' + tmp, '-I' + str(root / 'src'),
                '-I' + str(root / 'tools'), str(root / 'tools/portkeys-flow-check.c'),
                str(root / 'tools/pkkeys.c'), '-lsqlite3', '-o', binary]
    subprocess.run(command, check=True)
    subprocess.run([binary], check=True, timeout=15)
