#!/usr/bin/env python3
"""The GKD Pixel 2's DDR from U-Boot to init, offline (make check-px2-dmc-boot).

plastron's DMC driver (drivers/devfreq/px30_dmc.c) as Buildroot builds it: the
board's kernel patches that touch it, applied in LC_ALL=C order of their names
as Buildroot applies them, into a scratch folder. Then what keeps the DDR at
the rate U-Boot left it at until user space asks for another
(9-px2-px30-dmc-boot-rate.patch): the probe starts on performance (or
userspace), never simple_ondemand, whose first look 50 ms after the probe
moved the DDR from 1056 to 328 MHz about 0.46 s into the kernel, where some
Pixel 2s crashed on cold starts (2026-11-21 to 26). performance holds the boot
rate only when that rate is the device tree's top DMC step, so the top step
must be the top rate U-Boot's FSP_INIT trains for this board's LPDDR4
(u-boot/patches/0004-px2-px30s-dmc-fsp.patch), and every step one it trains.
The kernel must have both governors built in. vcc_ddr has no voltage in the
device tree, so Linux never sets one on it (its "at 500 mV" at boot is the
RK817's unused register, not what the DDR gets). init's datapart still sets
performance before the card's first work.
"""
import os
import re
import subprocess
import sys
import tempfile

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "buildroot", "board", "px2")
PATCHES = os.path.join(ROOT, "patches", "linux")
DTS = os.path.join(ROOT, "dts", "rk3326s-gkd-pixel2.dts")
CONFIG = os.path.join(ROOT, "linux.config")
FSP = os.path.join(ROOT, "u-boot", "patches", "0004-px2-px30s-dmc-fsp.patch")
DATAPART = os.path.join(ROOT, "rootfs-overlay", "etc", "init.d", "datapart")
TARGET = "drivers/devfreq/px30_dmc.c"

failed = []


def check(ok, what):
    print(("ok   " if ok else "FAIL ") + what)
    if not ok:
        failed.append(what)


def sections(text, path):
    """The parts of a patch that change path, each from its ---/+++ pair."""
    out, keep = [], False
    lines = text.split("\n")
    for i, line in enumerate(lines):
        if line.startswith("diff "):
            keep = path in line
        elif line.startswith("--- ") and i + 1 < len(lines) and lines[i + 1].startswith("+++ "):
            keep = path in line or path in lines[i + 1]
        elif line == "-- ":
            keep = False
        if keep:
            out.append(line)
    return "\n".join(out) + "\n"


def driver():
    names = sorted(os.listdir(PATCHES), key=lambda n: n.encode())
    used = []
    with tempfile.TemporaryDirectory() as tmp:
        for name in names:
            if not name.endswith(".patch"):
                continue
            with open(os.path.join(PATCHES, name)) as f:
                text = f.read()
            if TARGET not in text:
                continue
            r = subprocess.run(["patch", "-p1", "-s", "-F0", "--no-backup-if-mismatch"],
                               cwd=tmp, input=sections(text, TARGET), text=True,
                               capture_output=True)
            if r.returncode:
                check(False, "%s applies to %s after %s" % (name, TARGET, ", ".join(used) or "nothing"))
                print(r.stdout + r.stderr)
                return None
            used.append(name)
        print("     %s from %s" % (TARGET, ", ".join(used)))
        with open(os.path.join(tmp, TARGET)) as f:
            return f.read()


def function(src, name):
    m = re.search(r"^static int %s\(.*?^}" % re.escape(name), src, re.S | re.M)
    return m.group(0) if m else ""


def node(text, label):
    m = re.search(r"%s[^{]*\{(.*?)\n\t\};" % re.escape(label), text, re.S)
    return m.group(1) if m else ""


src = driver()
if src is not None:
    probe = function(src, "px30_dmc_probe")
    check(bool(probe), "px30_dmc_probe found")
    add = re.search(r"devm_devfreq_add_device\(([^;]*)\);", probe)
    args = add.group(1) if add else ""
    check(bool(add), "the probe adds the devfreq device")
    check("SIMPLE_ONDEMAND" not in args,
          "the probe does not start on simple_ondemand (it switched the DDR 50 ms later)")
    gov = re.search(r"\bgov\s*=\s*([^;]*);", probe)
    choice = gov.group(1) if gov else ""
    check(re.search(r"devm_devfreq_add_device\(\s*dev\s*,\s*&dmc->profile\s*,\s*gov\s*,", probe) is not None
          and "DEVFREQ_GOV_PERFORMANCE" in choice and "DEVFREQ_GOV_USERSPACE" in choice
          and "SIMPLE_ONDEMAND" not in choice,
          "the probe starts on performance or userspace")
    check(re.search(r"dmc->rate\s*==\s*max\s*\?\s*DEVFREQ_GOV_PERFORMANCE", choice) is not None
          and "dev_pm_opp_find_freq_floor(dev, &max)" in probe,
          "performance only when the loader's rate is the top step, userspace otherwise")
    target = function(src, "px30_dmc_target")
    check(re.search(r"if \(rate == old\)\s*return 0;", target) is not None,
          "px30_dmc_target() returns at once for the rate the DDR already runs at")

with open(CONFIG) as f:
    config = f.read()
for opt in ("CONFIG_ARM_PX30_DMC_DEVFREQ", "CONFIG_DEVFREQ_GOV_PERFORMANCE", "CONFIG_DEVFREQ_GOV_USERSPACE"):
    check(re.search(r"^%s=y$" % opt, config, re.M) is not None, "%s=y in linux.config" % opt)

with open(DTS) as f:
    dts = f.read()
dmc = re.search(r"^&dmc \{(.*?)^\};", dts, re.S | re.M)
dmc = dmc.group(1) if dmc else ""
check("operating-points-v2 = <&px30s_dmc_opp_table>;" in dmc and 'status = "okay";' in dmc,
      "the board's dmc runs on px30s_dmc_opp_table")
table = node(dts, "px30s_dmc_opp_table: opp-table-dmc-px30s")
rates = [int(x) for x in re.findall(r"opp-hz = /bits/ 64 <(\d+)>;", table)]
check(len(rates) >= 1, "px30s_dmc_opp_table has steps: %s" % rates)

with open(FSP) as f:
    fsp = f.read()
m = re.search(r"px30s_fsp_lpddr4\[\] = \{(.*?)\};", fsp, re.S)
words = [int(x, 16) for x in re.findall(r"0x([0-9a-f]+)", m.group(1))] if m else []
# ddr_params_v1 after its version word: the top rate twice (f0, f3), then f1, f2, in MHz
trained = sorted({words[3], words[4], words[5], words[6]}) if len(words) > 6 else []
check(bool(trained) and words[3] == words[6], "U-Boot's FSP_INIT trains LPDDR4 at %s MHz" % trained)
if rates and trained:
    check(max(rates) == max(trained) * 1000000,
          "the top DMC step (%d) is the rate FSP_INIT leaves the DDR at (%d MHz)" % (max(rates), max(trained)))
    check(all(r % 1000000 == 0 and r // 1000000 in trained for r in rates),
          "every DMC step is one FSP_INIT trains")

vcc_ddr = re.search(r"vcc_ddr: DCDC_REG3 \{(.*?)\n\t\t\t\};", dts, re.S)
vcc_ddr = vcc_ddr.group(1) if vcc_ddr else None
check(vcc_ddr is not None and "microvolt" not in vcc_ddr.split("regulator-state-mem")[0],
      "vcc_ddr has no voltage in the device tree, so Linux sets none on it")

with open(DATAPART) as f:
    check("echo performance > /sys/class/devfreq/dmc/governor" in f.read(),
          "datapart still sets the DMC's performance governor before the card's first work")

if failed:
    print("px2-dmc-boot-check: %d failed" % len(failed))
    sys.exit(1)
print("px2-dmc-boot-check: all passed")
