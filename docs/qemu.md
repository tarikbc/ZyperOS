# ZyperOS in QEMU

ZyperOS runs on a Mac in QEMU, on its arm64 `virt` machine under Apple's
Hypervisor framework, standing in for the GKD Pixel 2. It is the Pixel 2's
system (plastron's `qemu_defconfig`: the same toolchain, libraries, Mesa,
SDL2 and ZyperOS payload) with a kernel for the virt machine and the Pixel 2's
own parts simulated. ZyperOS's first frame comes 0.85 s after QEMU starts, the
menus move at about 33 frames a second, a NES game runs at its full 60 (about
48 reach the display), and screenshots and button presses work over SSH, as
on the device,
so a launcher or OS flow can be tried without the Pixel 2, and by several
sessions at once, each with its own VM.

[The simulator](simulator.md) shows a VM as a Pixel 2 in a window, with its
buttons, its side lights and its battery.

## Setting it up

```sh
brew install qemu
```

The image comes from plastron (`make qemu` there: hours the first time,
Buildroot's toolchain and then LLVM for Mesa's llvmpipe; minutes after that):

```sh
cd ../plastron
make qemu ZYPEROS_SRC=$PWD/../ZyperOS ZYPEROS_VENDOR=$PWD/../ZyperOS/vendor
```

It writes `out/qemu/sdcard.img` and `out/qemu/Image`. `tools/qemu-run.sh` takes
them from `--image DIR`, `$ZYPEROS_QEMU_IMAGE`, `plastron/out/qemu` beside this
repository's main checkout, or `~/Library/Caches/zyperos-qemu/image`, a copy
any checkout on the Mac can use. Like a development card, the image
lets in the SSH key in plastron's `local/authorized_keys`; `qemu-run.sh` uses
`~/.ssh/zyperos_pixel2` (`$PX2_KEY`).

## Running it

```sh
tools/qemu-run.sh start -n mine          # boots, waits for ZyperOS, returns
tools/qemu-run.sh shot -n mine shot.png  # px2-shot, as a PNG
tools/qemu-run.sh input -n mine a down a # px2-input's buttons
tools/qemu-run.sh ssh -n mine            # a shell (or: ssh -p PORT root@127.0.0.1)
tools/qemu-run.sh stop -n mine           # a clean power-off
```

Each name is a VM of its own: its own copy of the card, its own SSH port and
its own sockets, in `~/Library/Caches/zyperos-qemu/NAME`. `qemu-run.sh info`
prints them, `list` shows every VM. The card copy stays between runs, games
and saves with it; `--fresh` starts a new one. `prune` lists the stopped VMs
with their size and last use, `prune NAME...` removes those and `prune --all`
every stopped one: never a running VM, and never `image/`. A new image goes
onto an existing card the way a system update goes onto a real one: its boot
and root partitions, the games partition left as it is.

A fresh card gets `Roms/NES/ZyperOS Test.nes`, made by
`tools/qemu/make-test-nes.py`: ZyperOS's own small NES program, so there is a
game to start with nothing downloaded. It lights a box for each button held
(A, B, SELECT, START, UP, DOWN, LEFT, RIGHT, left to right), moves a marker
along a lower row every four frames, and plays a tone while A is held. A
screenshot after a press shows the press reached the game; two screenshots a
second apart show it runs.

Other games: `qemu-run.sh roms -n mine DIR` copies `DIR/NES/`, `DIR/SNES/`
and the rest into the card's `Roms/` and restarts the launcher.

A ZyperOS change goes in without a new image, as on the device:

```sh
tools/qemu-run.sh deploy -n mine PAYLOAD/ZyperOS
```

(`tools/px2-deploy.sh` to the VM's port; `PX2_PORT` does the same by hand.)
The payload is `make px2-payload`'s `build/px2/payload/ZyperOS`
(docs/building.md): built against the Pixel 2's SDK, it runs in the VM too,
Diatom and its games included.

Hold presses a little longer than on the device in menus over a game
(`down:200`): the launcher draws at about 33 frames a second there, and an 80 ms
press can fall between two of its reads.

`--window` shows QEMU's own window, where the keyboard is the Pixel 2's
buttons (below); `--sound` plays the VM's sound on the Mac. Headless, the
display is still there for `shot`, `dump` (what QEMU shows, even with the
screen off) and the simulator.

## The Pixel 2's parts

plastron's `px2sim` (its package/px2sim) makes them in the VM under the names
the Pixel 2's kernel gives them, so ZyperOS, Diatom, gauge and the tools run
unchanged:

| Part | In the VM | Set or read |
|---|---|---|
| Joypad, volume keys, power key | uinput devices `gkd_pixel2_joypad`, `gpio-keys`, `rk805 pwrkey`, first in `/dev/input` | `px2-input`, the keyboard, the simulator |
| Headphone jack | uinput switch `rk817_int Headphones` | `qemu-run.sh sim headphones 1`, key h |
| Battery and charger | `power_supply/battery`, `power_supply/rk817-charger` (module `px2sim_hw`) | `qemu-run.sh sim battery 15`, `sim cable 0`, `sim current 1200` |
| What the cable is in | an extcon, as the USB PHY's BC1.2 answer: DCP a wall charger, SDP a computer | `qemu-run.sh sim kind computer`, the simulator's Into |
| Backlight | `backlight/backlight`, 0-255 and `bl_power` | read by `sim state`, shown by the simulator |
| Side lights | `leds/battery0` to `battery4`, driven by `gauge` | read by `sim state`, shown by the simulator |
| Rumble | the joypad takes `FF_RUMBLE` | shown by the simulator |
| Codec volume | `Master Playback Volume` on the sound card, applied by ALSA's softvol | the volume keys |

The battery starts full, on the cable, so Auto Off and the empty-battery
power-off stay away until a test sets otherwise. `/tmp/zyperos_fake_batt`
works as on the device.

### A charger, and a handheld that is off

What the Pixel 2 does with a charger, as measured on one (2026-11-05 and
11-06), and how the VM does the same:

- **The cable into a handheld that is off starts it.** The RK817 powers it
  on and says why in its ON_SOURCE register: 0x40 for a charger, 0x80 for
  POWER, 0x02 after a restart. plastron's U-Boot passes `zycharge=1` for a
  charger's start, and ZyperOS shows its charging screen (DESIGN.md 11.25).
  QEMU has no PMIC, so `qemu-run.sh start --charger` writes the reason,
  `on_source=0x40`, into `/px2sim-on.txt` on the card's boot partition, and
  plastron's QEMU boot.scr reads it there as U-Boot reads the PMIC, then sets
  it to 0x02: a restart is an ordinary start, as on the device. The
  simulator's cable switched on while the VM is off does the same, as does
  `simulator/simctl cable 1`.
- **What the cable is in.** The USB PHY tells a wall charger (DCP, which the
  Pixel 2 draws 2 A from) from a computer (SDP, 1.5 A); ZyperOS reads that,
  not rk817-charger's `usb_type`, which says DCP for anything. px2sim makes
  the same extcon, set with `sim kind wall|computer` (wall to start with).
- **The charging current.** The battery takes 1.5 A from a wall charger and
  0.9 A from a computer until 85%, then less and less, down to the 150 mA the
  RK817 ends a charge at (a battery's CV phase), and nothing once full; the
  voltage rises to the RK817's 4.35 V. `sim current MA` sets the charging
  current instead, 0 for the model again. Off the cable it reads -0.4 A.
  ZyperOS's watts and its "Full in about" come from these.

These need a plastron image made since (its px2sim and QEMU boot.scr, and
CONFIG_EXTCON in the QEMU kernel). On an older image `start --charger` is an
ordinary start, the current is a flat 0.5 A, and what the cable is in is not
known ("charger" on the charging screen).

The keyboard (QEMU's window, or the simulator) is the buttons: arrows for the
d-pad, x z s a for A B X Y, q w e r for L1 R1 L2 R2, Return for START, Right
Shift or Delete for SELECT, Esc or m for MENU (FUNCTION), = and - for volume,
p for power, h for the headphones.

## What it covers, and what still needs the device

It runs the Pixel 2's own system, so it covers what that system does: the
boot, the card's first-boot setup (the games partition, ZyperOS copied onto
it), the launcher and its menus, settings and their database, Diatom and the
libretro cores (the same aarch64 builds), saves and save states, the in-game
menu, the battery's warnings and Auto Off, the side lights' rules (gauge), the
volume and brightness steps, ports' scripts, SSH, `px2-shot`, `px2-input` and
`px2-deploy`.

It does not stand in for the hardware:

- **GPU and speed.** There is no Mali: Mesa draws with llvmpipe on the Mac's
  cores. Frame rates, Panfrost's quirks, GPU-rendered cores (flycast2021,
  mupen64plus) and anything timed say nothing about the device. The Mac's
  cores are also many times faster than the RK3326S: a core that keeps up
  here may not there. Measure on the device.
- **The display.** QEMU's is a 640x480 landscape one; the Pixel 2's panel is
  480x640, mounted turned. ZyperOS and Diatom draw unturned here, and Diatom
  scans out as XRGB8888, which virtio-gpu takes, rather than ARGB8888. The
  turned path, the boot animation (left out: its frames are made turned),
  `splash --still` over a port and portkeys' volume bar over a port (drawn
  turned for the portrait panel) only run on the device.
- **Clocks and power.** No cpufreq or devfreq: the power profiles, the RAM
  drop on an idle shelf and S01governor do nothing (Diatom logs that it could
  not set the clock ceilings). Battery drain and power draw are not modelled:
  the battery stays at whatever it was set to.
- **LEDs, charging, rumble.** The side lights, the charger and the motor are
  simulated: the rules that drive them run, the GPIOs, the RK817 and the PWM
  do not. Of the RK817's own reports, its reason for a start and the current
  through a charge are modelled (above); others are not (it says Discharging
  on a full battery on the cable, for one), nor how its gauge learns a
  battery's capacity.
- **USB.** No USB controller: no USB gadget (USB File Transfer's address,
  10.42.0.1, isn't reachable; the file transfer page is on the VM's web port,
  `qemu-run.sh info`), no USB-C DAC, no USB Wi-Fi adapter (no wlan0, so Wi-Fi
  menus show no adapter).
- **Sound.** QEMU's virtio sound card, not the RK817: the volume works
  through software, the speaker and headphone windows are the same sound, and
  the headphone jack only switches which window applies.
- **Boot.** With an image from plastron's `make qemu` that has U-Boot
  (`u-boot.bin`), QEMU's U-Boot runs the card's own boot.scr, slots and all;
  an older image has QEMU load the kernel itself. The Pixel 2's U-Boot (its
  first stage, its DDR set-up, its PMIC) and its device tree are the
  device's only.

## Known problem

Diatom does not exit on SIGTERM in the VM (the device measured 280 ms): its
sound thread stays in ALSA's poll on the virtio sound card, with QEMU's silent
backend and with CoreAudio alike, and the sound queue is full and dropping
when it stops. So a power-off with Diatom running (a game, or just the resident
one) takes about 8 s, the games partition is left in use and is checked at the
next boot (`datapart: ... was not unmounted cleanly`, then fine), and a game's
state is still saved first. Quit from the in-game menu works as on the
device. Not yet found: a dummy ALSA card (snd-dummy) in place of virtio sound
is the next thing to try.

## Under the hood

- plastron: `configs/qemu_defconfig`, `board/qemu/` (kernel config
  fragment, card layout, inittab, ALSA config), `package/px2sim` (the parts),
  `package/mesa3d-px2`'s llvmpipe option, `make qemu`.
- ZyperOS: `tools/qemu-run.sh`, `tools/qemu/make-test-nes.py`; the Pixel 2's
  device files take a landscape display (`src/device/pixel2.c` draws
  unturned there, Diatom's port already did) and Diatom's falls back to
  XRGB8888 frames; `tools/px2-shot.c` writes a landscape display as it is;
  `sd/pixel2/launch.sh` turns a port's picture only on a portrait panel.
- The VM: 4 cores and 1 GB as the device, virtio disk (the card image, its
  partitions as on the device), network (SSH and port 80 forwarded on
  127.0.0.1), GPU (KMS only), sound, keyboard, random numbers, a serial
  console (`serial.sock`, `serial.log`) and a virtio-serial port for px2sim
  (`sim.sock`).
