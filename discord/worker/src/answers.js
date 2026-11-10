// Zyper's answers to the common questions. Each one is short and links to the
// README section that says it in full; keep them in step with the README.

const R = "https://github.com/tarikbc/ZyperOS#";

export const LINKS = {
  title: "ZyperOS links",
  url: "https://zyperos.com",
  releases: "https://github.com/tarikbc/ZyperOS/releases/latest",
  text:
    "**Website:** https://zyperos.com\n" +
    "**Download:** https://github.com/tarikbc/ZyperOS/releases/latest\n" +
    "**Code:** https://github.com/tarikbc/ZyperOS\n" +
    "**Guide:** https://github.com/tarikbc/ZyperOS/blob/main/docs/guide.md\n" +
    "**Discord:** https://discord.gg/Ust8sE22Tm",
};

export const ANSWERS = {
  install: {
    title: "Installing ZyperOS",
    url: R + "install-in-three-steps",
    text:
      "1. **Download ZyperOS Installer** for macOS, Windows or Linux from https://github.com/tarikbc/ZyperOS/releases/latest\n" +
      "2. **Write the card.** Put the microSD card in the computer, pick the GKD Pixel 2, the latest release and the card, and choose **Fresh install**. It erases the card.\n" +
      "3. **Play.** Put the card in the Pixel 2 and turn it on. The first start sets the card up in about a minute, then shows you each button.\n\n" +
      "Use a spare card if you want to keep the stock one as it is. You can also write the `.img.xz` with any disk image writer, such as Raspberry Pi Imager.",
  },
  bios: {
    title: "BIOS files",
    url: R + "supported-systems",
    text:
      "BIOS files go **loose in `Bios/`** on the card, not in a folder per system. ZyperOS ships none.\n\n" +
      "• **PlayStation:** `scph5501.bin`\n" +
      "• **TurboGrafx-CD:** `syscard3.pce`\n" +
      "• **Sega CD:** `bios_CD_U.bin`, plus `bios_CD_E.bin` or `bios_CD_J.bin` for European or Japanese discs\n" +
      "• **Neo Geo CD:** `Bios/neocd/neocd.bin`\n" +
      "• **Dreamcast, NAOMI, Atomiswave:** in `Bios/dc/`: `dc_boot.bin`, `naomi.zip`, `awbios.zip`\n" +
      "• **3DO:** `panafz10.bin` · **Lynx:** `lynxboot.img`\n" +
      "• Game Boy Advance and Atari 7800 run without theirs.\n\n" +
      "The README's Supported systems list has every system.",
  },
  wifi: {
    title: "Wi-Fi on the Pixel 2",
    url: R + "install",
    text:
      "The Pixel 2 has no Wi-Fi of its own. A **USB Wi-Fi adapter** in its USB-C port gives it Wi-Fi while it's plugged in, and with it box art, RetroAchievements, System Update and Wi-Fi File Transfer.\n\n" +
      "It needs a Realtek chip ZyperOS has a driver for, like the **TP-Link TL-WN725N** on a USB-C adapter.\n\n" +
      "No adapter? **USB File Transfer** moves games over the USB cable instead, and no network around? Wi-Fi File Transfer makes the Pixel 2 its own hotspot.",
  },
  transfer: {
    title: "Adding games",
    url: R + "adding-games-over-wi-fi",
    text:
      "**Over Wi-Fi:** join a network in **MENU > Wi-Fi**, open **MENU > Wi-Fi File Transfer**, then open the address it shows in a browser and type the PIN.\n" +
      "**Over the USB cable:** plug the Pixel 2 into a Mac, Linux or Windows 11 computer and choose **File Transfer** when it asks.\n\n" +
      "Drag games into their console's folder, albums into `Music`, books into `Audiobooks` and films into `Videos`. They're on the shelf when you leave the screen. A card with no games shows both ways on its home.",
  },
  portmaster: {
    title: "PortMaster",
    url: R + "portmaster",
    text:
      "On the **Ports** shelf, open **PortMaster** and press **Install**. It downloads the official PortMaster app over Wi-Fi (about 24 MB).\n\n" +
      "Its first start unpacks and downloads its catalog, which takes a few minutes. Pick the ports you want; they're on the Ports shelf when you leave. Many ports of commercial games need files from a copy you own, and PortMaster says which and where.\n\n" +
      "**MENU** over any port opens the in-game menu: Continue, Controls, Quit.",
  },
  update: {
    title: "Updating ZyperOS",
    url: R + "system-update",
    text:
      "**MENU > System Update** gets new versions over Wi-Fi. **A** downloads, checks and installs it, and **Restart Now** starts it. It needs 25% battery or the charger.\n\n" +
      "No Wi-Fi? **Update ZyperOS** on the File Transfer page installs it over the USB cable.\n\n" +
      "A new version starts on trial, and goes back to the one before by itself if it doesn't start. **Y** rolls back one version by hand.",
  },
  controls: {
    title: "Getting around",
    url: R + "controls",
    text:
      "• **MENU** in a game opens the in-game menu: save, load, Controls and more. **X** there quits.\n" +
      "• **A** continues a game, **Y** starts it from the beginning, **X** on the shelf opens its details.\n" +
      "• **SELECT + R1 / L1** quick save and load, **SELECT + R2 / L2** faster and slower.\n" +
      "• **MENU + START**, held a second, takes you home from anything.\n" +
      "• **POWER** saves and turns off; the next start puts you back in the game.\n\n" +
      "**Controls** in the in-game menu lists every button for the game you're playing.",
  },
};
