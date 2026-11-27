"""The TrimUI Smart Pro's device tree (board/smartpro/dts), written from the
Brick's (board/brick/dts) and TrimUI's own Smart Pro tree, taking from
TrimUI's only the values where the two boards differ: the battery, the USB
input's Type-C detection, the headphone detect case, the panel's mode, fb0
and its turn, and the CPU's trip points. TrimUI's tree is the builder's own
copy, never in this repository: its sunxi.fex from the firmware image,
decompiled and sorted (dtc -s -I dtb -O dts).

  python3 scripts/make-smartpro-dts.py \
      buildroot/board/brick/dts/sun50iw10p1-trimui-brick.dts \
      SMARTPRO-sorted.dts buildroot/board/smartpro/dts/sun50iw10p1-trimui-smartpro.dts
"""
import re,sys
brick=open(sys.argv[1]).read(); sp=open(sys.argv[2]).read()
L=r"\n\t+(?:[\w]+: )?"
def node(text, name):
    m=re.search(L+re.escape(name)+r" \{", text)
    assert m, name
    i=m.end(); depth=1
    while depth:
        c=text[i]
        if c=="{": depth+=1
        elif c=="}": depth-=1
        i+=1
    return m.start(), i
def props(text, name):
    a,b=node(text, name)
    return dict(re.findall(r"^\t+([\w,#.+-]+) = (.*);$", text[a:b], flags=re.M))
out=brick
def set_in(name, changes, drop=(), add=()):
    global out
    a,b=node(out, name); seg=out[a:b]
    for k,v in changes.items():
        pat=re.compile(r"^(\t+)%s = [^;]*;$" % re.escape(k), re.M)
        assert len(pat.findall(seg))==1,(name,k,len(pat.findall(seg)))
        seg=pat.sub(lambda m: "%s%s = %s;" % (m.group(1),k,v), seg)
    for k in drop:
        pat=re.compile(r"^\t+%s = [^;]*;\n" % re.escape(k), re.M)
        assert len(pat.findall(seg))==1,(name,k)
        seg=pat.sub("", seg)
    for anchor,line in add:
        pat=re.compile(r"^(\t+)%s = [^;]*;$" % re.escape(anchor), re.M)
        m=pat.search(seg); assert m,(name,anchor)
        seg=seg[:m.end()]+"\n%s%s;" % (m.group(1),line)+seg[m.end():]
    out=out[:a]+seg+out[b:]
spb=props(sp,"bat-power-supply"); brb=props(brick,"bat-power-supply")
ch={k:v for k,v in spb.items() if k.startswith("pmu_") and k in brb and brb[k]!=v}
extra=[k for k in spb if k.startswith("pmu_") and k not in brb]
assert not extra, extra
dropped=[k for k in brb if k.startswith("pmu_") and k not in spb]
set_in("bat-power-supply", ch, drop=dropped)
print("battery:", len(ch), "values; dropped", dropped)
set_in("usb_power_supply", {"pmu_usb_typec_used": "<0x00>"})
set_in("sound@0", {"hp_detect_case": "<0x00>"})
spl=props(sp,"lcd0@01c0c000"); brl=props(brick,"lcd0@01c0c000")
lch={k:v for k,v in spl.items() if k.startswith("lcd_") and k in brl and brl[k]!=v}
print("panel:", lch)
set_in("lcd0@01c0c000", lch)
set_in("disp@06000000", {"fb0_width": "<0x500>", "fb0_height": "<0x2d0>"},
       add=[("fb0_height", "disp_rotation_used = <0x01>"), ("disp_rotation_used", "degree0 = <0x03>")])
for name in ("trip-point@0","trip-point@1","cpu_crit@0"):
    v=props(sp,name)["temperature"]; set_in(name, {"temperature": v}); print(name, v)
head='''/*
 * The TrimUI Smart Pro (Allwinner A133P): its device tree, as source.
 *
 * The Brick's (board/brick/dts/sun50iw10p1-trimui-brick.dts), which TrimUI's
 * own Smart Pro tree matches in nearly every node, with the Smart Pro's
 * values where TrimUI's tree for it (firmware 1.1.1) has others: the
 * panel's mode (720x1280 at 69 MHz, four lanes), fb0 at 1280x720 turned onto
 * it by the G2D (disp_rotation_used, degree0 3 = 270 degrees, as TrimUI's
 * kernel turns it), its battery (5000 mAh and its own table), no Type-C
 * detection on the USB input, the CPU's trip points (70, 80, 110 C) and the
 * headphone detect case. Its buttons and sticks are two serial halves on
 * uart3 and uart4 (PD15, PD19), read from userspace (ZyperOS's sppad).
 * Written by plastron's scripts/make-smartpro-dts.py from the Brick's.
 *
 * What follows is the Brick's note on how its tree was made.
 */
'''
open(sys.argv[3],"w").write(head+out)
