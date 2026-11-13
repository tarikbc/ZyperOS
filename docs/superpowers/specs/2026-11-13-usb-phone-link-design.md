# USB that just works: the phone link (1.0.5)

2026-11-13. Branch `usb-phone-link`. Status: Tarik approved the screens (real launcher shots, 2026-11-13); built; the device proof waits for Tarik.

## What the hardware does (evidence)

- **No Type-C controller, no USB PD.** The device tree (ours, ROCKNIX's and the stock-derived
  `rk3326s-gkd-pixel2-uboot.dtb` in twigUI-next) has no CC or Type-C node; the role is the USB PHY's
  ID input (`phy-rockchip-inno-usb2`, its extcon's USB-HOST cable) and the RK817's `otg_switch` boost
  gives VBUS. On the device (read only, 2026-11-13): one I2C bus, i2c-0, with the RK817 alone at 0x20;
  no `/sys/class/typec`, no `usb_power_delivery`. The stock-derived tree enables i2c2 with nothing on
  it, so the device's own firmware drives no CC chip either. Nothing the kernel can program decides the
  CC side: a fixed role for computers has no software route.
- **The side a partner takes is the partner's choice, and it behaves like a dual-role port.** A
  DAC (sink) always reads "wants power". A phone (Try.SNK) reads "wants power" at every plug. A
  Mac (dual-role, no preference) settles either way at random. A Windows laptop with Try.SNK
  ports (Dell Latitude 7310) never gives power. A USB-A port always gives power.
- **A phone that waits for power joins the Pixel's bus as a USB device once the Pixel powers it**
  (usbrole log, 2026-11-13): the iPhone as 05ac:12a8 "iPhone", the AYN Thor as 18d1:4e11 then
  18d1:4ee2 (its USB mode changing). So on a USB-C to USB-C cable the Pixel is the host and the
  phone is the device, every time.
- **The kernel has no USB network host drivers** (`CONFIG_USB_NET_DRIVERS` off). The release
  kernel exports every symbol (`TRIM_UNUSED_KSYMS` is off because of rtl8188eu), so modules
  built from the same tree load on it: the device test uses that, with no system write.
- **iPhone:** `ipheth` answers only after the iPhone is paired (lockdown, usbmuxd and
  libimobiledevice), which the image does not have. Not for 1.0.5.
- **Windows 10:** `UsbNcm.sys` exists from version 2004 and binds by the WINNCM OS descriptor the
  gadget already sends; reports say it handles short packets differently from Windows 11. Not
  tested here (no Windows 10 machine). Windows and macOS both take a device's first
  configuration, so RNDIS in a configuration of its own serves one of them, not both; in the same
  configuration as NCM it gives Windows 11 and Linux two network adapters. Not for 1.0.5 without
  a test.

## The idea, for an Android phone on a USB-C to USB-C cable

1. "What did you plug in?" > **Phone**: the Pixel powers the phone (as USB Accessory does).
2. The message at the top says: turn on USB tethering on the phone.
3. Android's USB tethering makes the phone a USB network device (RNDIS, or NCM). usbrole loads
   `rndis_host` / `cdc_ncm` by the phone's alias, as it loads a Wi-Fi adapter's driver.
4. The launcher sees the phone's network (a host-side interface whose driver is `rndis_host`,
   `cdc_ether` or `cdc_ncm`), runs `udhcpc` on it, and the phone gives the Pixel an address.
5. USB File Transfer opens by itself on that link, with the address and the PIN. The phone's
   browser opens the address. (The phone also shares its internet with the Pixel: the clock
   syncs; nothing else uses it.)
6. Flash Drive cannot work this way (the phone is the device, not the host): the screen says so,
   and names the adapter route for it.

An iPhone: when the device on the bus is Apple's, the message says an iPhone opens the files over
Wi-Fi File Transfer, or with a USB-C to USB-A adapter and a USB-A to USB-C cable.

Only for the plug the player answered Phone: anything can say it is a USB network device, so a plug
powered as USB Accessory or by Automatic gets no lease and no File Transfer by itself. MENU's row is USB
File Transfer while the phone's link has its address.

"What did you plug in?" becomes a list (Tarik: "improve the what did you plug in screen design"): each
row its mark, its name and one line of what it does (headphones, Wi-Fi, keyboard / files, or the card as
a flash drive / files in the phone's browser). Hints A Choose, B Computer; Up and Down move.

## What does not change

- Computer, USB Accessory and Automatic stay as they are. A computer that takes the waiting side
  still gets the replug message and the A-to-C line (PR #111); nothing in software can change the
  side it takes.
- The power rule stays usbrole's: host only while no power comes in, never for a computer seen on
  the bus.

## To prove on the device first (kit in the notes folder, `usbnet/kit`)

1. The six modules load on the running kernel (insmod from `/tmp`).
2. Thor plugged in, powered (USB Accessory), USB tethering on: an interface appears and `udhcpc`
   gets an address.
3. The Thor's browser opens a test page on the Pixel's address.
4. The power the phone draws from the Pixel (battery current, logged every 10 s).

## Builds

A plastron build: `linux-px2.config` adds the six drivers as modules (`linux.config`
regenerated). The coordinator rebuilds the 1.0.5 image after the merge.
