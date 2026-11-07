# ZyperOS Simulator

A GKD Pixel 2 on the Mac's screen, as Apple's Simulator shows an iPhone: the
device drawn to its measurements, its screen showing ZyperOS running in QEMU
([qemu.md](qemu.md)), its buttons pressed with the mouse or the keyboard, and
the parts a player can't press - the battery, the charging cable, the
headphones - set from a panel beside it. What the device shows of them shows
too: the side lights, the backlight, the motor.

## Building and opening it

```sh
simulator/scripts/build-app.sh
open "simulator/build/ZyperOS Simulator.app"
```

It needs plastron's QEMU image (`make qemu` there) and QEMU (`brew install
qemu`), as `tools/qemu-run.sh` does: the window starts its VM with it, by the
name `simulator` (`--args --name NAME` for another, `--image DIR` for an image
somewhere else). A VM already running by that name is shown as it is, and
stays running when the window closes; one the window started shuts down with
it. The VM's sound plays on the Mac.

## Using it

- **Buttons.** Click and hold any of them: the d-pad (it rolls between
  directions as the pointer moves), A B X Y, SELECT and START, the shoulder
  keys along the top, the volume rocker on the left side, MENU and POWER on
  the right. The keyboard: arrows, x z s a (A B X Y), q w e r (L1 R1 L2 R2),
  Return (START), Right Shift or Delete (SELECT), Esc or m (MENU), = and -
  (volume), p (power), h (headphones). A key held is a button held.
- **Power.** POWER is ZyperOS's power button. When the VM is off (after Power
  Off, or Auto Off), POWER turns it on again.
- **The panel** (View > Hardware Panel, ⌘I): the battery level, the charging
  cable, the headphones; what the backlight, the side lights and the motor
  are doing; Shut Down; Save Screenshot.
- **The Device menu:** Power Button (⇧⌘P), Shut Down, Charging Cable (⇧⌘U),
  Battery levels, Headphones (⇧⌘H), Volume Up and Down (⌘↑, ⌘↓), Save
  Screenshot (⌘S, to the Desktop), Copy Screen (⌃⌘C).
- **Zoom** (View, ⌘1 to ⌘4): at 100% the screen's 640 pixels are the Mac's
  own on a Retina display.

The side lights sit on the right edge under POWER, as on the device: top to
bottom battery4, battery3, battery2, and the bottom light, red (battery0) or
green (battery1). They follow plastron's gauge: all four for a full battery
on the cable, a filling bar while charging, a red flash every five seconds
under 10% on the battery. The backlight dims the screen a little (enough to
see a level change, not so much that a low setting hides the picture) and
blacks it out when ZyperOS turns the panel off. Rumble shakes the device.

## Driving it from a shell

`simulator/simctl` talks to the open window over its control socket
(`~/Library/Caches/zyperos-qemu/NAME/app.sock`), one command and one JSON
answer, for checking ZyperOS and the window itself with no clicks:

```sh
simulator/simctl state                        # power, battery, cable, lights, backlight...
simulator/simctl tap start                    # 80 ms; tap a+right 500 for a chord
simulator/simctl press menu; simulator/simctl release menu
simulator/simctl battery 8; simulator/simctl cable 0
simulator/simctl wait-frames 10               # until ten more frames came
simulator/simctl screenshot shot.png          # the VM's screen
simulator/simctl window-shot device.png       # the whole device, drawn
simulator/simctl power | shutdown | quit
```

`-n NAME` picks another window's VM. With no window open, `tools/qemu-run.sh
sim -n NAME battery 8` and the rest reach the same parts over SSH.

## How it works

The window is a SwiftUI app (`simulator/`, a Swift package: `SimCore` for the
parts with no window, `ZyperSimulator` for the app). It runs
`tools/qemu-run.sh start -n NAME --sound` and connects to two of the VM's
sockets:

- `vnc.sock`, QEMU's VNC server: the screen, as raw frames (SimCore's
  `RFBClient`), shown in a layer. QEMU's VNC server looks for changes on a
  timer of its own and backs off when one look sends nothing, so the window
  shows about 10 to 13 frames a second of a game the VM draws at 48 (measured
  2026-11-05, the app at 4% of a core): enough to follow a flow, not to judge
  motion. QEMU's D-Bus display, which pushes every frame, would lift that.
- `sim.sock`, QEMU's end of a virtio-serial port, whose other end is
  plastron's px2sim in the VM: lines both ways. The window sends `press a`,
  `release a`, `battery 15`, `cable 0`, `headphones 1`; px2sim sends `state
  leds=... backlight=... bl_power=... battery=... cable=... headphones=...`
  on every change, and `rumble STRONG WEAK MS` when the motor runs.

The device is drawn from the same measurements as the website's 3D model
(`tools/build-pixel2-model.py`, the dimensioned drawing's millimetres).

`swift test` in `simulator/` checks the line protocol and the control
commands.
