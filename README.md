<p align="center">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="res/readme/lockup.png">
    <img src="res/readme/lockup-light.png" width="520" alt="ZyperOS: Zyper, a miniature schnauzer with his head cocked and his tongue out, beside the name">
  </picture>
</p>

<h3 align="center">A fast, light firmware for the GKD Pixel 2 that plays your whole retro library.</h3>

<p align="center">
  <b>On in about 2 seconds. The stock firmware takes 30 to 40.</b><br>
  Turn it off mid-game; turn it on, and 2 seconds later you're back where you were.<br>
  Forty-four systems with their box art, PortMaster ports, RetroAchievements, and your music and videos. Free and open source.
</p>

<p align="center">
  <a href="https://github.com/tarikbc/ZyperOS/releases/latest"><img src="https://img.shields.io/github/v/release/tarikbc/ZyperOS?style=for-the-badge&label=Download&color=476b00" height="34" alt="Download the latest release of ZyperOS"></a>
  &nbsp;
  <a href="#install-in-three-steps"><img src="https://img.shields.io/badge/Install-in_three_steps-1a1c1a?style=for-the-badge" height="34" alt="Install in three steps"></a>
  &nbsp;
  <a href="https://discord.gg/Ust8sE22Tm"><img src="https://img.shields.io/badge/Discord-Join_us-5865F2?style=for-the-badge&logo=discord&logoColor=white" height="34" alt="Join the ZyperOS Discord"></a>
  &nbsp;
  <a href="https://github.com/tarikbc/ZyperOS/releases"><img src="https://img.shields.io/github/downloads/tarikbc/ZyperOS/total?style=for-the-badge&label=Downloads&color=1a1c1a" height="34" alt="Downloads of every ZyperOS release"></a>
</p>

<p align="center">
  <a href="https://zyperos.com"><b>zyperos.com</b></a> &nbsp;·&nbsp;
  <a href="#why-zyperos">Why ZyperOS</a> &nbsp;·&nbsp;
  <a href="#coming-from-the-stock-os">From the stock OS</a> &nbsp;·&nbsp;
  <a href="#features">Features</a> &nbsp;·&nbsp;
  <a href="#how-it-compares">Compare</a> &nbsp;·&nbsp;
  <a href="#supported-systems">Systems</a> &nbsp;·&nbsp;
  <a href="#controls">Controls</a> &nbsp;·&nbsp;
  <a href="#faq">FAQ</a> &nbsp;·&nbsp;
  <a href="https://discord.gg/Ust8sE22Tm">Discord</a>
</p>

<p align="center">
  <img src="res/readme/shelf-n64.png" width="640" alt="The Nintendo 64 shelf: Super Mario 64's box large in the middle, Mario Kart 64 and Ocarina of Time beside it, a play mark on each cover for its video snap">
</p>

## Why ZyperOS

- **Your library looks the part.** Every game shows its real box art, found on the handheld itself from libretro, ScreenScraper and SteamGridDB, and a few seconds of video when you rest on it.
- **On in about 2 seconds.** The stock firmware takes 30 to 40 (my test, on my Pixel 2). Turn it off mid-game, and the next start puts you back where you were, 2 seconds later. Games start the moment you press A.
- **Plays more, smoothly.** Forty-four systems, the PSP among them, and PortMaster's ports. Mario Kart DS keeps 60 frames a second and Super Mario 64 its 30, on the Pixel 2.
- **No more taking the card out.** Add games, music and saves from any browser over Wi-Fi or the USB cable, and update ZyperOS the same way.
- **Made to be picked up.** A first start that teaches every button, eight languages, light and dark, and a menu that never makes you wait.
- **Free and open source,** under GPL-3.0, with nothing locked behind a payment.

## Coming from the stock OS?

In my test on my Pixel 2, the stock firmware takes over 30 seconds to start, usually close to 40. ZyperOS is on in about 2, and **POWER** in a game saves it, so the next start puts you back in that game. Beyond that, ZyperOS has what the stock column of [the comparison](#how-it-compares) doesn't show:

- **Box art on the handheld,** from three sources, with no computer and no Skraper
- **RetroAchievements,** signed in on the device
- **Games over the USB cable** or from any browser, not only Samba
- **System updates** over Wi-Fi or the cable, with roll back
- **Charge limit,** battery **time left** and a **charging screen** while it's off
- **A first-start tutorial** for every button
- **Eight languages** and **menu music**
- **Storage**, by part and by game, and **side lights** for the battery, volume and more

Keep the stock card as your way back: ZyperOS goes on a second card, and swapping cards switches systems ([keeping the stock card safe](https://zyperos.com/blog/gkd-pixel-2-stock-firmware/)). Your games come over as files, each into its console's folder under `Roms/` ([the folders](#supported-systems)).

## Coming from EmulationStation

Knulli, ROCKNIX, ArkOS, AmberELEC, ROGUE OS and the other systems built on EmulationStation keep each console's games in a short folder (`roms/nds`), with what their scraper found beside them. Copy those folders into `Roms/` as they are: `nds` is read as `Nintendo DS`, `psx` as `PlayStation`, `megadrive` as `Genesis`, and so on for every system ([the whole list](docs/guide.md#coming-from-emulationstation)). A shelf reads its ZyperOS folder while that has games, and otherwise the EmulationStation one with the most; a new ZyperOS card's folders start empty, so the copied ones are read.

What their scrapers left is used where ZyperOS has nothing of its own, in this order:

- **Covers:** ZyperOS's own (`.media/`, from Box Art or Cover and Video), then `gamelist.xml`'s `<thumbnail>` and `<image>`, then `images/<game>-thumb`, `-image` and plain, `media/box2dfront/`, `media/images/`, `boxart/` and `downloaded_images/`, and last the game's logo (`<marquee>`). Box Art fetches covers only for games with none.
- **Video snaps:** ZyperOS's own, then `gamelist.xml`'s `<video>`, then `videos/<game>-video.mp4`, `videos/` and `media/videos/`.
- **Names and details:** a moment after each start, `gamelist.xml`'s `<name>`, `<desc>`, `<releasedate>`, `<genre>`, `<players>`, `<developer>`, `<publisher>`, `<rating>` and `<favorite>` go into ZyperOS's library for each game that has no details yet. A ScreenScraper scrape later replaces them; they never replace a scrape.

The folders still work in the other system: ZyperOS writes nothing in them but its own covers, in `.media/`, and a game you rename takes its EmulationStation pictures, video and `gamelist.xml` entry with it.

## Install in three steps

1. **Download ZyperOS Installer** for macOS, Windows or Linux from [the latest release](https://github.com/tarikbc/ZyperOS/releases/latest).
2. **Write the card.** Put the microSD card in the computer, pick the GKD Pixel 2, the latest release and the card, and choose **Fresh install**.
3. **Play.** Put the card in the Pixel 2 and turn it on. The first start sets the card up in about a minute, then shows you each button.

Then add games over Wi-Fi, the USB cable or the card itself. [The full install](#install) covers the card image, updates and the Wi-Fi adapter.

Questions, ideas, or want to help build it? Come say hi on [the ZyperOS Discord](https://discord.gg/Ust8sE22Tm).

<!-- tour-video: the tour on zyperos.com (video/README.md) -->
<p align="center">
  <a href="https://zyperos.com/#tour"><img src="res/readme/tour-poster.jpg" width="720" alt="Watch the ZyperOS tour: a green GKD Pixel 2 showing a shelf of PlayStation covers, beside the ZyperOS name"></a><br>
  <sub>The tour, 105 seconds on zyperos.com</sub>
</p>
<!-- /tour-video -->

## Screens

<div align="center">

<table>
  <tr>
    <td align="center"><img src="res/readme/shelf.png" width="240" alt="The PlayStation shelf: Metal Gear Solid's box large, Crash Bandicoot and Spyro the Dragon beside it, a heart before the focused game's name"><br><sub>Your games, cover by cover</sub></td>
    <td align="center"><img src="res/readme/favorites.png" width="240" alt="Favorites: Sonic The Hedgehog 2 between Metroid Fusion and Super Mario 64, Genesis under its name"><br><sub>Favorites from every system</sub></td>
    <td align="center"><img src="res/readme/shelf-snes.png" width="240" alt="The SNES shelf: Super Mario World's box between Donkey Kong Country and Super Metroid"><br><sub>Box art the handheld finds itself</sub></td>
  </tr>
  <tr>
    <td align="center"><img src="res/readme/systems.png" width="240" alt="The Spotlight home: PlayStation's console large, the other systems in a row below"><br><sub>Every system, in pixel art</sub></td>
    <td align="center"><img src="res/readme/gallery.png" width="240" alt="The Gallery home: PlayStation's console centered between its neighbors"><br><sub>Or the Gallery home</sub></td>
    <td align="center"><img src="res/readme/media.png" width="240" alt="Metal Gear Solid's Cover and Video page: its cover now, the boxes, title screen and screenshots the sources offer, and its video"><br><sub>Pick a cover, and see it first</sub></td>
  </tr>
  <tr>
    <td align="center"><img src="res/readme/cheevo-notice.png" width="240" alt="An achievement's unlock notice: its badge, 'Unlocked · 10 points' over its name, and Zyper at the right"><br><sub>RetroAchievements, and Zyper cheers</sub></td>
    <td align="center"><img src="res/readme/gamemenu-shader.png" width="240" alt="The in-game menu over a paused Spyro the Dragon, on its Shader row: Sharp-Shimmerless, with Quick Slot 1 above it"><br><sub>A shader and a quick slot for each system</sub></td>
    <td align="center"><img src="res/readme/controls-hotkeys.png" width="240" alt="Controls: Hotkeys. SELECT with R1 and L1 quick save and load, with R2 and L2 faster and slower"><br><sub>SELECT with a shoulder: the hotkeys</sub></td>
  </tr>
  <tr>
    <td align="center"><img src="res/readme/portmaster.png" width="240" alt="Install PortMaster: its crane icon, PortMaster's version, what it is, and the download's bar at 9.8 of 24 MB"><br><sub>PortMaster with one press</sub></td>
    <td align="center"><img src="res/readme/storage.png" width="240" alt="MENU > Storage: the card in one bar by part, two recommendations, then every system by size"><br><sub>What fills the card</sub></td>
    <td align="center"><img src="res/readme/battery.png" width="240" alt="MENU > Battery Icon set to Time Left: the corner reads 3 h 25 min beside the battery"><br><sub>The battery, as time left</sub></td>
  </tr>
  <tr>
    <td align="center"><img src="res/readme/mp4.png" width="240" alt="MP4's shelf of an album, a film and a book, each marked with what it is"><br><sub>Your albums, books and films, in MP4</sub></td>
    <td align="center"><img src="res/readme/info.png" width="240" alt="Metal Gear Solid's details: Synopsis, Year, Genre, Achievements, Favorite, Cover and Video"><br><sub>A game's details, one button away</sub></td>
    <td align="center"><img src="res/readme/menu.png" width="240" alt="The ZyperOS menu on its Games tab, About and Display beside it"><br><sub>The device's settings in six tabs</sub></td>
  </tr>
  <tr>
    <td align="center"><img src="res/readme/folders.png" width="240" alt="The Game Boy Advance shelf: the folder Commercial Owned as a pixel folder with 4 on its label, Friends Games beside it, 4 games under its name"><br><sub>Your folders, as you made them</sub></td>
    <td align="center"><img src="res/readme/list.png" width="240" alt="The Game Boy Advance shelf as a list: four folders with their counts, then the games, each with a small card, the focused folder's card on the right"><br><sub>Or a list, per system</sub></td>
    <td align="center"><img src="res/readme/list-inside.png" width="240" alt="Inside the folder Commercial Owned as a list: Golden Sun focused, its box art on the right, B Back"><br><sub>A folder open, its covers beside it</sub></td>
  </tr>
  <tr>
    <td align="center"><img src="res/readme/es-shelf.png" width="240" alt="The PlayStation shelf read from Knulli's psx folder: Metal Gear Solid's box between Crash Bandicoot and Spyro the Dragon, each with a play mark for its video"><br><sub>Your EmulationStation art, as it is</sub></td>
    <td align="center"><img src="res/readme/info-rename.png" width="240" alt="Metal Gear Solid's details: Genre, Achievements, Favorite, Cover and Video, Rename (chosen) and Delete Game"><br><sub>Rename a game on the handheld</sub></td>
    <td align="center"><img src="res/readme/rename.png" width="240" alt="The keyboard titled Rename, holding Metal Gear Solid (USA)"><br><sub>The new name, on the keyboard</sub></td>
  </tr>
</table>

<sub>The shots show real box art to show what the Scraper does. The games and their art belong to their owners: ZyperOS comes with no games and no art.</sub>

</div>

## Features

- **Forty-four systems,** from the Atari 2600 and ColecoVision to PlayStation, Nintendo 64, Nintendo DS, PSP, Dreamcast and Saturn, with Arcade, MAME, Neo Geo, NAOMI and Atomiswave, Doom, ScummVM, PICO-8, TIC-80, OpenBOR and J2ME ([the full list](#supported-systems)).
- **Ports:** the Ports shelf installs the official [PortMaster](https://portmaster.games) with one press and runs its games, such as Stardew Valley and Celeste. **MENU** over any port, and over PortMaster itself, pauses it under ZyperOS's in-game menu, with **Controls** for that port ([how](#portmaster)).
- **Nintendo DS your way:** [DSperate](https://github.com/beebono/DSperate) inside the emulator, with three screen layouts and a d-pad stylus. A Mario Kart DS race keeps 60 frames a second on the Pixel 2. **Game Language** in the DS shelf's menu picks the language a game speaks: Auto, the language of **MENU > Language** where the DS has it (English, Español, Français, Deutsch, Italiano, 日本語) and English where it doesn't, or any of them outright, whatever `firmware.bin` says. A game left in another language starts fresh in the new one, with its battery save.
- **Nintendo 64 at full speed:** [ParallelN64](https://github.com/libretro/parallel-n64) with Glide64, as ROGUE OS ran it. Super Mario 64 and a Mario Kart 64 race hold their 30 frames a second on the Pixel 2, and the d-pad is the N64's stick. Mupen64Plus-Next is the second choice on the shelf's **Core** row, for a game Glide64 draws badly. A game left on the other core starts fresh, with its battery save.
- **Dreamcast** on flycast2021, the 2021 libretro [Flycast](https://github.com/flyinghead/flycast), drawn on the GPU inside the emulator, with saves, states and resume like any other system. Standalone Flycast is the second choice if you add it.
- **A home screen in pixel art:** every system as its own console, drawn for ZyperOS, and it moves: a handheld plays its startup, a console takes its cartridge or its disc, and Favorites' heart beats. On a Spotlight or a Gallery home, with your games as a row of covers. The systems go A to Z, or by release date in **MENU > System Order**. Light or dark, [as you like](#light-and-dark).
- **Eight languages:** English, 简体中文, Español, Português (Brasil), Français, Deutsch, 日本語 and Italiano, chosen on the first start or in **MENU > Language**. Every word follows it: the menus, the in-game menu, the charging and low battery screens, the boot screen's line while ZyperOS is copied onto the card, and the File Transfer page. Chinese and Japanese each have a face of their own, so they read as clearly as the rest, and switching languages costs no speed ([below](#languages)).
- **A first start that asks:** Zyper says hello in each language until you pick yours, teaches each button and shows where it is on the Pixel 2, then asks for the theme, the home screen, the button sounds and the menu music ([below](#the-first-start)).
- **An empty card that says what to do:** with no games yet, the home shows the two ways to add them, one button each, and Zyper cheers when the first ones arrive.
- **Your folders, kept:** sort a system's games into folders on the card, "Homebrew", "Rom Hacks", "Translations" inside it, as deep as you like, and the shelf shows them as you made them: the folders first, each a pixel-art folder in the system's color with papers in it and its count of games, then the loose games. A opens a folder, B goes back up. A disc set, a ScummVM game and a port stay one game each, as before. Favorites, Search Games, Random Game, Storage, Delete Game, play time and resume all reach the games inside folders.
- **Covers or a list:** **START** on a system's shelf, or **MENU > View**, turns its row of covers into a list of names, each with a small card in the system's color, and the focused game's cover and play time beside it; **START** again brings the covers back. Up and Down step, Left and Right jump a letter, and the focused game's video snap plays in its cover's place, as on the row. Each system keeps its own choice, as it keeps its sort order.
- **MENU in tabs:** Games, Display, Sound, Connections, System and About, with **L1** and **R1** going round them. MENU opens on the tab you left it on, and every list goes round: Down on the last row goes to the first, and a long list carries on past its end, while a held button stops at the end.
- **Your look:** **MENU > Display > Theme** picks the accent (ten of them, lime as it ships), a background tone for the light theme (warm, neutral or cool) and one for the dark (black, deep blue or dark grey), rounded or square corners, and Zyper on or off, with both themes shown small as you go. Every color is worked out to stay readable, and the volume bar over a game follows ([below](#light-and-dark)).
- **Search Games and Random Game:** **MENU > Search Games** finds any game by a few letters of its name, on any system. **Random Game** picks one and starts it, from every system in MENU or from one shelf in that system's menu.
- **RetroAchievements:** sign in on the device, under **MENU > Achievements**. A game's achievements download the first time you play it with Wi-Fi, and then work offline: an unlock shows at once, waits on the card, and goes to your account as soon as the handheld is on Wi-Fi again, dated when you earned it. The game's list says how many wait, and Zyper says when they are sent. A game never played with Wi-Fi says "not checked yet" on its Achievements row. Each achievement has its badge in the list and in the pop-up when you unlock it, a trophy standing in until the badge is on the card, Zyper cheers from the pop-up, and a count like "5 / 10" pops up as you get closer.
- **Box art the device finds itself,** in **MENU > Scraper**: from libretro's collection with no account; from ScreenScraper with a free account, which brings each game's name, year, genre and synopsis too, in a build made with ScreenScraper's developer pair (the page says `not in this build` otherwise); and from SteamGridDB, with your own free key typed or pasted, for what neither has. Renamed ROM packs are matched by checksum, and each game shows under its proper name with its file left as it is. Or add your own. To change one game's cover, X on it opens **Cover and Video**: every cover, 3D box, title screen and screenshot the three sources offer, each named with where it comes from, a big look before it is used, and the one it replaced kept to put back ([below](#a-game-has-no-box-art)).
- **Coming from Knulli, ROCKNIX, ArkOS or ROGUE OS:** copy their `roms` folders over as they are. The folders EmulationStation names (`nds`, `gba`, `psx`, `megadrive` and the rest) are read as ZyperOS's own, and the covers, video snaps, names, details and favorites their scrapers left (`gamelist.xml`, `images/`, `videos/`, `media/`) show on the shelf with no new scrape. ZyperOS's own covers come first, and the folders still work in the other system ([what ZyperOS reads](#coming-from-emulationstation)).
- **Rename a game from the handheld:** X on a game, then **Rename**, types a new name on the keyboard. Its saves, save states, cover, video snap, achievements, favorite and play time follow the file, and so does its EmulationStation art and entry. Handy for a name no scrape finds.
- **Delete a game from the handheld:** press X on a game and choose **Delete Game**. It asks first, naming the game, with the cursor on Cancel. The game's file or files go (a disc set's folder, a `.cue` with its tracks, an arcade set), with its saves, save states, cover, video snap, cached achievements, details and play time, and the shelf moves on to the next game. A file another game still uses stays, and so does a BIOS set like `neogeo.zip`.
- **Storage, as a phone shows it:** **MENU > Storage** shows what fills the card in one bar, games, saves, art, media and ZyperOS, then every system and folder by size. A system's page lists its games largest first, each with the room Delete Game would give back; Music, Videos and Audiobooks open their albums, films and books the same way, and A deletes one. **X** sorts any of these lists by size, A to Z or last played. Two recommendations clear the covers and video snaps of games no longer on the card, and the save states of games you have not played for a year (the saves stay). It opens at once: the sizes are measured in the background and kept on the card ([below](#what-fills-my-card)).
- **Video snaps:** rest on a game for a second, while a bar under its cover fills, and the cover turns into a few seconds of the game itself, from ScreenScraper. A play mark on a cover says its game has one. **MENU > Scraper** gets them per console, for all games, the favorites or the ones played, and says the room first; their sound starts muted.
- **A screensaver of your games:** after a minute with nothing pressed on the shelf, the video snaps of random games play over the whole screen, each with its name and system. **START** boots the one on screen and any other button goes back. **MENU > Screensaver** sets the delay or turns it off. It never starts in a game or while a long job runs, never keeps the handheld on past Auto Off, and shows covers when there are no video snaps (more in the questions below).
- **Auto Off waits for long jobs:** a Scraper run, a System Update download, PortMaster's install, Storage measuring the card and files moving over File Transfer hold it, and a whole countdown starts when they end.
- **Wi-Fi File Transfer:** move games, music, audiobooks, videos, saves and covers on and off the card from any browser on your Wi-Fi, behind a PIN, on [a page made for it](#the-file-transfer-page), on a computer or a phone. With no Wi-Fi network around, the handheld makes [its own](#no-wi-fi-network-around), for a phone that scans the QR code on the screen. Or use the USB cable, with no Wi-Fi at all, as USB File Transfer: from a computer, or on the Pixel 2 from an Android phone's own browser over its USB tethering, if it has it. Open it by name, `http://zyperos.local`, or by its address, and until a browser opens the page the screen checks the connection step by step and says what to do if a step stalls. A transfer keeps going while you go back to the shelf, with a mark in the corner; B asks before it stops one halfway; the page shows the time left, checks the room on the card first, and its Settings page updates ZyperOS over the cable. Drag files onto a folder to move them there.
- **USB that only charges until you say so:** plug in a computer and the Pixel 2 asks whether it should charge, open USB File Transfer, or (on development images) bring up SSH. [Developer mode](#developer-mode) brings SSH up on a release image. Plug in an Android phone, choose **Phone** and turn on its USB tethering: USB File Transfer opens in the phone's browser, and the handheld gets the phone's internet, so box art, System Update and achievements work with no Wi-Fi adapter ([how](#from-a-phone-on-the-usb-cable)).
- **Player 2 on a friend's phone:** on the Pixel 2, **Player 2** in an NES game's in-game menu lets a friend play the second controller in a browser, with no app and no account. With a Wi-Fi adapter in, the handheld makes a network just for the game and shows its QR code: the friend scans it, and the game opens on the phone with a touch pad under the picture. A computer on the USB cable opens `zyperos.local/play` and plays with the keyboard. The page speaks the handheld's language and wears its theme. A game controller paired with the phone comes with a later update ([how](#player-2-a-friend-on-a-phone)).
- **System Update:** [over Wi-Fi](#system-update), only what changed, signed, with Roll Back. Without Wi-Fi, the File Transfer page brings it over the USB cable.
- **Autosave and resume,** plus six save slots for when you want a checkpoint, packed small on the card: a Nintendo 64 game's state takes about 1.5 MB where it took 16, and a PSP game's no longer 40 MB on every quit.
- **Quick save and quick load, without opening a menu:** hold SELECT and press **R1** to save a state, **L1** to load it back. A pop-up names the slot. **Quick Slot** in the in-game menu picks which of the six slots the two use, for each system, and the card remembers it. SELECT is still each game's own button: pressed alone, it reaches the game as you let go of it; only SELECT with a shoulder is a hotkey.
- **A QWERTY keyboard** that keeps up with the d-pad.
- **Clocks per system:** 8-bit and handheld systems run the CPU, memory and GPU under lower ceilings, measured to keep every frame, and so does the 16-bit generation a step higher. Lower clocks run at lower voltage, which saves battery. At the first frame that comes late, a game goes back to full clocks for the rest of the session.
- **Turbo buttons:** X and Y press A and B for you, on the nine consoles whose controllers had two face buttons. On Game Boy Advance, L2 and R2 do the same for L and R.
- **Faster and slower:** hold SELECT and press **R2** to run a game at 1.5x, 2x or 3x, and **L2** to step back down to normal speed. A pop-up says the speed.
- **Shaders, from sharp pixels to an old TV:** **Shader** in the in-game menu draws the game five more ways. [Sharp-Shimmerless](https://github.com/Woohyun-Kang/Sharp-Shimmerless-Shader), zadpos's shader for pixel art drawn at a size that is not a whole number of times the game's, as a 256-pixel-wide game is on the 640-pixel screen: every pixel stays the same size and as sharp as it can be, so a scrolling picture doesn't shimmer. **CRT** and **CRT Curved**, Timothy Lottes's CRT shader: scanlines and the colored stripes of a TV tube, flat or with curved glass. **Scanlines** alone, and **LCD Grid**, the grid of a Game Boy's screen, both from zadpos's shader too. Choose one for one system in a game or in that system's menu, or for every system in **MENU > Display > Shader**. It starts Off ([how they look](#shaders)).
- **Every control, in the game:** **Controls** in the in-game menu shows what each button does in the game you are playing: its system's own buttons as its emulator reads them (the PlayStation's as their cross, circle, triangle and square), the turbo buttons, the hotkeys, the Nintendo 64's A and B on the A and B buttons, its stick on the d-pad (its own D-pad is SELECT with the d-pad), its C buttons (SELECT with X, A, B or Y) and Rumble Pak, the PICO-8's arrows, O, X and pause menu, and the DS's screens and stylus. Over a port it shows what the port documents: the controls in its README, the key each button types for it, and what to copy where before it runs.
- **Rumble on the Pixel 2:** Dreamcast games, PlayStation games (as a DualShock) and Game Boy Color rumble cartridges shake the handheld, and Nintendo 64 games get a Rumble Pak. A game that saves to the Controller Pak, the N64's memory card, takes it instead in the in-game menu's **Pak** row, which keeps the choice for that game. The motor stops while the in-game menu is open.
- **A USB-C headphone adapter on the Pixel 2:** plug one in and the sound goes there, ahead of the headphone jack, in games and in MP4. It's a choice of its own in Audio Output.
- **Bluetooth headphones** on the Brick, paired on the device, with its volume buttons reaching them and their own buttons playing and skipping in MP4. Plug in wired ones and they take over.
- **Mono audio:** the Pixel 2 has one speaker, so Audio Channels starts on Auto, mono on the speaker and stereo on headphones, mixed so it never clips. For anyone who hears with one ear, it plays games and MP4 in mono everywhere, or in mono in just the left or right ear on headphones.
- **MP4, a media player for music, audiobooks and videos:** your albums, books and films on one shelf of their own covers, mixed, each with a small mark in its corner for an album, a book or a video, or one kind at a time with **MENU > Show**; every book and every video picks up where you left it, and **Shuffle All** plays every song on the card in a random order. SELECT opens MP4 from anywhere, even the in-game menu, and it keeps playing when you close it. While it plays, the game is silent. **X**, or **SELECT + START** together, locks it: the screen goes dark and every button but volume stops working, POWER included, until you hold **SELECT + START** for two seconds. Any other press shows how to unlock for a moment, a volume press lights the screen just long enough to show the volume bar, and a video stays in sight.
- **Menu music:** ten free 8-bit tracks play behind the shelf and the menus, never over a game, and **MENU > Menu Music** shuffles them, plays one over and over or turns them off. Your own go in `Menu Music`, a folder of their own at the top of the card beside `Music` and `Videos`: copy MP3 or Ogg files there with the card in a computer, in Flash Drive, or on File Transfer's page. **ZyperOS Tracks** in the same menu turns ZyperOS's ten off, so only yours play. On the shelf, **L2** and **R2** go to the song before or the next, and Zyper pops up with each new song's name, read from its tags. While MP4 plays behind the shelf, L2 and R2 skip its tracks (or a book's chapters) instead.
- **The top bar:** the clock, Wi-Fi, headphones in the jack and the battery at the top of the home screen, every MP4 screen and the in-game menu, and the song that plays, with its note. **MENU > Clock** chooses 12-hour or 24-hour, and **MENU > Battery Icon** shows the battery as bars alone, with its percentage, or with the time left. Time Left counts down to where ZyperOS saves and powers off. It starts from the charge and current the battery gauge reports, so a time appears at once, and moves over to how fast the battery has been falling as that proves itself; for the moment before the first number it shows three dots filling in turn. On the cable it shows the time to full, or to the Charge Limit, then "On hold" when the Charge Limit is holding the charge and "Charged" when the battery is full.
- **The Pixel 2's side lights** show the battery charging and running low, the volume or brightness as you change it, a wave while files move to or from the card (down while they come in, up while they go out, in Flash Drive and on File Transfer's page, faster for a faster transfer), a climb when files or covers finish, and a sparkle for an achievement; **MENU > Side Lights** turns each on or off, and plays each on the lights as you move over it. On the charger, the shelf's screen goes dark after the Auto Off time, and any button lights it again; USB File Transfer's goes dark too when nothing is moving, and only dims while files are on their way.
- **Battery care:** a battery too low to start says so and charges first; a charger plugged in while the Pixel 2 is off shows the charge and the time to full (hold POWER to start); **Getting hot** warns before the handheld slows down to cool, over a game or a port; and **MENU > Charge Limit** stops charging at 80, 85 or 90% to keep the battery young, and holds it within 2 points of the limit on the cable (78 to 80% at 80%). Need it all for a trip? **Y** on the charging screen charges to 100% this time; the limit is back when the cable comes out.
- **About,** besides the version and the card, tells [the story of the name](#the-name) beside a photo of Zyper, and thanks the projects ZyperOS is built on.
- **Play Time:** how long you've played each game or console, today, this week, this month, this year or ever.
- **A way out of anything:** hold **MENU + START** for a second and whatever runs ends, a stuck game or port included, and the home comes back. After a crash, the Pixel 2 starts on the home rather than back in the game. In the in-game menu, **X** quits from any row.
- **Continue or from the start, favorites and sorting:** A continues a game where you left it and Y starts it from the start; Y on a game's details (X) favorites it, and a heart before its name says so on its console's shelf; each console sorts by name, play time, last played or recently added.

## The name

<img align="right" src="res/zyper.png" width="200" alt="Zyper, a grey miniature schnauzer, sitting on the grass with his tongue out">

ZyperOS is named after **Zyper**, my dog, who passed away. He was quick, light on his feet and full of energy, and that is what this system tries to be.

The name is also two words in one: **zippy** and **hyper**. Games start the moment you press A, the menus never make you wait, and the whole system is small enough to come up in about two seconds.

## How it compares

The GKD Pixel 2 has several operating systems to choose from. This is ZyperOS beside the one it comes with and four others, from what each one's own pages, code and releases say in November 2026. ROGUE OS set the bar for this handheld, and ZyperOS set out to match it.

| | ZyperOS | Stock<sup>1</sup> | ROGUE OS | TortOS | ROCKNIX | plumOS Pixel2 |
|---|---|---|---|---|---|---|
| **Source, license** | GPL-3.0 | — | Its changed files, no license named | GPL-3.0 | GPL-2.0 for its own code | MIT for its own code |
| **Install, latest** | Card image or ZyperOS Installer, v1.0.5 (Nov 2026) | On the card it comes with | Card image, then v1.22's files (Aug 2025) | Card image or TortOS Installer, v2.1.1 (Oct 2026) | Card image, 20261101 (Nov 2026) | Card image, v0.1.4 (Aug 2026), in testing |
| **Systems** | 44, and Ports | Over 100 folders | 117<sup>2</sup> | 11 | 120<sup>2</sup> | 86<sup>2</sup> |
| **Games without taking the card out** | Wi-Fi or USB web page at zyperos.local, its own hotspot, SSH | Samba | USB cable, SSH | Wi-Fi or USB web page | Samba, SFTP, USB cable | Samba, SFTP, FTP |
| **Box art on the device** | libretro, ScreenScraper<sup>3</sup>, SteamGridDB | On a computer (Skraper) | ScreenScraper | libretro, ScreenScraper | ScreenScraper | libretro |
| **RetroAchievements** | Yes | — | — | Yes | Yes | — |
| **Achievements offline** | Yes: sent when Wi-Fi is back, dated when earned | — | — | Yes: sent later | No: needs the internet | — |
| **System updates** | Over Wi-Fi or the USB cable, A/B with roll back | — | Copy the new files | TortOS Installer, on a computer | Over the air, or a file | A file you copy, A/B with roll back |
| **First-start tutorial** | Yes | — | — | — | — | — |
| **Themes** | Light and dark, or by the clock; ten accents, three backgrounds each, square or rounded corners | EmulationStation themes | 13 launcher skins | 2 card art styles | Yes, with a downloader | Your own themes |
| **Languages** | 8: English, 简体中文, Español, Português (Brasil), Français, Deutsch, 日本語, Italiano | — | 21, in its launcher | English | 36 | 6 |
| **Menu music** | 10 free tracks and yours, from a folder of its own | — | — | — | Yes | — |
| **Media player** | Music, audiobooks, video | — | Music, video | Music, audiobooks | Music, video | Music |
| **PortMaster** | Yes, installed from the Ports shelf with one press, with the in-game menu over every port | Yes | Yes | — | Yes | Yes |
| **Rumble** | Yes | — | Yes | No | Yes | — |
| **Shaders** | Sharp-Shimmerless, CRT (flat or curved), scanlines and an LCD grid, for one system or every system | — | A screen filter in DraStic | — | Built in, per game or per system | — |
| **Side lights** | Battery, volume and brightness, files moving and finished, achievements: each on or off | — | — | Battery | Battery | — |
| **Folders of games, and a list** | Folders to any depth, each with its count; covers or a list for each system | EmulationStation's | — | — | EmulationStation's | — |
| **Storage by part and by game** | Yes, with recommendations and media pages | — | — | — | — | — |
| **Pick a game's cover on the device** | From three sources, a big look first, the old one kept to put back | — | — | — | — | — |
| **Rename a game on the device** | With its saves, states, art and EmulationStation entry | — | — | — | — | — |
| **Video previews on the shelf** | Yes | — | — | — | — | — |
| **Charge limit** | 80, 85 or 90% | — | — | — | — | — |
| **Battery time left** | Time left, or time to full on the cable | — | — | — | — | — |
| **A DS game's language** | Picked per game, whatever `firmware.bin` says | — | — | — | — | — |
| **Charging screen when off** | Yes | — | — | — | — | — |
| **A second player in a browser** | NES games: a friend's phone or computer, over the handheld's own Wi-Fi or the USB cable | — | — | — | — | — |
| **Boot time** | 1.6 s<sup>4</sup>, about 2 s from power on<sup>5</sup> | 30 to 40 s from power on<sup>5</sup> | — | 2.2 s<sup>4</sup> | — | — |

— means none of the sources below says either way. The Pixel 2 has no Wi-Fi of its own, so on every system a Wi-Fi feature needs a USB Wi-Fi adapter. Knulli, muOS, twigUI (spruceOS) and a MinUI port run on the Pixel 2 too.

1. GameKiddy publishes no documentation for the stock firmware. Its column is from Retro Game Corps' review of February 2025, and the firmware may have changed since.
2. No count is published, so this is the systems in its own list, counted without tools, media players and ports. Hacks and regions can be entries of their own.
3. In builds made with ScreenScraper's developer key.
4. Each project's own number, and not measured the same way: ZyperOS's from the kernel's start to the home screen, on the Pixel 2 (mean of 10 starts), and TortOS's from power on to its shelf.
5. My own count on my Pixel 2, from power on until it can be used, with the stock card it came with and with ZyperOS: the stock firmware takes over 30 seconds, usually close to 40; ZyperOS about 2.

<details>
<summary><b>Sources</b></summary>

- **ZyperOS:** this README and its code, [v1.0.5](https://github.com/tarikbc/ZyperOS/releases/tag/v1.0.5); the boot times I measured on my Pixel 2 (notes 4 and 5) and [#83](https://github.com/tarikbc/ZyperOS/pull/83).
- **Stock:** [Retro Game Corps, February 2025](https://retrogamecorps.com/2025/02/01/how-to-make-the-gkd-pixel-ii-software-suck-less/); the boot time is my own count (note 5).
- **ROGUE OS:** [README](https://github.com/Ninoh-FOX/PIXEL2-ROGUE-OS/blob/f567073e9dd9d5f688a23728067d043af2fbdc20/README.md), [v1.22](https://github.com/Ninoh-FOX/PIXEL2-ROGUE-OS/releases/tag/GKDPIXELII_1.22), [system list](https://github.com/Ninoh-FOX/PIXEL2-ROGUE-OS/blob/f567073e9dd9d5f688a23728067d043af2fbdc20/EMUELEC/SYSTEM/usr/local/share/emulationstation/es_systems.cfg), [launcher skins](https://github.com/Ninoh-FOX/PIXEL2-ROGUE-OS/tree/f567073e9dd9d5f688a23728067d043af2fbdc20/EMUELEC/SYSTEM/usr/local/share/iux/skins), [launcher languages](https://github.com/Ninoh-FOX/PIXEL2-ROGUE-OS/tree/f567073e9dd9d5f688a23728067d043af2fbdc20/EMUELEC/SYSTEM/usr/local/share/iux/translations).
- **TortOS:** [README](https://github.com/ericreinsmidt/TortOS/blob/aafe6dee0d8221642e68d8b17fb17c6c5a39f8e1/README.md), [guide](https://github.com/ericreinsmidt/TortOS/blob/aafe6dee0d8221642e68d8b17fb17c6c5a39f8e1/docs/guide.md), [plastron's README](https://github.com/ericreinsmidt/plastron/blob/b67f5ec86a601f878f1bc8452d16af7112950f3d/README.md), [v2.1.1](https://github.com/ericreinsmidt/TortOS/releases/tag/v2.1.1).
- **ROCKNIX:** [README](https://github.com/ROCKNIX/distribution/blob/13d241a7d35fbe9b895ef04519f62bdb3a57d31f/README.md), [features](https://rocknix.org/), [adding games](https://rocknix.org/play/add-games/), [scraper](https://rocknix.org/configure/scraper/), [RetroAchievements](https://rocknix.org/play/retro-achievements/), [updates](https://rocknix.org/play/update/), [themes](https://rocknix.org/configure/themes/), [shaders](https://rocknix.org/configure/shaders/), [RK3326 systems](https://github.com/ROCKNIX/distribution/blob/13d241a7d35fbe9b895ef04519f62bdb3a57d31f/documentation/PER_DEVICE_DOCUMENTATION/RK3326/SUPPORTED_EMULATORS_AND_CORES.md), [Pixel 2 device tree](https://github.com/ROCKNIX/distribution/blob/13d241a7d35fbe9b895ef04519f62bdb3a57d31f/projects/ROCKNIX/devices/RK3326/linux/dts/rockchip/rk3326s-gkd-pixel2.dts) and [settings](https://github.com/ROCKNIX/distribution/tree/13d241a7d35fbe9b895ef04519f62bdb3a57d31f/projects/ROCKNIX/packages/hardware/quirks/devices/GameKiddy%20GKD%20Pixel2), [menu music](https://github.com/ROCKNIX/emulationstation-next/blob/f6c542872e3850c5f432929ead219547a897cd4a/es-app/src/guis/GuiMenu.cpp), [languages](https://github.com/ROCKNIX/emulationstation-next/blob/f6c542872e3850c5f432929ead219547a897cd4a/es-app/src/guis/GuiMenu.cpp#L1627-L1670), [20261101](https://github.com/ROCKNIX/distribution/releases/tag/20261101).
- **plumOS Pixel2:** [README](https://github.com/game-de-it/plumOS-pixel2_v2/blob/dec872c042229c1d38443f7e8e3fe962fc369e81/README.md), [network](https://github.com/game-de-it/plumOS-pixel2_v2/blob/dec872c042229c1d38443f7e8e3fe962fc369e81/docs/user/network.md), [updates](https://github.com/game-de-it/plumOS-pixel2_v2/blob/dec872c042229c1d38443f7e8e3fe962fc369e81/docs/user/updates.md), [folders](https://github.com/game-de-it/plumOS-pixel2_v2/blob/dec872c042229c1d38443f7e8e3fe962fc369e81/docs/user/storage.md), [system list](https://github.com/game-de-it/plumOS-pixel2_v2/blob/dec872c042229c1d38443f7e8e3fe962fc369e81/package/frontend-pixel2/systems.json), [scraper](https://github.com/game-de-it/plumOS-pixel2_v2/blob/dec872c042229c1d38443f7e8e3fe962fc369e81/package/app-layer-pixel2/bin/plumos-thumbnail-scraper), [languages](https://github.com/game-de-it/plumOS-pixel2_v2/tree/dec872c042229c1d38443f7e8e3fe962fc369e81/vendor/plumos-frontend/seed/share/frontend/lang), [v0.1.4](https://github.com/game-de-it/plumOS-pixel2_v2/releases/tag/v0.1.4).
- **The others:** [Knulli 20260511](https://github.com/knulli-cfw/knulli-linux/releases/tag/20260511), [muOS](https://github.com/MustardOS/mustardos.github.io/blob/HEAD/_data/devices/rk-pixel-2.yml), [twigUI](https://github.com/spruceUI/twigUI-next), [MinUI for the Pixel 2](https://github.com/NukeBomB3000/minui-gkd-pixel2).

</details>

## Light and dark

ZyperOS has two themes, Beard in light and Salt & Pepper in dark. **MENU > Display > Appearance** chooses `Light`, `Dark` or `Auto`, which is light from 7:00 to 19:00 by the clock or at the hours you set in **Auto Hours**, and the first start asks too. Every screen follows it. The File Transfer page follows the phone or computer it is open on instead.

**MENU > Display > Theme** makes them yours: an accent from ten (lime, green, teal, blue, purple, pink, red, orange, yellow and silver), a background for each theme (warm, neutral or cool grey in light; black, deep blue or dark grey in dark), rounded or square corners, and Zyper on or off. Both themes are drawn small at the top as you change them, and **Reset to Default** puts them back. ZyperOS works out every other color from the two you pick and keeps every word at the contrast it has as it ships.

<table>
  <tr>
    <td><img src="res/readme/display.png" width="360" alt="MENU's Display tab, on Theme, with both themes' chips beside it"></td>
    <td><img src="res/readme/theme.png" width="360" alt="The Theme page: both themes drawn small, then Accent, Light Background and Dark Background"></td>
  </tr>
</table>

<div align="center">

<table>
  <tr>
    <th>Light (Beard)</th>
    <th>Dark (Salt &amp; Pepper)</th>
  </tr>
  <tr>
    <td><img src="res/readme/shelf-n64.png" width="360" alt="The Nintendo 64 shelf in the light theme, Super Mario 64's box in the middle"></td>
    <td><img src="res/readme/dark/shelf-n64.png" width="360" alt="The Nintendo 64 shelf in the dark theme, Super Mario 64's box in the middle"></td>
  </tr>
  <tr>
    <td><img src="res/readme/list.png" width="360" alt="The Game Boy Advance shelf as a list in the light theme"></td>
    <td><img src="res/readme/dark/list.png" width="360" alt="The Game Boy Advance shelf as a list in the dark theme"></td>
  </tr>
  <tr>
    <td><img src="res/readme/systems.png" width="360" alt="The Spotlight home in the light theme"></td>
    <td><img src="res/readme/dark/systems.png" width="360" alt="The Spotlight home in the dark theme"></td>
  </tr>
  <tr>
    <td><img src="res/readme/media.png" width="360" alt="A game's Cover and Video page in the light theme"></td>
    <td><img src="res/readme/dark/media.png" width="360" alt="A game's Cover and Video page in the dark theme"></td>
  </tr>
</table>

</div>

## Shaders

**Shader**, under Display in the in-game menu, draws the game one of six ways: `Off`, then Sharp-Shimmerless for sharp, even pixels, CRT and CRT Curved for an old TV, Scanlines alone, and LCD Grid for a handheld's screen. It is kept for the game's system and changes the game when you continue; a system's own menu has the same row, and **MENU > Display > Shader** sets every system at once. Here they are on the Pixel 2's 640x480 screen, a part of the picture at twice its size:

<div align="center">

<img src="res/readme/shaders.png" width="720" alt="Balloon Fight's balloon fighters six ways: Off, Sharp-Shimmerless, CRT, CRT Curved, Scanlines and LCD Grid">

</div>

The CRT's lines are most even where the picture's height is a whole number of times the game's, as with **Display** on `Integer tall`. The CRT, Scanlines and LCD Grid are drawn by the Pixel 2's GPU. Sharp-Shimmerless is zadpos's, and so are the scanlines and the grid; the CRT is Timothy Lottes's. Each is in the public domain ([Credits](#credits)).

## Supported systems

| Console | Put games in | File types | Core |
|---|---|---|---|
| Atari 2600 | `Roms/Atari 2600` | `.a26` `.bin` `.zip` | Stella 2014 |
| Game & Watch | `Roms/Game & Watch` | `.mgw` | Game & Watch |
| ColecoVision | `Roms/ColecoVision` | `.col` `.cv` `.bin` `.rom` `.zip` | Gearcoleco |
| Atari 5200 | `Roms/Atari 5200` | `.a52` `.bin` `.zip` | a5200 |
| Vectrex | `Roms/Vectrex` | `.vec` `.bin` `.zip` | vecx |
| NES | `Roms/NES` | `.nes` `.fds` `.unf` `.unif` `.zip` | FCEUmm |
| MSX2 | `Roms/MSX2` | `.rom` `.mx1` `.mx2` `.dsk` `.cas` `.zip` | blueMSX |
| Atari 7800 | `Roms/Atari 7800` | `.a78` `.bin` `.zip` | ProSystem |
| Master System | `Roms/Master System` | `.sms` `.zip` | Genesis Plus GX |
| Game Boy | `Roms/Game Boy` | `.gb` `.dmg` `.zip` | mGBA |
| Genesis | `Roms/Genesis` | `.md` `.gen` `.bin` `.smd` `.zip` | Genesis Plus GX |
| TurboGrafx-16 and CD | `Roms/TurboGrafx-16` | `.pce` `.sgx` `.cue` `.ccd` `.chd` `.toc` `.m3u` `.zip` | Beetle PCE Fast |
| Atari Lynx | `Roms/Atari Lynx` | `.lnx` `.o` `.zip` | Handy |
| Game Gear | `Roms/Game Gear` | `.gg` `.zip` | Genesis Plus GX |
| Neo Geo | `Roms/Neo Geo` | `.zip` `.7z` | FinalBurn Neo |
| SNES | `Roms/SNES` | `.sfc` `.smc` `.zip` | Snes9x 2010 |
| Sega CD | `Roms/Sega CD` | `.chd` `.cue` `.iso` `.m3u` | PicoDrive |
| 3DO | `Roms/3DO` | `.iso` `.chd` `.cue` | Opera |
| Doom | `Roms/Doom` | `.wad` `.iwad` `.pwad` `.m3u` | PrBoom |
| 32X | `Roms/32X` | `.32x` `.zip` | PicoDrive |
| Saturn | `Roms/Saturn` | `.cue` `.chd` `.iso` `.ccd` `.mds` | YabaSanshiro |
| Virtual Boy | `Roms/Virtual Boy` | `.vb` `.vboy` `.zip` | Beetle VB |
| PlayStation | `Roms/PlayStation` | `.cue` `.chd` `.pbp` `.m3u` `.ccd` `.toc` `.iso` `.mdf` `.cbn` `.exe` | PCSX ReARMed |
| Neo Geo CD | `Roms/Neo Geo CD` | `.chd` `.cue` | NeoCD |
| Nintendo 64 | `Roms/Nintendo 64` | `.n64` `.z64` `.v64` `.zip` | ParallelN64, or Mupen64Plus-Next |
| Neo Geo Pocket | `Roms/Neo Geo Pocket` | `.ngp` `.ngc` `.ngpc` `.npc` `.zip` | Beetle NeoPop |
| Game Boy Color | `Roms/Game Boy Color` | `.gbc` `.cgb` `.zip` | mGBA |
| WonderSwan | `Roms/WonderSwan` | `.ws` `.wsc` `.zip` | Beetle WonderSwan |
| Neo Geo Pocket Color | `Roms/Neo Geo Pocket Color` | `.ngp` `.ngc` `.ngpc` `.npc` `.zip` | Beetle NeoPop |
| Dreamcast | `Roms/Dreamcast` | `.chd` `.cdi` `.gdi` `.cue` | flycast2021, or Flycast if you add it |
| WonderSwan Color | `Roms/WonderSwan Color` | `.ws` `.wsc` `.zip` | Beetle WonderSwan |
| J2ME | `Roms/J2ME` | `.jar` `.kjx` | FreeJ2ME-Plus |
| Game Boy Advance | `Roms/Game Boy Advance` | `.gba` `.agb` `.zip` | mGBA |
| ScummVM | `Roms/ScummVM` | a folder per game | ScummVM |
| Pokémon Mini | `Roms/Pokemon Mini` | `.min` `.zip` | PokeMini |
| OpenBOR | `Roms/OpenBOR` | `.pak` | OpenBOR |
| Nintendo DS | `Roms/Nintendo DS` | `.nds` `.zip` | DSperate |
| PSP | `Roms/PSP` | `.iso` `.cso` `.chd` `.pbp` | PPSSPP |
| PICO-8 | `Roms/PICO-8` | `.p8` `.png` | FAKE-08 |
| TIC-80 | `Roms/TIC-80` | `.tic` `.png` | TIC-80 |
| Arcade | `Roms/Arcade` | `.zip` `.7z` | FinalBurn Neo |
| MAME | `Roms/MAME` | `.zip` | MAME 2003-Plus |
| NAOMI | `Roms/NAOMI` | `.zip` `.7z` | flycast2021 |
| Atomiswave | `Roms/Atomiswave` | `.zip` `.7z` | flycast2021 |
| Ports | `Roms/Ports` | `.sh`, with the port's folder beside it | PortMaster |

EmulationStation's folder names work too, as they are: `nds`, `gba`, `psx`, `megadrive`, `pcengine` and the rest ([coming from EmulationStation](#coming-from-emulationstation)).

**BIOS files** go loose in `Bios/`. TurboGrafx-CD games need `syscard3.pce` and PlayStation games `scph5501.bin`. Sega CD games need `bios_CD_U.bin`, and a European or Japanese disc `bios_CD_E.bin` or `bios_CD_J.bin` as well. Neo Geo CD games need the Neo Geo CD BIOS in a folder of its own, `Bios/neocd/neocd.bin`. The 32X needs no BIOS. A multi-disc PlayStation game goes in a folder of its own with an `.m3u` listing its discs. Game Boy Advance runs without its BIOS; add `gba_bios.bin` if you want the original boot animation. The Atari 7800 runs without one too; add `7800 BIOS (U).rom` for the Atari logo at start. The Dreamcast, NAOMI and Atomiswave need their BIOS in `Bios/dc/`: `dc_boot.bin`, `naomi.zip` and `awbios.zip`. The 3DO needs `panafz10.bin` and the Lynx `lynxboot.img`; the Atari 5200 and the Pokémon Mini run without theirs (`5200.rom`, `bios.min`). MSX2 games need blueMSX's `Machines` and `Databases` folders in `Bios/`. Saturn games need the Saturn's BIOS, `saturn_bios.bin` (or `sega_101.bin` or `mpr-17933.bin`). ColecoVision games need the ColecoVision's BIOS, `coleco.rom` (or `colecovision.rom` or `os7.u2`): without it the shelf says so under the game. The PSP needs no BIOS.

**Folders inside a system's folder** are folders of games on its shelf, as deep as you like: `Roms/Game Boy Advance/Rom Hacks/Translations/Mother 3.gba` shows as the folder Rom Hacks, with Translations in it. A folder is a game's own when it holds that game's set: an `.m3u`, a PSP game's `EBOOT.PBP`, two or more discs of the one game, or a `.cue` (or `.gdi`) with its tracks; or when its one disc image is named as the folder is (`God of War/God of War (USA).iso`). Otherwise it is a folder of games, also with one game in it, so `PSP/Favorites/Ridge Racer (USA).iso` shows as the folder Favorites with Ridge Racer in it. On the ScummVM shelf a folder with a game's files in it is the game, and on the Ports shelf the folders are the ports' own. Saves and save states go by the game's file name, as they always have, so two copies of one game in two folders share them.

**Nintendo DS** runs on DSperate, which ZyperOS ships, with ZyperOS's own in-game menu. A game starts with the top screen alone. While it runs, DSperate gives its threads a core each, as its own frontend does, and the CPU stays at its top clock: a Mario Kart DS race keeps 60 frames a second on the Pixel 2. **L1** and **R1** are the DS's L and R. **L2** shows the other screen. **R2** shows the bottom screen and turns the stylus on: the d-pad moves a cursor and **A** touches. **R2** again turns the stylus off and goes back. The in-game menu also has **Screens** (Single, Stacked or Top Focus), **Swap Screens** and **Stylus**. A DraStic save (`Saves/<game>.dsv`, from ROGUE OS or another system) is taken over the first time a game runs on DSperate. Encrypted ROM dumps need `bios7.bin`, `bios9.bin` and `firmware.bin` in `Bios/`. A DS game reads its language from the firmware, and a `firmware.bin` carries the language of the console it was dumped from, so ZyperOS tells DSperate which language to use with each game and leaves the file alone. **Game Language** in the console's menu, under **Core**, is **Auto** to start with (the language of **MENU > Language** where the DS has it, English where it doesn't, as Português (Brasil) and 简体中文 are not on a DS) or one of English, Español, Français, Deutsch, Italiano, 日本語 and 简体中文 outright; 简体中文 is for the iQue DS's games, which are the only ones that have it. A game resumes where you left it only in the language it was left in: after a change of **Game Language** it starts fresh, in the new one, with a note that says why, and its battery save keeps your progress. A save state you load yourself keeps the language it was saved in, and **Reset** in the in-game menu starts the game again in the row's language.

**Arcade games are ROM sets,** one `.zip` per game, named the way FinalBurn Neo names it (`ssf2.zip` is Super Street Fighter II). Keep the name and keep the zip whole: the name says which game it is, and the core checks every chip inside. Vertical games such as 1941 are turned upright on their own. **Neo Geo games** are FinalBurn Neo sets too (`mslug.zip` is Metal Slug), and every one needs `neogeo.zip`, the Neo Geo's own BIOS set, in `Bios/` or beside the game.

<img align="right" width="320" src="res/readme/scummvm.png" alt="The ScummVM shelf: freeware games, each a folder, as cards">

**ScummVM games** are folders: copy each game's folder, as it came, into `Roms/ScummVM`, and the shelf shows it under the folder's name. ScummVM works out which game it is from the files. A `.scummvm` file in the folder, holding the game's ScummVM ID (`sky` for Beneath a Steel Sky, `queen` for Flight of the Amazon Queen), names it for certain, for the rare folder ScummVM reads as more than one game. **A** clicks, **B** right-clicks and the d-pad moves the pointer (**R1** or **R2** held makes it slow); **Y** skips a cutscene, **START** opens ScummVM's menu, where the game saves and loads, and **SELECT** a keyboard. ScummVM has no save states, so save from that menu before you quit.

<br clear="right">

**J2ME games** are the `.jar` files of the Java phones. They run on FreeJ2ME-Plus, which brings its own Java: the first J2ME game you start unpacks it into `Bios/` (about 60 MB), which takes a little while once. A phone's keys are on the pad: the d-pad is its arrows, **A** its OK key, **SELECT** and **START** the left and right soft keys, **X** 0, **B** 7, **Y** 9, **L1** 1, **R1** 3, **L2** `*` and **R2** `#`.

<img align="right" width="320" src="res/readme/openbor.png" alt="The in-game menu over a paused OpenBOR game: Continue, Controls and Quit">

**OpenBOR games** are modules, one `.pak` file each, run by OpenBOR itself rather than a libretro core. **A** attacks, **B** jumps, **X** is the second attack, **Y** the special, **L1** and **R1** the third and fourth, **START** pauses into OpenBOR's own menu and **SELECT** saves a screenshot. **MENU** pauses the game under ZyperOS's in-game menu, as in any other game: **Continue**, **Controls**, which lists those buttons, and **Quit**, back to the shelf (**X** quits from any row). OpenBOR keeps its own saves and has no save states, so the menu has no Save, Load or Reset. The volume and brightness bars work over it, and **POWER** ends the game and turns the handheld off. A module that came as a folder has to be packed into a `.pak` first. The Pixel 2 only.

<br clear="right">

**Saturn games** run on YabaSanshiro and need the Saturn's BIOS in `Bios/` (above). The Saturn is the most a Pixel 2 is asked to run: it runs some games, and not at full speed. Flicky's Flock, a homebrew game, ran at about 37 frames a second of 60.

**PICO-8 carts** are `.p8` files or `.p8.png` images and run on FAKE-08, an open reimplementation: you don't need Lexaloffle's PICO-8.

<img align="right" width="320" src="res/readme/controls-psp.png" alt="A PSP game's Controls page in the in-game menu: L1/R1 are L/R, R2 is the d-pad as the stick, START/SELECT are Start/Select">

**PSP games** run on PPSSPP, with no BIOS: copy the `.iso`, `.cso`, `.chd` or `.pbp` into `Roms/PSP`. ZyperOS ships PPSSPP's own files, the PSP's fonts among them, and puts them in `Bios/PPSSPP`. The PSP's buttons are where a PlayStation pad has them, **L1** and **R1** its L and R. The Pixel 2 has no analog stick, and a PSP game moves with its stick or with its d-pad, game by game. A game starts with the d-pad as the d-pad; **R2** turns it into the analog stick, and **R2** again turns it back. ZyperOS remembers this for each game, so a game you play with the stick starts with it the next time. A game's own saves are in `Saves/PSP/SAVEDATA`, as on a PSP's memory stick, so saves from PPSSPP on another system copy straight in. PPSSPP runs at the PSP's own 480x272 and draws every frame. A heavy 3D game can still run slower than a real PSP on the Pixel 2.

<br clear="right">

**Doom games** are WADs, run by PrBoom. An IWAD is a whole game: `doom.wad`, `doom2.wad`, `tnt.wad` and `plutonia.wad` from id Software's games, or `freedoom1.wad` and `freedoom2.wad` from [Freedoom](https://freedoom.github.io), which are free. A mod is a PWAD: put it beside its IWAD, and it runs with the first IWAD it finds there, Doom II's first. For a mod that wants another IWAD, or several mods at once, write an `.m3u` that lists the IWAD and then the mods, one file a line. The d-pad walks and turns, **B** fires, **Y** runs, **A** opens doors and presses switches, **L1** and **R1** step sideways, **L2** and **R2** change the weapon, **SELECT** shows the map and **START** opens Doom's menu. The handheld rumbles when you are hit. Save states work inside a level.

**ColecoVision games** run on Gearcoleco and need its BIOS (above). **B** and **A** are the controller's left and right buttons. Its keypad is on the rest: **Y** 1, **X** 2, **L1** 3, **R1** 4, **L2** 5, **R2** 6, **START** `*` and **SELECT** `#`. A game's menu asks for 1 to 4 for a one-player game.

**TIC-80 carts** are `.tic` files, or `.png` cart images, and run on TIC-80's own core, in any of its languages: Lua, MoonScript, Fennel, JavaScript, Python, Wren, Squirrel, Janet, Scheme or WebAssembly. A cart's saved data is kept, but TIC-80 has no save states.

## Install

ZyperOS 1.0.5 is out for the GKD Pixel 2, released in November 2026. [The latest release](https://github.com/tarikbc/ZyperOS/releases/latest) on GitHub has the card image and [ZyperOS Installer](installer/) for macOS, Windows and Linux. The TrimUI Brick's image comes later.

1. **Write the card.** Download ZyperOS Installer for your computer from [the latest release](https://github.com/tarikbc/ZyperOS/releases/latest): the `.zip` for macOS, the `.exe` for Windows, the `.deb` or `.rpm` for Linux. Put the microSD card in the computer, pick the GKD Pixel 2, the latest release and the card, then choose **Fresh install**, which erases the card. Or download the card image, `ZyperOS-v<version>-pixel2.img.xz`, and write it with any program that writes disk images, like Raspberry Pi Imager.
2. **Put the card in the Pixel 2 and turn it on.** The first start sets the card up in about a minute, turning the rest of it into space for games, music and saves. Every boot after that comes straight up, and the first one ends with [the tutorial](#the-first-start). If it ever stops partway, the card's PX2BOOT drive has `zyperos-setup.txt`, a record of what it did: send it to me in [an issue](https://github.com/tarikbc/ZyperOS/issues).
3. **Set the time zone** under **MENU > Date & Time**. Wi-Fi sets the time whenever it connects; without it, set the time there too, and it keeps it.
4. **Add games** to the folders in `Roms/`. Put the card back in the computer, where it shows up as ZYPEROS, or send them over, with Wi-Fi or the USB cable: [a card with no games](#a-card-with-no-games) shows both ways. Windows may also ask to format two drives it can't read: those are ZyperOS's system, so choose **Cancel** ([the card in a computer](docs/guide.md#the-card-in-a-computer)).
5. **Add the extras you want.** [PortMaster](#portmaster) installs from the Ports shelf over Wi-Fi. Copy Flycast to `Emulators/Flycast`. A shelf with nothing in it stays hidden, except Ports, which holds PortMaster's entry until you install it.

New versions come to the handheld with [System Update](#system-update) over Wi-Fi, or over the USB cable from [the File Transfer page](#the-file-transfer-page). On a computer, the installer's **Update** puts a new version on a card that has ZyperOS and keeps its games, music and saves. To build the card image yourself instead, see [For developers](#for-developers).

The Pixel 2 has no radio of its own. A USB Wi-Fi adapter in its USB-C port gives it Wi-Fi while it's plugged in, and with it box art, RetroAchievements and Wi-Fi File Transfer: one whose Realtek chip ZyperOS has a driver for, like the TP-Link TL-WN725N on a USB-C adapter. Its power saving is off while it is on a network, since this chip is known to drop off some routers over and over with it on. Without one, USB File Transfer (below) moves files over the USB cable. There's no Bluetooth.

The Pixel 2 powers what you plug into its port only when you say so. Its USB-C port has no chip to tell a Wi-Fi adapter or USB-C headphones from a computer that has not connected yet, and a computer must never be powered by the handheld. So when something like that is plugged in, the shelf asks, **What did you plug in?**, as a list where each choice says what it is for: **USB Accessory** (headphones, Wi-Fi, keyboard), **Computer**, where it starts (files, or the card as a flash drive), or **Phone** (files in the phone's browser). For a computer it says your computer didn't connect: unplug the cable and plug it back in. It can take a few tries, since the two USB-C ports settle which side gives power at each plug, and some computers' USB-C ports take the waiting side every time, as a phone's does. A USB-A to USB-C cable, with USB-A at the computer, connects every time, so the message says that too; a computer with only USB-C ports can use a USB-C to USB-A adapter or a USB-C hub with that cable. A phone takes the same side every time, so the handheld becomes its USB host: **Phone** powers the phone and asks you to turn on its USB tethering, and then USB File Transfer opens by itself for the phone's own browser ([how](#from-a-phone-on-the-usb-cable)). A phone with no USB tethering is told to use Wi-Fi File Transfer, or the adapter below for **Flash Drive**. An iPhone shares its network over USB only with a computer it trusts, so it gets Wi-Fi File Transfer instead, or a USB-C to USB-A adapter on the iPhone and a USB-A to USB-C cable, which also give any phone **Flash Drive**. Nothing comes up over a game: something plugged in while you play is asked about when you're back on the shelf. An accessory that was powered when you turned the Pixel 2 off is powered again at the next start, with no question, and a question that pops up by itself ignores a button you were already holding, and any press in its first half second, so a press meant for the menu doesn't answer it. While a plug waits for an answer, a small USB mark shows in the top corner, and **MENU > USB Device** asks again, so a wrong answer is never stuck. To skip the question, set **MENU > USB Accessories** to **Automatic**: accessories get power two seconds after they go in. Its one risk, said when you choose it: a computer that has not connected yet gets about two seconds of power before the handheld sees it and stops, and then the next plug is asked about until the port has been empty for a few seconds. The second time you answer **USB Accessory**, once it has its power, Zyper offers **Automatic** just once: **Keep Asking** leaves the question on, and either answer is kept.

### Languages

ZyperOS speaks English, 简体中文, Español, Português (Brasil), Français, Deutsch, 日本語 and Italiano. The first start asks for yours before anything else, and **MENU > Language** changes it at once, with no restart. Every screen follows it, down to the charging screen and the boot screen's line, and the File Transfer page follows the handheld. Chinese and Japanese each have a face of their own. Text is never cut to fit: where a language's words run long, that place gets a short form of its own.

<div align="center">

<table>
  <tr>
    <td align="center"><img src="res/readme/tour-language.png" width="240" alt="The first start: Zyper says 你好! beside the eight languages, each in its own words, 简体中文 chosen"><br><sub>Your language first</sub></td>
    <td align="center"><img src="res/readme/menu-pt.png" width="240" alt="The ZyperOS menu in Portuguese, Idioma: Português (Brasil) chosen"><br><sub>MENU in Português</sub></td>
    <td align="center"><img src="res/readme/menu-ja.png" width="240" alt="The ZyperOS menu in Japanese"><br><sub>MENU in 日本語</sub></td>
  </tr>
</table>

</div>

### The first start

The first time ZyperOS starts, Zyper says hello in each of its eight languages in turn, each named in its own words: **UP** and **DOWN** look, the screen speaks the one you are on, and **A** keeps it (**MENU > Language** changes it later). Then he teaches the buttons one at a time, with a picture of the Pixel 2 turned to show where each one is; the shoulder keys light up beside the D-pad (L1/R1 jump) and SELECT (L2/R2 change the song). Then he asks four things, each shown as you look at it: the theme, the home screen, the button sounds (Wood, Glass, Bubble or Off) and, on a card with tracks, the menu music, which plays while On is chosen. **LEFT** and **RIGHT** look, **A** keeps it, and **START** skips the rest. Each choice is a MENU row later, and **MENU > Show Tutorial** runs the tutorial again.

<div align="center">

<table>
  <tr>
    <td align="center"><img src="res/readme/tour-theme.png" width="320" alt="The tutorial asks: Light or dark? The home screen shown in both themes"><br><sub>Light or dark?</sub></td>
    <td align="center"><img src="res/readme/tour-home.png" width="320" alt="The tutorial asks for the home screen: Spotlight or Gallery"><br><sub>Your home screen</sub></td>
  </tr>
  <tr>
    <td align="center"><img src="res/readme/tour-sounds.png" width="320" alt="The tutorial asks for the button sounds: Wood, Glass, Bubble or Off"><br><sub>Button sounds</sub></td>
    <td align="center"><img src="res/readme/tour-music.png" width="320" alt="The tutorial asks for the menu music: On or Off"><br><sub>Menu music</sub></td>
  </tr>
</table>

</div>

### A card with no games

<img align="right" width="360" src="res/readme/empty.png" alt="No games yet: USB File Transfer on A, Wi-Fi File Transfer on Y, dimmed until the Wi-Fi adapter is in">

A card with no games opens on **No games yet** and the two ways to add them: **A** starts USB File Transfer and **Y** Wi-Fi File Transfer. On the Pixel 2 the cable and the Wi-Fi adapter use the same port, so the way that can't start now is dimmed and says why, and it lights up as you plug in or unplug. When the first games arrive, by either way or any other, the home shows them and Zyper cheers.

<br clear="right">

### Adding games over Wi-Fi

<img align="right" width="360" src="res/readme/hare-way.png" alt="Wi-Fi File Transfer before anyone signs in: the address in large type, zyperos.local under it, and the PIN as four large digits">

1. Join a network under **MENU > Wi-Fi**.
2. Open **MENU > Wi-Fi File Transfer**. It shows an address in large type, its name `zyperos.local` under it, and a PIN.
3. Open that address (or `http://zyperos.local`) in a browser on your phone or computer, and type the PIN. [The page](#the-file-transfer-page) opens on ROMs.
4. Drag games into their console's folder, albums into `Music`, books into `Audiobooks` or films into `Videos`. They're on the shelf when you leave the screen.

The PIN is new every time you open Wi-Fi File Transfer. With two ZyperOS handhelds on one network, the second is `zyperos-2.local`, and each screen shows its own name. Over Wi-Fi the address comes first: anyone on the network could answer for a name, and the address on the handheld's own screen is the one to trust. A handheld that finds every name it tries taken stops using a name for that session and shows the address alone. Once someone types the PIN, the screen looks like Flash Drive's: **Download** and **Upload**, each with its speed and how much has moved, a bar under Download for the file coming in, the file's name, and who is connected, with the address and the PIN at the top for the next browser. It stays that way until you close it.

<br clear="right">

#### No Wi-Fi network around?

<img align="right" width="360" src="res/readme/hotspot.png" alt="Wi-Fi File Transfer as the handheld's own network: a QR code, the network's name, its password and the PIN">

On the Pixel 2 with the adapter in and no network joined, Wi-Fi File Transfer makes the handheld its own Wi-Fi network for as long as the screen is open, and its row says `hotspot`.

1. Open **MENU > Wi-Fi File Transfer**. It shows a QR code, the network's name and password, and the PIN.
2. Scan the code with the phone's camera and join the network. A laptop joins with the name and the password.
3. The phone opens the transfer page by itself, at `10.42.1.1`. Type the PIN. The code then makes way for the same Download and Upload screen, with the network, its password and the PIN at the top for another phone.

Close the screen and the network goes, and Wi-Fi is as it was. The phone's sign-in sheet is a small browser: on an iPhone it uploads but can't save a download, and on Android it downloads but can't upload. The page says how to open it in the real browser.

<br clear="right">

### Adding games over the USB cable

<img align="right" width="360" src="res/readme/usb-ask.png" alt="Computer Connected: Charge Only, File Transfer or Developer (SSH)">

The same thing over a cable, with no Wi-Fi, called USB File Transfer. It works with a Mac, Linux, or Windows 11; Windows 10 has no driver for this kind of connection.

On the Pixel 2 the USB port only charges until you say otherwise, so a computer sees nothing at first. When one is plugged in, the shelf asks once (MENU closes for it; a game is never interrupted), **Computer Connected**: **Charge Only** (where it starts, and what **B** means), **File Transfer**, **Flash Drive** (and on development images **Developer (SSH)**). A wall charger is never asked about. **Flash Drive** makes the card a plain flash drive that any computer opens with no driver, Windows 10 included: ZyperOS steps aside, its screen showing how fast the computer copies to the card and reads from it, and how much, until you eject it on the computer or pull the cable, then the card is checked if needed and ZyperOS comes back where it was. The handheld never uses the card while the computer has it.

1. Plug the device into the computer with a USB cable, and choose **File Transfer**. Or open **MENU > USB File Transfer**, which takes Wi-Fi File Transfer's place while a computer is on the cable. While the computer connects, it shows a bar; then its name, `http://zyperos.local`, in large type, its address under it, and a PIN.
2. Open `http://zyperos.local` in a browser on that computer, or the address, `http://10.42.0.1` on the Pixel 2 or `http://10.55.0.2` on the Brick, and type the PIN.
3. Drag games into their console's folder, albums into `Music`, books into `Audiobooks` or films into `Videos`. They're on the shelf when you leave the screen, or, with the transfer behind the shelf, a moment after they arrive.

<br clear="right">

#### From a phone on the USB cable

On the Pixel 2, an Android phone with USB tethering opens USB File Transfer on the same USB-C to USB-C cable, with no adapter and no Wi-Fi:

1. Plug the phone into the handheld. When the handheld asks **What did you plug in?**, choose **Phone**. The handheld powers the phone, and asks you to turn on USB tethering.
2. On the phone, turn on **USB tethering** (Settings, then Hotspot and tethering). The phone shares its network over the cable.
3. USB File Transfer opens by itself, with the address the phone gave the handheld (`192.168.x.y`) and a PIN. Open that address in the phone's browser, and type the PIN.

The phone charges from the handheld while it's in. Unplug the phone or turn USB tethering off, and the screen says the cable is unplugged; **B** closes it. While tethering is on, the handheld also has the phone's internet: it sets its clock from it, and **MENU > Scraper**, a game's Cover and Video, Box Art, **MENU > Achievements**, System Update and PortMaster's install all work over it, as over Wi-Fi. Close File Transfer with **B** and the phone stays connected. Over the phone, which may be on mobile data, nothing large starts by itself: a System Update download cut short waits for you to press **A**. An iPhone shares its network over USB only with a computer it trusts, which the handheld can't be, so use Wi-Fi File Transfer with an iPhone. Some Android devices have no USB tethering at all: my AYN Thor, an Android gaming handheld, offers it neither in Settings nor in its USB notification. With one of those, use Wi-Fi File Transfer too, or Flash Drive with the adapter below; the line under the handheld's message says so. Flash Drive needs the phone to be the host, so it takes a USB-C to USB-A adapter on the phone and a USB-A to USB-C cable.

#### If the page does not open

<img align="right" width="360" src="res/readme/usb-steps.png" alt="USB File Transfer when the computer takes no address: Address waiting, and a hint to check Network adapters in Device Manager">

On the Pixel 2, USB File Transfer shows **Connecting to the computer** and a bar until the computer has its address, then the address and the PIN. Under them it says where the connection is: **Cable**, **Address**, **Browser** or **PIN**, waiting. When a step before the browser waits too long, the screen shows the connection as the handheld sees it, in three steps that tick as the computer gets there, with what to do about the one that waits:

- **Cable**: the computer took the handheld and opened its network. `unplugged` means no computer is on the cable: some USB cables only charge, so try another. Still waiting after 10 seconds, the screen says why: the computer did not answer (try another port or cable), or it has no driver for the handheld (on Windows, look in Device Manager).
- **Address**: the computer took the address the handheld gives it, `10.42.0.2`. After 10 seconds without one, the screen points to Network adapters in Windows' Device Manager.
- **Browser**: a browser asked for the page. The screen shows what to type at full size, so if no browser gets there, check the address against the screen, and turn off any VPN or proxy, which can send the address somewhere else.

Once someone types the PIN, the screen shows File Transfer as usual. If the cable comes out before that, the steps come back.

- **Cable** says `waiting` when the computer is on the cable but didn't connect. After 5 seconds the hint says to unplug the cable and plug it back in, which can take a few tries, or to use a USB-A to USB-C cable, which connects every time.

The cable is its own small network, the handheld at `10.42.0.1` and the computer at `10.42.0.2`, with nothing else on it. That is narrower than any other network the computer is on, so the computer always sends `10.42.0.1` down the cable, even when its Wi-Fi, a VPN or an office network also uses addresses that start with `10.42.0`.

<br clear="right">

While USB File Transfer is open on the Pixel 2, the computer can also open a shell on it: `ssh root@zyperos.local` (or `ssh root@10.42.0.1`), with the PIN as the password. `ssh-copy-id root@zyperos.local` saves your key on the card, so later logins skip the PIN. Closing File Transfer ends every session.

### Leaving the transfer screen

**B** closes File Transfer when nothing is moving. While files move, it asks first, with three choices:

- **Keep going**: the screen goes, and the files keep moving behind the shelf while you use ZyperOS. A mark beside the clock says File Transfer is on, with a ring that fills as a file arrives, and **MENU > USB File Transfer** (or Wi-Fi File Transfer) opens the screen again with the same PIN.
- **Stop after this file**: the file on its way finishes, then File Transfer closes. The page says which files stayed on the computer.
- **Cancel now**: File Transfer closes at once. The file that was half written is deleted, so it never shows on the shelf as a broken game.

A game can't run while files move, because the game takes what moves them: starting one asks **Not now**, **Play after this file** or **Cancel and play**. With nothing moving, File Transfer closes for the game.

### The File Transfer page

The page that opens in the browser is the same for Wi-Fi File Transfer, the handheld's own network and USB File Transfer, on a computer or a phone. It needs no app and no account, only the PIN on the handheld's screen.

<div align="center">

<table>
  <tr>
    <td align="center"><img src="res/readme/portal.png" width="440" alt="The File Transfer page on a computer: the card's folders down the left with their pixel art, an NES folder open, and an upload under the folders with the time left"><br><sub>On a computer: the folders at the left, an upload under them</sub></td>
    <td align="center"><img src="res/readme/portal-pin.png" width="150" alt="The File Transfer page on a phone, asking for the PIN in four boxes, with Zyper above them"><br><sub>The PIN</sub></td>
    <td align="center"><img src="res/readme/portal-phone.png" width="150" alt="The File Transfer page on a phone: the folders as chips, ROMs open with a folder per console, each with its pixel art, and the Upload button"><br><sub>On a phone</sub></td>
  </tr>
</table>

</div>

- **The PIN** goes in four boxes, and the page opens when the fourth digit lands; a paste fills all four. Zyper shuts his eyes while you type. Five wrong PINs and the page waits 30 seconds.
- **The card's eight folders:** ROMs, Music, Audiobooks, Videos, Menu Music, BIOS, Saves and Save States. Every folder wears ZyperOS's pixel art: each console's folder its own picture from the home screen, on a tile in its color, and the others a cartridge, a vinyl album for Music, a book with headphones, a video tape, a cassette for Menu Music, a chip, a memory card and a floppy disk. Hidden folders such as `.media` stay out of the lists.
- **Upload** by dropping files anywhere on the page, into the folder that is open, or onto a folder's row. A dropped folder keeps the folders inside it, and a console's `.media` folder of covers too. **Upload** and **Upload folder** use the file picker instead. The uploads under the folders say how much is left and about how long it takes, and each file shows its progress.
- **The card's free space** is under the folders. Files that don't fit are caught before anything is sent, and the page offers to send the ones that fit.
- **Drag to move:** drag a file or a folder onto a folder's row, or onto a folder above in the path at the top, and it moves there on the card. On a phone, hold a row still for a moment to lift it. **Select** in a row's ⋯ (or Shift, Ctrl or Cmd with a click) picks several, and dragging one moves them all. A disc set's folder moves whole and a `.cue` takes its tracks with it; a game's covers, saves and save states stay where they are, since they go by its name. If the folder already has something of that name, nothing moves and the page says so.
- **Each file and folder has a ⋯** (or a right-click on a computer) for Download, Rename and Delete. Delete always asks first. **New folder** makes one. In an album or a book, **Tidy names** renames its files and the folder to the names MP4 shows, and lists each rename before it makes any.
- **On a phone** the folders are a row of chips, **Upload** is a button under your thumb, and the rest is behind the ⋯ at the top.
- **Settings**, at the foot of the folders (behind the ⋯ on a phone), holds the rest:
  - **Update ZyperOS** over the USB cable, where the handheld has no internet: the page finds the newest release on GitHub and downloads its files to the computer, you drop them on the page, and the handheld checks the release's signature and every file, then installs it as System Update does.
  - **Box art and album covers** over the USB cable use the computer's internet: the page fetches the missing covers from libretro's collection and the Cover Art Archive, and the handheld matches each one to its game or album.
  - **The SteamGridDB key**, pasted in, which beats typing 32 characters on the handheld. The page never shows it again, only its last four characters.
  - **Show hidden folders**, for the covers in `.media`.
  - **Download logs** gets the handheld's logs for a bug report ([below](#faq)).
- **On the handheld's own network** the page opens by itself in the phone's sign-in sheet, with a note on what that small browser can't do.

The page is light or dark as the phone or computer is. When File Transfer closes, the handheld reads the card again, so what arrived is on the shelf.

### Player 2: a friend on a phone

On the Pixel 2, a friend can play an NES game with you as player 2, on their phone or a computer, with nothing to install. Your buttons stay player 1, and the sound stays on the handheld.

1. In an NES game, press **MENU** and choose **Player 2**, under Shader.
2. **With a USB Wi-Fi adapter in,** press **A**. The handheld makes a Wi-Fi network just for this game and shows its QR code, its name and its password. Your friend scans the code with the phone's camera, and the game opens on the phone. If it doesn't open by itself, `10.42.1.1/play` in the phone's browser does it.

   **With a computer on the USB cable,** press **A** and open `zyperos.local/play` in the computer's browser.
3. **Continue** goes back to the game. A notice says when your friend joins and when they leave.

<div align="center">

<table>
  <tr>
    <td align="center"><img src="res/readme/player2-hotspot.png" width="320" alt="Player 2's page over a paused NES game: Scan with your phone's camera; the game opens. A QR code, the network ZyperOS-7A2B and its password, Or open 10.42.1.1/play, and Player 2 waiting"><br><sub>On the handheld: scan to join</sub></td>
    <td align="center"><img src="res/readme/play-phone.png" width="150" alt="The play page on a phone held upright: Player 2 and Picture off at the top, the game's picture, then a d-pad, B and A, SELECT and START"><br><sub>On the phone</sub></td>
    <td align="center"><img src="res/readme/play-phone-side.png" width="330" alt="The play page on a phone on its side: the picture the whole height in the middle, the d-pad and SELECT at the left, B, A and START at the right"><br><sub>On its side</sub></td>
  </tr>
</table>

</div>

The phone shows the game's picture as large as its screen allows, and an NES pad under it: the d-pad, **B** and **A**, SELECT and START, several fingers at once. On its side, the picture takes the whole height between the pad's halves. On a computer the keyboard is the pad: the arrow keys, **Z** for B, **X** for A, **Shift** for SELECT and **Enter** for START. **Picture off** hides the picture for a friend who would rather watch the handheld, and the pad takes the whole screen. The page speaks the handheld's language and wears its theme, light or dark, accent and corners. A button is never left held: if the phone locks or goes out of range, the handheld lets go of player 2's buttons about a second later.

**A** on the Player 2 page stops it, and so does the end of the game, however it ends; the network goes, and Wi-Fi is back as it was. It needs a recent browser (Safari 16.4 or Chrome 103 and later); an older one says so. For now it's NES games, two players, with the touch pad or the keyboard: a game controller paired with the phone doesn't work yet. That needs the page over HTTPS, and comes with a later update.

### System Update

<img align="right" width="360" src="res/readme/update.png" alt="MENU > System Update: 0.1.0 to 0.1.1, System and ZyperOS, A Download, B Back">

**MENU > System Update** keeps ZyperOS up to date over Wi-Fi. It looks each time Wi-Fi connects and once a day, and Zyper tells you once when there is something new. Its page shows the two versions and what's new in full, scrolling with up and down; **A** downloads, checks and installs it, and **Restart Now** starts the new version. The download goes on in the background while you play, with a mark in the top corner showing how far it is, and Zyper tells you when it's ready to install. Auto Off waits for it, and if Wi-Fi drops or the handheld turns off, it goes on from where it stopped once Wi-Fi is back. Only the files that changed download, each one checked against a list signed with ZyperOS's key. It needs 25% battery, or the charger.

A new version starts on trial: if it doesn't start, the handheld goes back to the one before by itself. **Y** rolls back one version by hand. A card installed with two 512 MB system slots, which new installs have, updates the system itself too, written to the slot that isn't running. A card from before the slots, or with smaller ones, gets updates of ZyperOS, and a new system for it comes with a fresh install from the ZyperOS Installer.

Without Wi-Fi, **Update ZyperOS** on [the File Transfer page](#the-file-transfer-page) installs a new version over the USB cable, with the same checks.

<br clear="right">

### PortMaster

<img align="right" width="360" src="res/readme/portmaster.png" alt="Install PortMaster: its crane icon, PortMaster's version, what it is, and the download's bar at 9.8 of 24 MB">

[PortMaster](https://portmaster.games) is the port store Linux handhelds share: PC games made to run on them, such as Stardew Valley and Celeste, each with what it needs. ZyperOS installs the official PortMaster app, as muOS, Knulli and ROCKNIX use it.

On the Ports shelf, PortMaster's entry has a cover of its own, drawn for ZyperOS in its pixel style for the light and dark themes: a dock crane lowering a game cartridge onto a stack of shipping containers. It is the same cover before and after the install, on Favorites too. The entry says **Install** until PortMaster is on the card. **A** opens its page, and **Install** downloads PortMaster's latest stable release from its GitHub over Wi-Fi (a USB Wi-Fi adapter on the Pixel 2), about 24 MB. ZyperOS checks it against the size and checksums PortMaster and GitHub publish, then unpacks it into `Roms/Ports`. Nothing goes in place until all of it checks out, so a cut-off download leaves no half-installed PortMaster. Then **A** opens it, from the page or the shelf, like any port, and PortMaster updates itself from then on.

Its first start unpacks its own files and downloads its catalog of ports, which takes a few minutes. Pick the ports you want in PortMaster; they are on the Ports shelf when you leave it. Many ports of commercial games need the game's own files from a copy you own, and PortMaster says which and where they go: [the File Transfer page](#the-file-transfer-page) sends them from a computer to the port's folder, under **ROMs > Ports**, over Wi-Fi or the USB cable. Ports copied from another system's ports folder run too.

When a port runs, ZyperOS steps aside to give it the memory, puts its own PortMaster settings for the Pixel 2 in place, and keeps the volume keys and **POWER** working. PortMaster is not part of ZyperOS: it comes from the PortMaster team, under the MIT license.

If PortMaster closes with an error, ZyperOS says so when it comes back, with the line of PortMaster's log that tells what went wrong, and offers **Reinstall**. **Reinstall PortMaster** is also on PortMaster's details (**X** on its entry). It puts a fresh copy of PortMaster's latest stable release on the card: your ports, their saves, the runtimes and the themes you downloaded stay, and PortMaster's own settings start over. Before each start, ZyperOS also removes a PortMaster list or settings file that a cut-off write left half-written, so PortMaster makes it again instead of closing as it starts.

<img align="right" width="320" src="res/readme/ports-controls.png" alt="Controls over a port: the d-pad, A, B, X, L1/R1 and START+SELECT as its README says them">

**MENU** over a port, or over PortMaster, pauses it under the in-game menu: **Continue**, **Controls** and **Quit**, back to the Ports shelf. **X** quits from any row, as in every in-game menu. The game stops where it is and nothing you press in the menu reaches it. **Controls** reads the port's own files: what its README says each button does, the keyboard key each button types for it (its gptokeyb keymap), the buttons that work in every port (**MENU**, **MENU** then **X** to quit, the volume keys, **MENU + VOL** for brightness, **SELECT + START**, PortMaster's own way to end a port, and **POWER**), and, last, what its PortMaster entry says to copy where. Over PortMaster it lists PortMaster's own buttons: **Up/Down** move, **L1/R1** a page, **A** chooses, **B** goes back, **X** filters the list, **Y** uninstalls a port. **Quit** ends the port, its helpers and the runtime it mounted, then the shelf comes back.

<br clear="right">

### Removing ZyperOS

Take the card out, and the Brick starts its own system again, unchanged. On the Pixel 2 the system lives on the card, so a card with another system takes its place.

Everything ZyperOS keeps is on the card, Wi-Fi passwords and Bluetooth pairings included, so reformat it before you give it away.

## Controls

| Button | What it does |
|---|---|
| **A / B** | Choose and back. On a shelf, A opens a folder of games and B goes back up one |
| **MENU** | Settings for whatever you're looking at. In a game: save, load, quit and more |
| **X** in the in-game menu | Quit, from any row: the Quit row shows the X button |
| **SELECT** | MP4, the music, audiobook and video player, from anywhere. In MP4, **X** or **SELECT + START** locks the screen and holding **SELECT + START** unlocks it |
| **Up / Down** | In a menu, move the cursor; it stops at the first and the last row and does not wrap. On a shelf, jump to the next or previous letter; on a shelf shown as a list (MENU > View), step along it, and Left and Right jump a letter. On the home, jump a letter with MENU > System Order on A to Z, three systems with it on release date |
| **L1 / R1** | Jump three systems on the home, a screenful of games on a shelf |
| **L2 / R2** | The previous and next song on the home and a shelf: MP4's while it plays, else the menu music's |
| **SELECT + R1 / L1** | In a game, quick save and quick load, to the in-game menu's Quick Slot |
| **SELECT + R2 / L2** | In a game, faster (1.5x, 2x, 3x) and slower, back to normal speed |
| **X / Y** | In a game, turbo A and B. On the shelf, X opens a game's details (Favorite, with Y, and Delete Game) and Y starts a game from the start |
| **START** on a shelf | Covers or a list: turns the row of covers into a list of names and back, as MENU > View does |
| **POWER** | Save and turn off. Over a port, it ends the port first. The next start continues the game; after a crash or a power cut it starts on the home |
| **MENU + START**, held a second | The way out: ends whatever runs, a stuck game or port included, and brings back the home |
| **MENU** over a port | The in-game menu over it: Continue, Controls, Quit (or **X**) |

The device lists every button under **MENU > Controls**, and in a game, **Controls** in the in-game menu lists what each button does in that game. [The guide](docs/guide.md#controls-and-menus) covers every control, menu and setting in detail.

## FAQ

<details>
<summary><b>My games don't show up.</b></summary>

Check the folder name against the table above, spelled exactly, and that the file type is listed for that console. A console with nothing in its folder is hidden. Games sent over Wi-Fi File Transfer appear when you leave its screen. Games copied any other way while the device is on appear after **Rescan Folder** in that console's menu, or after a restart. The log names every file a console's folder left out, and the file types it takes.

</details>

<details>
<summary><b>A game has no box art.</b></summary>

**MENU > Scraper** fetches every missing cover over Wi-Fi. Its page says first what a run will get: the covers it will look for, the video snaps, on which systems, and the room they take. **Y** opens Systems, where **A** turns a console's covers on or off and **X** its videos. The page says about how long a run will take and whether it fits on the card, and one that would not fit does not start. **A** on the page runs it: each cover lands on the screen as it is found, with the time left. A console's own **Box Art** opens the same page for that console alone. Link a free [ScreenScraper](https://www.screenscraper.fr) account in the page's Settings (**START**) and it asks there first, and brings each game's name, year, genre and synopsis for its details screen. ScreenScraper also needs the developer pair ZyperOS is built with: a build without it says `not in this build` on that page. Without an account, or for a game ScreenScraper doesn't have, it uses [libretro's thumbnail collection](https://thumbnails.libretro.com), looking a game up by its file name and, for zipped games, by checksum.

For what neither has, like homebrew, hacks and ports, give it a free [SteamGridDB](https://www.steamgriddb.com) API key (steamgriddb.com, Preferences, API) in **MENU > Scraper**'s Settings, or paste the key on the File Transfer page, which beats typing 32 characters. A run then asks SteamGridDB last, for a portrait cover by the game's name. The key stays on the card's settings and shows only its last four characters.

Some games aren't in any collection, like fan translations. Add your own: a PNG named exactly like the game file, in `Roms/<console>/.media/`. `Black Castle.gb` wants `Black Castle.png`. Around 512 pixels on the long side is plenty.

Covers go over USB or Wi-Fi File Transfer too: drag a console's `.media` folder onto that console's folder on the page, and its covers land in the card's `.media` (Cmd-Shift-. shows hidden folders in Finder). Safari leaves hidden folders out of a folder it sends, so a whole console's folder sent from Safari arrives without its covers: send its `.media` folder on its own. The litter a Mac leaves, `.DS_Store` and `._` files, always stays behind.

Don't like a cover? Press X on the game and choose **Cover and Video**. The cover on the shelf is at the left and what libretro, ScreenScraper and SteamGridDB offer is at the right, each named (box front, 3D box, title screen, screenshot, fan cover) with where it comes from. A shows one big beside the one you have, and A again uses it. The cover it replaced stays as **Before**, and A on it puts it back. The same page shows the game's video snap: watch it with sound, download or replace it, or delete it.

<div align="center">
<table>
  <tr>
    <td align="center"><img src="res/readme/media.png" width="320" alt="A game's Cover and Video page: Now at the left, the pictures found at the right with the focused one named Box Front, USA, from ScreenScraper, and the game's video with Watch, Replace and Delete"><br><sub>What the sources offer, beside the cover you have</sub></td>
    <td align="center"><img src="res/readme/media-look.png" width="320" alt="Look Closer: one picture big beside the cover on the shelf, with Back and Use as Cover"><br><sub>A look before anything changes</sub></td>
  </tr>
</table>
</div>

With ScreenScraper linked, **MENU > Scraper** can fetch each game's video snap with its cover. Rest on a game for three seconds and its cover turns into the snap. They take room, a megabyte or three a game, so they are off to start: **X** on a console in Systems turns them on, **Y** there picks all games, the favorites or the games played, and the page says the room before a run and after it. The **Video Snaps** page in Settings plays them on the shelf or not, per console, sets their sound, muted to start, and deletes them all.

Auto Off waits while a fetch runs, so a library left to scrape overnight is not cut short. The power button and a flat battery still power the device off.

</details>

<details>
<summary><b>What is the screensaver?</b></summary>

<div align="center">
<img src="res/readme/screensaver.png" width="640" alt="The screensaver: Metal Gear Solid's video snap over the whole screen, its system and name at the foot, and START's hint, Play, at the top right">
</div>

**MENU > Screensaver** plays the video snaps of random games over the whole screen after a delay with nothing pressed on the shelf: `off`, 30 seconds, or 1, 2, 5 or 10 minutes, `1m` to start with. Each clip shows for 15 seconds with its game's name and system, then fades to the next.

- **START** boots the game on screen, as A on its card would. Any other button goes back to the shelf as it was; the volume and brightness buttons do their own work and leave it be.
- It never starts in a game or over a menu, MP4 or File Transfer, nor while music plays or a long job runs, nor under 20% on battery.
- **It never keeps the handheld on past Auto Off.** Auto Off powers it off at its own time with the screensaver up, and on the cable its time turns the screen off as it does for the shelf.
- It plays muted and draws a frame only when the picture changes. On battery it stops by itself after 10 minutes, when the chip gets hot, or when the battery falls under 20%, and waits for a button.
- With no video snaps on the card it shows the games' covers, 8 seconds each; with no covers either, it does nothing.

</details>

<details>
<summary><b>What fills my card?</b></summary>

**MENU > Storage**, just above System Update. The bar shows the card by part: games, saves and save states, art (covers and video snaps), media (music, videos, audiobooks), ZyperOS itself, and the rest. Under it, every system and folder by size; A on a system opens its page, with its games largest first. A on a game there asks Delete Game's question. A on Music, Videos or Audiobooks opens its albums, films and books by size, with your own menu music and the covers and playlists beside them; A on one asks before it goes, with the room it gives back. **X** changes the order of the list you are on: by size, A to Z, or last played (not on Music, Videos and Audiobooks, where nothing records what was played). Each list keeps its order. The first time, Zyper measures the card while you wait; after that the page opens at once and updates itself.

<div align="center">

<table>
  <tr>
    <td align="center"><img src="res/readme/storage.png" width="320" alt="MENU > Storage: 120.5 GB used and 7.4 GB free of 128 GB, a bar by part, two recommendations, then PlayStation, Videos and Sega CD by size"><br><sub>The card by part, then by size</sub></td>
    <td align="center"><img src="res/readme/storage-system.png" width="320" alt="PlayStation's page: 3.8 GB in six games, its bar of games, saves and art, Delete Video Snaps, then its games largest first"><br><sub>A system's games, largest first</sub></td>
    <td align="center"><img src="res/readme/storage-music.png" width="320" alt="Music's page: 5.2 GB in 318 songs, then its albums largest first, and X beside the list for the order"><br><sub>Music's albums; X changes the order</sub></td>
  </tr>
</table>

</div>

**Unused Art** deletes the covers and video snaps of games that are no longer on the card; a fetch in **MENU > Scraper** can bring them back. **Old Save States** deletes the save states of games none of whose states was written in a year and that you have not played in one. Your saves stay. Each asks first, with the room it gives back, and the cursor on Cancel.

</details>

<details>
<summary><b>How do I delete a game?</b></summary>

Press X on the game, choose **Delete Game** at the bottom of its details, and answer **Delete**. The cursor starts on **Cancel**, and B is Cancel too. It works from Favorites as well, and on Ports: a port goes with the folders its PortMaster `port.json` names, its saves in them, and PortMaster itself always stays.

<div align="center">

<table>
  <tr>
    <td align="center"><img src="res/readme/info-delete.png" width="320" alt="A game's details, the cursor on Delete Game"><br><sub>Delete Game, the last row of a game's details</sub></td>
    <td align="center"><img src="res/readme/delete-ask.png" width="320" alt="Delete Game's question: Delete Metal Gear Solid? Its saves and states, cover and video go too. This cannot be undone. Delete, and Cancel under the cursor"><br><sub>It asks first, on Cancel</sub></td>
  </tr>
</table>

</div>

What goes, and nothing else:

- **The game:** its file, or its folder for a game in a folder of its own (a multi-disc set with its `.m3u`, a ScummVM game). A folder of games it is kept in stays, with the other games in it. A `.cue`, `.gdi` or `.toc` at the top of the folder takes the track files it names, and a `.cue` its `.sbi`. An arcade set is its `.zip`, and on NAOMI and Atomiswave the folder of its name that holds its CHD.
- **Its saves:** `Saves/<game>.srm`, `.sav` and DraStic's `.dsv`, and the arcade cores' own, in `Saves/fbneo/`, `Saves/mame2003-plus/` and `Saves/reicast/`.
- **Its save states:** the six slots and the autosave, with their pictures.
- **Its cover and video snap** in `.media/`, and its cached achievement set.
- **Its details, title, favorite and play time** in the card's library.

A file that another game on the card still uses stays: a save two systems' games of the same name both write, a cover a `.cue` and a `.chd` of one name share, a `.bin` that a second `.cue` names. A BIOS set on a shelf, like `neogeo.zip`, is never deleted. Achievements you earned stay on your account. If a `.cue` or `.m3u` names a file outside the game's folder, nothing is deleted and the handheld says so. Ports are not deleted from here: a port's files are spread over its script, its folder and PortMaster's shared ones, so delete it with File Transfer.

</details>

<details>
<summary><b>How do achievements work?</b></summary>

Sign in under **MENU > Achievements** with your RetroAchievements account. A game's first start needs Wi-Fi, to identify the game and fetch its achievements. From then on it plays offline too: what you unlock without Wi-Fi shows at once, with its notice and Zyper, and waits on the card. When the handheld is on Wi-Fi again, on the shelf, the waiting unlocks go to your account by themselves, and Zyper says how many went. Each one is dated when you earned it, up to 14 days back, the most RetroAchievements takes.

While some wait, the game's list says how many under its name, and an achievement opened with A says "earned, not sent yet". A game you have never played with Wi-Fi says "not checked yet" on its Achievements row, and A on the row says how it gets its achievements. Offline unlocks are softcore, the only mode ZyperOS plays.

<img src="res/readme/cheevos-waiting.png" width="400" alt="A game's achievements list, its heading saying '3 / 36 · 2 waiting to send': two achievements earned offline, not sent yet">

<img src="res/readme/cheevo-notice.png" width="400" alt="The notice over a game when an achievement unlocks: its badge at the left, 'Unlocked · 10 points' over its name, and Zyper at the right">

An unlock shows its badge, its name and its points for a few seconds, with Zyper at the right. Each achievement's badge is a small picture (64 pixels) from RetroAchievements, kept on the card beside the game's achievements. It is fetched when the handheld is online and the game is not running, never during a game, so no frame waits on the network. Until a badge is on the card, in the list and in the pop-up a trophy takes its place, and it changes to the badge by itself. A handheld that is never online can get them from a computer: `tools/ra-sets.py` puts each game's achievements and badges on the card.

</details>

<details>
<summary><b>Can I use Bluetooth headphones?</b></summary>

Yes, on the Brick. Put them in pairing mode, open **MENU > Bluetooth**, press **Y** to search and **A** to pair. They reconnect by themselves from then on. Wired headphones always win when they're plugged in.

The Brick's volume buttons set the headphones' volume, and the headphones' own volume buttons move the Brick's. On a few headphones, the Brick's buttons set only the starting volume; after that, change it on the headphones themselves. Play, pause and skip on the headphones control MP4.

The Pixel 2 has no Bluetooth: use wired headphones, or a USB-C headphone adapter.

</details>

<details>
<summary><b>How do I add music?</b></summary>

Put it in `Music/` on the card, from a computer, over Wi-Fi with Wi-Fi File Transfer, or over the USB cable with USB File Transfer, a folder per artist and a folder inside that per album: `Music/Radiohead/The Bends/01 Planet Telex.mp3`. An album folder put straight into `Music/` is moved under its artist when its tags name one, and songs in no folder at all are moved into `Music/Singles/`, an album of their own. MP3, M4A, M4B, MP4, AAC, FLAC, Ogg, Opus and WAV all play. MP4 appears on the shelf once there's music, and each album's cover comes from its own files, or a picture in the album's folder. **MENU > Album Art** on MP4's shelf fetches a sharper cover over Wi-Fi from [MusicBrainz](https://musicbrainz.org)'s Cover Art Archive for any album whose files carry none, or only a small one.

</details>

<details>
<summary><b>How do I add audiobooks?</b></summary>

Put each book in its own folder in `Audiobooks/`, with an author's folder above it or without: `Audiobooks/Iain M. Banks/The Hydrogen Sonata/` or `Audiobooks/The Hydrogen Sonata/`. One M4B or a folder of MP3s both work, and the files play in name order. **MENU > Show** on MP4's shelf starts on All, your music, books and videos together, and switches to one of them alone. A book remembers where you stopped, even through a power-off, and one played to the end is marked finished; A on it starts it over.

</details>

<details>
<summary><b>How do I add videos?</b></summary>

Put them in `Videos/` on the card. A film on its own goes straight in, `Videos/Where the Light Is.mp4`, and is a card of its own; a folder of videos is a series, its files in name order, with seasons as folders inside it if you like: `Videos/Planet Earth/01 From Pole to Pole.mp4`. A picture named like the film, `Where the Light Is.jpg`, is its cover; otherwise MP4 takes one from the video itself. Every video remembers where you stopped.

The Pixel 2 has no hardware video decoder that its Linux can use, so its four cores decode in software. **H.264 in an .mp4 plays smoothly up to 720p**; 854x480 or 640x360 is the size to make for its 640x480 screen, and costs a fraction of the battery. HEVC and VP9 play up to about 480p. 1080p only just keeps up, dropping frames when the picture moves a lot. AV1 does not play. MKV, WebM, MOV and AVI files play too, if the video inside them is one of those.

**Y** turns a video between full screen, where the controls show only when you press a button, and a view with its controls always on screen.

</details>

<details>
<summary><b>Where are my saves?</b></summary>

Battery saves are `.srm` files in `Saves/`, named after the game. Save states, the autosave included, are in `.userdata/shared/.zyperos/`, one folder per console. Neo Geo Pocket Color games write no `.srm`, so their progress lives in the autosave.

</details>

<details>
<summary><b>Something went wrong. How do I send the logs?</b></summary>

Open **MENU > Wi-Fi File Transfer** (**USB File Transfer** over the USB cable), open its address in a browser, and click **Download logs** under the folders (behind the ⋯ on a phone). You get one `.tar.gz` file holding the last ten boots' logs, on the Pixel 2 the USB port's last 50 plugs (`usb.log`: what was plugged in, what the handheld asked and did, and how long it stayed) and the Wi-Fi adapter's joins and drops, each drop with the reason the network or the adapter gave (`wifi.log`), and a short summary of the device, ready to send to whoever is helping you. Wi-Fi network, headset and account names are masked in it, and the logs on the card are left as they are. The logs themselves are on the card, in `.userdata/brick/logs/` on the Brick and `.userdata/pixel2/logs/` on the Pixel 2.

</details>

## For developers

ZyperOS is C and SDL2, two repositories that build together:

- [ZyperOS](https://github.com/tarikbc/ZyperOS) (this one): the launcher, the card's scripts and the tools, in [`diatom/`](diatom/) the libretro frontend that starts once at boot and stays running (its own Makefile, under the Mozilla Public License 2.0), and in [`installer/`](installer/) ZyperOS Installer, which puts ZyperOS on a card from macOS, Windows or Linux (Rust and Tauri)
- [`plastron/`](plastron/): the Linux under it, built with Buildroot in Docker (its own license, the GNU GPL version 3, in [`plastron/LICENSE`](plastron/LICENSE))

[Building](docs/building.md) covers the toolchain, the card and the checks. With a development build on the card and the Pixel 2 on the USB cable, `make -C plastron push` writes a new system to the device, and `tools/px2-deploy.sh` puts a new ZyperOS on it, without taking the card out. On a development image, **MENU > Developer Mode** (on to start) keeps the USB link to a computer up with no question, for the image's own SSH at `10.42.0.1`. A release image has [developer mode](#developer-mode) instead, which you turn on yourself.

### Developer mode

A release image starts no SSH and holds no key, so a shell on it is yours to ask for. Developer mode is how, and it stays out of the way until you look for it. On **MENU > About**, on the first page (the one with the version), hold **L2 and R2 together for five seconds**. A **Developer Mode** row appears with a bar that fills as the seconds pass, and letting go of either button, or leaving the page, starts again. When it is full, a message says **Developer Mode is on**, and the row stays on the first page as *on*. **MENU > Developer Mode** then opens its page, where **A** turns developer mode off again.

With it on, the Pixel 2 behaves like a development build for as long as a computer is on the cable. The USB link stays up with no question, and SSH runs on the cable's address, `ssh root@zyperos.local` or `ssh root@10.42.0.1`, never over Wi-Fi. Closing a page or starting a game does not end it, and when the launcher restarts (a `tools/px2-deploy.sh` run does that) the sessions drop and SSH is back a few seconds later, so a key logs in again. Pulling the cable out, or turning developer mode off, ends it. There are two ways in, and both are yours:

- **A key from the card.** Copy your public key, `id_ed25519.pub` or any `.pub` file (or an `authorized_keys` file), into the `Developer` folder at the card's root; the Pixel makes the folder the first time. Plain public keys only: a line with options in front, such as a forced command, is skipped. A key taken is kept with the ones `ssh-copy-id` saves. The folder is outside `ZyperOS/` because an update swaps that one for the new version's.
- **A password for this plug.** The Pixel makes eight random characters each time a computer is plugged in. Only **MENU > Developer Mode** shows them, and nothing keeps them. Five wrong ones lock SSH until the next plug, as File Transfer's PIN does. This is also how the first key gets in without taking the card out: sign in with the password, then run `ssh-copy-id root@10.42.0.1`.

**MENU > Developer Mode** opens the page: the command, the password, where the keys go, how many it read, and SSH's status. **A** there turns developer mode off; **B** closes the page and leaves everything running. Nothing is shipped in the image: no password, no key. A development image is unchanged: its SSH starts at boot with the key in `plastron/local/authorized_keys`, and `tools/px2-deploy.sh` works as before.

[Releasing](docs/releases/README.md): `make release PUBLISH=1` on the Mac builds the card image and makes the draft release; the tag it pushes has GitHub build ZyperOS Installer for macOS, Windows and Linux, add it to the same release and publish it. Every version's release has the image and the installer together. A release also carries System Update's pack and its list, signed with ZyperOS's release key (`tools/update-key.sh`) ([how it works](docs/updates.md)).

Without the device: ZyperOS runs on a Mac in [QEMU](docs/qemu.md) (`tools/qemu-run.sh`, plastron's `make qemu`), and the [simulator](docs/simulator.md) shows it as a Pixel 2 in a window, its buttons, battery, cable and side lights included. Speed, the GPU and the hardware itself still need the device.

## Credits

ZyperOS stands on other people's work:

- **Eric Reinsmidt**, for [TortOS](https://github.com/ericreinsmidt/TortOS), [TortOS Installer](https://github.com/ericreinsmidt/TortOS-Installer), [plastron](https://github.com/ericreinsmidt/plastron) and [Diatom](https://github.com/ericreinsmidt/diatom), where ZyperOS's launcher, installer, Linux and emulator started. His code in ZyperOS keeps his copyright and notices.
- **[ROCKNIX](https://github.com/ROCKNIX/distribution)**, for the RK3326 kernel patches, the Pixel 2 device tree, the U-Boot setup and the DDR clock driver.
- **[PortMaster](https://portmaster.games)**, for the ports and the runtimes they run on.
- **[plorpOS](https://github.com/thwonp/plorpOS)** by Thwonp, for the DSperate libretro wrapper and the FAKE-08 patches.
- **congusbongus** (Napping on a Cloud), **Zane Little Music** (Apple Cider, Hush Hamlet), **HydroGene** (Truth), **Spring Spring** (Scheatt), **TAD** (Ice Cave), **John Oestmann** (Starfall Island), **nene** (Leap), **LordZintick** (The Arplands) and **Joth** (Bossa Nova), whose free 8-bit tracks are the menu music a fresh card plays, all given away under CC0 on [OpenGameArt](https://opengameart.org) ([THIRD-PARTY-LICENSES.md](THIRD-PARTY-LICENSES.md)).
- **The libretro core authors**, ScummVM's, YabaSanshiro's and FreeJ2ME-Plus's among them, whose emulators do the real work, each under its own license ([THIRD-PARTY-LICENSES.md](THIRD-PARTY-LICENSES.md)).
- **[OpenBOR](https://github.com/DCurrent/openbor)**, by Senile Team and the OpenBOR Team, which runs the OpenBOR shelf.
- **Eclipse Temurin** (OpenJDK) and **DejaVu Sans**, the Java runtime and the font J2ME games run with.
- **minimp3** by lieff and **stb_vorbis** by Sean Barrett, which decode the menu music.
- **zadpos**, for [Sharp-Shimmerless](https://github.com/Woohyun-Kang/Sharp-Shimmerless-Shader) and its scanlines and grid (CC0), and **Timothy Lottes**, for his CRT shader, CRTS (public domain), which the Shader row draws with.
- **Rockchip**, for the firmware that starts the Pixel 2 and runs its memory at full speed.

## License

ZyperOS is open source, under the [GNU General Public License, version 3](LICENSE) (GPL-3.0-only), so anyone can build on it. The [NOTICE](NOTICE) adds two terms that section 7 of the GPL allows. The first keeps the name ZyperOS, ZyperOS Installer, Zyper (the miniature schnauzer the system is named after) and his art, the logo and the website as trademarks, so a fork needs its own name and mascot; naming ZyperOS to say where your work came from is always fine. The second asks you to keep the copyright notice, the NOTICE and the attribution in the About screen when you share it. [TRADEMARK.md](TRADEMARK.md) says in plain words what is and is not fine. Versions published before 2026-11-06 were under the GNU General Public License, version 3 or later, and for a short time after that ZyperOS was under the PolyForm Shield License 1.0.0; this is GPL-3.0-only. Eric Reinsmidt's TortOS code in ZyperOS is from TortOS's MIT-licensed versions and keeps his MIT notice ([LICENSES/](LICENSES/)). plastron stays under the GNU GPL version 3 and Diatom under the Mozilla Public License 2.0. The cores keep their own licenses, some of them non-commercial. OpenBOR, a program of its own on the card, is under the BSD 3-Clause License. Flycast 2.7 is a separate program, not part of ZyperOS. Full notices are in [THIRD-PARTY-LICENSES.md](THIRD-PARTY-LICENSES.md).
