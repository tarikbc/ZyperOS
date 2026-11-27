/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * The handheld usbdrive runs on, as facts: its games partition, its gadget,
 * its cable, its panel's pages. The GKD Pixel 2's unless the build says
 * USBDRIVE_BRICK (usbdrive.mk, from the image's device), the TrimUI Brick's,
 * and with USBDRIVE_SMARTPRO the Smart Pro's, which is the Brick's board.
 *
 * The pages ZyperOS leaves in FRAMES are 640x480 on both (its
 * usbdrive_screen.c lays the atlas out in those units): the Pixel 2's as its
 * 480x640 panel scans them, turned; the Brick's landscape, which its panel
 * (panel_fb.c) shows scaled to its 1024x768.
 */
#ifndef USBDRIVE_DEVICE_H
#define USBDRIVE_DEVICE_H

#ifdef USBDRIVE_BRICK
/* plastron's Brick card: the games partition is 5 (datapart), the gadget
 * its usbgadget script makes, the AXP2202's USB input */
#define DEV_PART       "/dev/mmcblk0p5"
#define DEV_PART_STAT  "/sys/class/block/mmcblk0p5/stat"
#define DEV_GADGET     "/sys/kernel/config/usb_gadget/brick"
#define DEV_VBUS_IN    "/sys/class/power_supply/axp2202-usb/online"
#ifdef USBDRIVE_SMARTPRO
#define DEV_INQUIRY    "ZyperOS Smart Pro card"
#else
#define DEV_INQUIRY    "ZyperOS TrimUI Brick card"
#endif
#define DEV_TURNED     false
#else
#define DEV_PART       "/dev/mmcblk0p3"
#define DEV_PART_STAT  "/sys/class/block/mmcblk0p3/stat"
#define DEV_GADGET     "/sys/kernel/config/usb_gadget/px2"
#define DEV_VBUS_IN    "/sys/class/power_supply/rk817-charger/online"
#define DEV_INQUIRY    "ZyperOS GKD Pixel 2 card"
#define DEV_TURNED     true
#endif

#endif
