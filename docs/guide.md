# ZyperOS guide

The [README](../README.md) keeps each topic short. This is the long version:
every control, menu row and setting, and the reasons behind them.

## Installing

Each device's card is a disk image written over the whole card, as the
[README](../README.md#install) says, with [ZyperOS Installer](../installer/)
or any program that writes disk images. Every release is on
[GitHub](https://github.com/tarikbc/ZyperOS/releases/latest), from 1.0.0 on:
the GKD Pixel 2's image is there, and the TrimUI Brick's comes later.

The image is [plastron](../plastron/), the small
Linux both devices run ZyperOS on: a boot partition and the system itself,
read-only, and on the Brick also a small partition for system data such as
Bluetooth pairings. At the first boot, in about a minute, the rest of the
card becomes ZYPEROS, an exFAT partition, with ZyperOS on it. Games, music,
saves and settings live there, and it is the partition a computer sees.

The device itself is never written to. The Brick boots from the card before
its own system, so **removing the card brings the stock system back**;
[Undoing it](#undoing-it) covers the rest.

Copying from macOS leaves `._name` metadata files beside everything. They are
harmless - the launcher skips every name beginning with a dot, so they never
show up as games - and `dot_clean /Volumes/ZYPEROS` removes them if they
bother you.

Building it yourself instead is in [building.md](building.md).

BIOS files go **loose in `Bios/`**, not in a folder per system - that directory
is handed to the core as its system directory, and a core asks for a filename
inside it:

```
Bios/syscard3.pce             TurboGrafx-16 CD games (.chd, .cue, .m3u ...)
```

That one is required: a PC Engine CD will not start without it, and ZyperOS says
so by name rather than letting the core refuse the disc. HuCards on the same
shelf need nothing and are unaffected.

Nothing else on the shelf needs a BIOS. mGBA has a built-in one, so Game Boy
Advance runs without the real thing; if you want the original it is
`Bios/gba_bios.bin`, and the difference is the boot animation.

The consoles on the shelf come from `systems.cfg`, so the list in the README is
the shipped one rather than a fixed one: a line removed from the config takes
its shelf with it, and a system whose folder is empty is hidden until there is
something in it.

### Undoing it

On the Brick, remove the card: ZyperOS never writes to the device, so the
Brick's own system starts as before. On the Pixel 2 the system lives on the
card, so a card with another system takes its place.

ZyperOS keeps everything on the card: battery saves in `Saves/`; settings,
states and achievements in `.userdata/`; Wi-Fi networks and their passwords
in `.userdata/brick/wifi/` (`pixel2` on the Pixel 2); and on the Brick, the
Bluetooth pairings in its small system partition. Reformat the card and all
of it goes, with nothing kept anywhere else, so wipe the card before you give
it away.

## Controls and menus

| | |
|---|---|
| **Left/Right** | move left or right along the shelf |
| **Up/Down** | jump to the previous or next initial on the games shelf |
| **L1/R1** | jump three systems on the systems row, a screenful on a games shelf, coming round at the ends |
| **L2/R2** | the previous / next song on the systems row and a games shelf: MP4's while it plays (a book's chapter, as L1/R1 in MP4), else the menu music's. Zyper pops up with its name |
| **A** | open a system or start a game |
| **B** | back to the systems row |
| **X** | game info for the card under the cursor |
| **Y** | favorite it - Favorites is a shelf of its own. Nothing on MP4's shelf |
| **Volume rocker** | volume, everywhere, including in game |
| **F1/F2** | brightness, everywhere, including in game |
| **MENU** (on the systems row) | the ZyperOS menu - settings that are about the firmware |
| **MENU** (inside a system) | that system's menu, below |
| **MENU** (in game) | the in-game menu: Continue, Save, Load, Quick Slot, Display, Controls, Achievements, Reset, Quit, and Pak in a Nintendo 64 game. It opens when you let go, so MENU can be held for brightness |
| **SELECT+R1** (in game) | quick save: a state into the Quick Slot |
| **SELECT+L1** (in game) | quick load: the state in the Quick Slot |
| **SELECT+R2** (in game) | faster: 1.5x, 2x, 3x |
| **SELECT+L2** (in game) | slower, a step a press, back to normal speed |
| **MENU** (inside any menu) | closes the whole menu, however deep in it you are. B goes back one screen |
| **SELECT** | MP4, the music, audiobook and video player, from anywhere but a running game - the in-game menu included. Again to close it |
| **POWER** | powers off. In a game, the game is saved first |

**On the Pixel 2:**

| | |
|---|---|
| **MENU** (the FUNCTION key) | opens the menu when it is let go, and works as a modifier when held |
| **MENU+VOL** | brighter / dimmer (VOL+ / VOL-), everywhere, including in game: the Pixel 2 has no brightness keys of its own |
| **L2+R2** (outside a game) | flash the battery level on the side lights, below. L2 or R2 alone is the menu music's song, and acts as it is let go, so the pair never changes the song |

**The Pixel 2's four side lights** show the battery, and a few things worth a
glance:

| | |
|---|---|
| **charging** | a bar fills up from the bottom, starting from a light for each full quarter it already has: at 50% the bottom two stay lit and the other two fill in. A full battery holds all four, still, and so does a battery held at the **Charge Limit**, at its level |
| **below 10%**, on the battery | the bottom light flashes red every five seconds; below 5%, twice every two seconds |
| **volume or brightness** changing | the level, in eight steps: each light is two, and a blinking light is a half. Volume at 0 is the bottom light red |
| **files or covers done** | a light climbs the four twice when a File Transfer batch is in, or the Scraper finishes |
| **an achievement** | a sparkle: two pairs of lights take turns |
| **L2+R2** (outside a game) | the level, flashed four times: all four lights at 75% and up, three at 50, two at 25, one at 10, the bottom light red below that |
| **the rest of the time** | dark |

MENU > Side Lights turns each of the first five on or off. The red flash shows
through everything else; L2+R2 flashes the level whatever is chosen. The
lights are on or off, with nothing between, so there is no brightness to set.

MENU means three different menus depending on where you are, and each one is
about the thing you are looking at: the firmware on the systems row, one
console inside it, the running game in a game. Inside any of them MENU closes
the whole menu at once - from Play Time or Wi-Fi straight back to where you
were, or back into the game - and B goes back one screen at a time.

**MENU > Controls** lists the buttons on five pages.
Left and right change the page, and the top right of each says which it is, "2 / 5". The pages cover movement, the shelf, games, MP4, and common controls.
The shelf always runs horizontally.

**Controls in the in-game menu** lists what each button does in the game you
are playing, on its system. The first pages are the system's own buttons, as
its emulator reads them: on a Genesis the A button is C, and on a Neo Geo
Pocket X is a turbo B. The turbo buttons are there too, and a PlayStation
game's face buttons are drawn as the pad's cross, circle, triangle and square.
A Nintendo 64 game has a page for its C buttons (hold SELECT and press X, A, B
or Y) and, under them, which Pak its controller holds and where to change it:
"MENU > Pak: Rumble Pak". A PICO-8 game lists its
arrows, O and X and its pause menu. A DS game has a page for its two screens
and the stylus. Then Hotkeys, SELECT with a shoulder: quick save and load, and
faster and slower. The last page, In a game, is MENU, volume, brightness and
POWER. Left and right change the page, B goes back to the menu, and MENU goes
back to the game.

**OpenBOR has the in-game menu too**, though it runs as a program of its
own rather than a libretro core. MENU stops the game where it is and opens
the menu over it with three rows: Continue, Controls and Quit. Controls lists
OpenBOR's buttons (A attacks, B jumps, X is the second attack, Y the special,
L1 and R1 the third and fourth, START pauses into OpenBOR's own menu, SELECT
saves a screenshot) and then In a game, where POWER quits and turns the
handheld off. OpenBOR keeps its own saves and has no save states, so there is
no Save, Load or Reset. Quit ends the game and goes back to the shelf.
The standalone Flycast, when a card has it for the Dreamcast, gets the same
three rows. Its buttons are set in its own menus, so Controls there has only
the In a game page.

**About ZyperOS has two pages**, changed with Left/Right. The first shows the
version, network address, battery charge and awake time. The second shows the
card's used, available and total storage in decimal GB, then estimated battery
health, full-charge capacity and design capacity in mAh. Health is the battery
controller's full-charge capacity divided by its design capacity, rounded and
capped at 100%. It is an estimate, not an independent battery-wear measurement.
A device that does not report the capacities shows "unknown" instead. The page
refreshes its storage and capacity readings every two seconds; the charge state
uses the status corner's shared fast cache.

**The first time ZyperOS starts on a handheld it teaches the buttons**: Zyper
says hello, and then each button is a step of its own - the D-pad, A, B, Y, X,
MENU, SELECT, the volume, the brightness and POWER - where a small picture
shows what it does and the step waits for you to press it. The shoulder keys
come with two of them, lit on the picture of the handheld: L1/R1 jump with
the D-pad, and L2/R2 change the song with SELECT. A press of another
button gets Zyper's answer, which button that was and which to try. POWER
does not turn the handheld off in its own step, so it can be tried, and the
volume and brightness keys change the real levels. It takes about a minute,
START skips it at any point, and **MENU > Show Tutorial** runs it again. A
start straight back into a game skips it until the next start that comes to
the shelf, and a handheld turned off before the power step starts it again.
A handheld updated from a ZyperOS that had no tutorial shows it once too.

The ZyperOS menu opens from the systems row and is about the device rather
than any one console:

| | |
|---|---|
| **Play Time** | per game or per system, by day, week, month, year or all time |
| **Wi-Fi** | the network's name when connected, or why it is not |
| **Bluetooth** | the connected headset, or `not connected` - pair, connect and forget |
| **Audio Output** | `Auto` or `Speaker`, and where Auto landed - see **Audio** below |
| **Audio Channels** | `stereo`, `mono`, `mono left` or `mono right` - see **Audio** below |
| **Wi-Fi File Transfer** | the file server; needs Wi-Fi and says so when there is none. On the Pixel 2 with no network it says `hotspot`, and makes the handheld its own network for a phone to join - see below. While a computer is on the USB cable it is **USB File Transfer** instead, always at `10.55.0.2` on the Brick and `10.42.0.1` on the Pixel 2 |
| **Date & Time** | the clock and the time zone, set with left and right - see below |
| **Clock** | `12-hour` or `24-hour`, for the clock on the shelf and here. `12-hour` on a new device |
| **Auto Off** | how long without a button before the device powers itself down. On the charger it does not power down; on the shelf the screen goes off instead, and any button (power too) lights it again |
| **Side Lights** | the Pixel 2 only: a page of switches for what the side lights show on their own, `Charging Bar`, `Low Battery`, `Volume and Brightness`, `Files and Covers Done` and `Achievements`, each `On` or `Off`; the row says how many are on. L2+R2 shows the level in all four. `All on` on a new device |
| **System Order** | the home's systems `A to Z` by name, on a new device and after an upgrade, or by `Release Date`. Favorites stays first, and Ports and MP4 stay last |
| **Appearance** | `Light`, `Dark` or `Auto`, which is light from 7:00 to 19:00 by the clock. `Light` on a new device and after an upgrade |
| **Scraper** | box art, its sources and video snaps - see below. Its value is how many games have no cover, or `complete` |
| **Achievements** | the RetroAchievements account, or `sign in` |
| **Controls** | every button and what it does, a page per place |
| **Show Tutorial** | the tutorial a new handheld starts with, again - see below |
| **Developer Mode** | development images on the Pixel 2 only: `on` keeps the USB link to a computer always up, `off` asks when one is plugged in - see **Plugging into a computer** |
| **About ZyperOS** | two pages: version, address, charge and uptime; then storage and estimated battery health |

The Pixel 2 has no radio, so the rows that need Wi-Fi show only while a
USB Wi-Fi adapter is plugged in: Wi-Fi, Achievements and Wi-Fi
File Transfer here (USB File Transfer stays, with a computer on the cable), Box
Art in a console's menu, and Album Art in MP4's. Scraper stays: its fetch says
it needs Wi-Fi, and its video snaps and sources need none. Bluetooth is never
there.

### Scraper

MENU > **Scraper** is box art in one place: one list, Fetch Missing Art and
Video Snaps, then the three sources in the order a fetch asks them.

| Row | What it is |
|---|---|
| **Fetch Missing Art** | fetches every missing cover over Wi-Fi, in this same panel: the row becomes its progress (`31 / 142`) and each source counts the covers it found. B stops it. A console's own menu keeps its Box Art row, the same fetch for that console alone |
| **Video Snaps** | `on` or `off` and the room they take; opens their page, below |
| **ScreenScraper** | `linked as` its account, `link`, or `not in this build`. Its page links the account (user, then password, checked once), says how many requests are left today, and unlinks it. It brings covers with each game's year, genre and synopsis, and the video snaps. A build needs ScreenScraper's developer pair for any of it |
| **libretro** | needs no account: covers by file name, and for zipped games by checksum |
| **SteamGridDB** | the API key as its last four characters (`••••1a2b`), `key refused`, or `link`. Its page types a key in, checks it once, and unlinks it; the File Transfer page takes one pasted. Covers the other two do not have |

**Video Snaps** are a few seconds of a game's own screen, from ScreenScraper.
After three seconds on a game, its cover turns wide into its snap, which loops;
it goes back to the cover when the focus moves or a menu opens. Its page:

| Row | What it is |
|---|---|
| **Fetch with Box Art** | `on` fetches each game's snap with its cover, and for games that already have a cover. `off` to start; needs ScreenScraper |
| **Play on the Shelf** | `on` to start |
| **Sound** | `muted` (to start), `quiet` or `full`. Never over music playing in MP4; Menu Music dips under it |
| each console | `on` or `off` for that console, and the room its snaps take |
| **Delete All** | deletes every snap, after a question |

A snap is `Roms/<console>/.media/<name>.mp4`, beside its cover, so one put
there by hand plays too.

Date & Time has a row for each part of the clock and one for the time zone,
each changed with left and right and set the moment it changes, so the time
holds through a power-off. The zones are one or two cities per hour of offset,
west to east. Each time Wi-Fi connects it sets the clock, so with Wi-Fi this
screen is mainly for the zone. A Pixel 2 with no adapter cannot get the time
from a network: set it here once.

The shelf shows the time in its top right corner, with Wi-Fi and the battery
beside it, and a Bluetooth headset on the Brick while one is connected. Wi-Fi's fan is crossed
while the radio is on and not connected, and gone when it is off. The battery
turns red at 10%, its fill blinks at 5%, and it shows a bolt while it charges.
The clock is not shown until it is set to 2026 or later, so a Pixel 2 that was
never set does not show a time that is not now. The same corner is on Now
Playing and over the menu of a paused game.

Wi-Fi File Transfer and Scraper's fetch need a network, and go quiet without one rather than
disappearing - a row that vanishes teaches nobody why. Achievements stays reachable
either way, because signing in is the thing you go there to do, and Bluetooth
and Play Time need no network at all.

### Wi-Fi File Transfer with no network

On the Pixel 2 with a Wi-Fi adapter in and no network joined - Wi-Fi off,
or on with nothing in range - Wi-Fi File Transfer makes the handheld its own
Wi-Fi network for as long as the screen is open. The row says `hotspot`.

1. Open **Wi-Fi File Transfer**. The screen shows a QR code, the network's name
   (`ZyperOS-` and four characters of the adapter's address), its password and
   the PIN. **B** closes it.
2. Point the phone's camera at the code and join the network it offers. A
   laptop joins by the name and the password instead.
3. The phone opens the transfer page by itself, in its sign-in sheet, at
   `10.42.1.1`. Type the PIN.

The password and the PIN are new every time the screen opens. Close the
screen and the network goes, and Wi-Fi comes back as it was: on and joining
your saved networks if it was on, off if it was off.

The sign-in sheet is a small browser and cannot do everything. On an iPhone
it uploads but cannot save a download; on Android it downloads but cannot
pick a file to upload. The page says so at the top, and how to open it in the
real browser instead: on an iPhone, tap **Cancel**, then **Use Without
Internet**, and open `10.42.1.1` in Safari; on Android, tap the menu, then **Use
this network as is**, and open `10.42.1.1` in Chrome. The phone has no
internet on this network; it comes back when the phone leaves it.

The adapter needs a driver that can be an access point. The TL-WN725N v2/v3
(RTL8188EUS) can, with Realtek's own driver, which plastron gives it. An
adapter that cannot shows **Could not start the hotspot.**

On the Play Time screen, **left and right** change the window - all time, this
year, this month, this week, today - and **Y** switches between one row per
game and one per system. The row under the cursor gets its own line at the
bottom: how many times it has been launched, its longest single session, when
it was last played, and how many of those sessions ended in a flat battery or
a crash rather than a quit. That last number only appears when it is not zero.

On the Wi-Fi screen, **Y** rescans and **X** forgets the network under the
cursor, behind a confirm. Forgetting the one you are connected through is
allowed - refusing would leave a row that is visibly saved and visibly
un-forgettable, which is worse to explain than the consequence.

The system menu opens on a shelf of games and applies to that system alone:

| | |
|---|---|
| **Games** | how many the shelf found |
| **Core** | which core runs them; left/right/A choose on a shelf that has two (the Dreamcast's) |
| **Game Language** | the Nintendo DS shelf's: left/right/A go round `Auto`, English, Español, Français, Deutsch, Italiano, 日本語 and 简体中文, saved the moment it changes. `Auto` is the language of MENU > Language where the DS has it and English where it does not, and the row says which. The next game started speaks it, whatever the language of the `firmware.bin` in `Bios/`. A game whose resume point was left in another language starts fresh in this one, with a note that says so, its battery save kept; a save state you load keeps its own language until Reset |
| **Sort By** | left/right; `Name`, `Play Time`, `Last Played` or `Recently Added` |
| **Display Mode** | left/right cycles it; saved the moment it changes |
| **Box Art** | fetch what this system is missing, and nothing else |
| **Rescan Folder** | read the card again, for ROMs that arrived since boot |

On Favorites the menu is only Games and Sort By. The rest is about one
console, and a game there takes its console's Display Mode wherever it was
started from.

Display Mode is per-system, because a Game Boy and a Genesis do not want the
same answer. Set here it applies from the next launch; the same row in the
in-game menu changes the running game as you press it, which is the one to use
when you want to see the difference rather than guess at it. Either way it is
written the moment it changes - there is no confirm step to hang the save off.

Sort By is per-system for the same reason. Alphabetical is the only sane way
to find a title you can name, and no help at all when you are coming back to
the two or three you are actually playing, which on a large shelf are
scattered through the alphabet. Play Time and Last Played read the same
session rows the Play Time screen does; Recently Added is the ROM's timestamp
on the card. Whichever order you pick, games it knows nothing about sort to
the bottom rather than into the middle, and two games it cannot tell apart
fall back to name - a shelf that reshuffles itself when nothing has changed
reads as broken even when the top of it is right.

Rescan Folder is what makes a ROM that arrived after boot appear without a
restart. Wi-Fi File Transfer already does it for you on the way out of the transfer
screen; this is the same thing by hand, for a card written some other way.

**In a game, X and Y are turbo A and turbo B** - hold one down and it presses
the button repeatedly for you instead of you mashing it. It applies to a whole
system rather than to one game.

Nine of the eleven have it: **NES, Master System, TurboGrafx-16, Game Boy, Game
Boy Color, Game Boy Advance, Game Gear, Neo Geo Pocket and Neo Geo Pocket
Color**. Those consoles had two face buttons,
so X and Y are spare and turbo can have them. Genesis and SNES are left out
because their pads use X and Y for real buttons.

Diatom does the pulsing, not the emulator core, which is why it works the same
on all nine rather than only on the one core that happens to implement turbo.

**On Game Boy Advance, L2 and R2 are turbo L and turbo R.** That one is the
emulator's own turbo rather than ZyperOS's, so it is only there on Game Boy
Advance; on Game Boy and Game Boy Color, which have no L and R, they do nothing.

**Faster and slower** are SELECT+R2 and SELECT+L2 in a game. Each press of R2
steps the speed to 1.5x, 2x and 3x, each press of L2 back down to normal, and
the step shows at the bottom of the screen. The game is silent while it's
fast, and the picture moves in steps rather than smoothly, because the
handheld draws every other frame to give the game more time. Each game starts
at normal speed, and the speed stays where you left it through the in-game
menu. MENU+R1 was fast forward until 1.0.2.

The speed is as fast as the game can go, up to the step. NES games reach it,
and Genesis comes close. A heavy SNES or Game Boy Advance game tops out at
about 2.5x, so there 3x means as fast as that game can go.

**Quick save and quick load** are SELECT+R1 and SELECT+L1 in a game: a state
saved into the in-game menu's **Quick Slot**, and loaded back from it, with a
pop-up that names the slot. The game keeps running while the state is
written. Quick Slot is one of the six slots of Save and Load, slot 1 until you
pick another, and each system remembers its own. A system whose emulator keeps
no states (ScummVM, J2ME) has no Quick Slot, and there R1 and L1 under SELECT
are the game's.

**The Nintendo 64's Pak.** An N64 game's controller holds the Rumble Pak, so
the Pixel 2 shakes. A game that saves to the Controller Pak, the N64's memory
card (Mario Kart 64's ghosts, and games that save nowhere else), takes it in
the in-game menu's **Pak** row: left, right or A turns it, at once, and the
game keeps it. The Controller Pak's saves are kept in the game's own save
file in `Saves/`, whichever Pak is in.

**SELECT stays a game button.** Most systems have a SELECT of their own, so a
press of SELECT is either the game's or the hotkeys', decided by what comes
next. SELECT and then a shoulder (R1, L1, R2, L2) is a hotkey, and the game
never sees that SELECT. SELECT and then any other button goes to the game at
once, with that button, so A+B+START+SELECT still resets a game. SELECT
pressed alone reaches the game as you let go of it, for as long as you held
it, and SELECT held alone for longer than 0.4 seconds is the game's, held.

Volume and brightness show the same bar at the top of the screen in the
launcher, in a game, in the in-game menu and over a port: a pill with a mark
at its left and a segment for each step. The mark says which of the two it
is: a speaker with one to three waves, and a cross at silence, for volume, and
a sun whose rays grow for brightness. The mark and the segments are lime for
volume and amber for brightness, on a bar in the theme that is on, light or
dark. Over a game on the Brick, Diatom still draws the old bar: a thin line
across the top, warm for brightness and cyan for volume.

## File transfer

**Wi-Fi File Transfer** (MENU, with Wi-Fi) and **USB File Transfer** (the same
row while a computer is on the USB cable) put the card's folders in a web
browser: ROMs, Music, Audiobooks, Videos, BIOS, Saves and Save States. The screen
shows the address to open and a four-digit PIN, new each time it opens; the
page asks for the PIN and opens as the fourth digit goes in. Five wrong PINs
and the page waits 30 seconds.

On a computer the folders are down the left, the open one in lime, and
the uploads go on under them. On a phone the folders are a row of chips over
the list, **Upload** is a button at the bottom right, and New folder, Upload
folder, Tidy names, the covers and the logs are behind the **⋯** at the top.
Every file and folder has a **⋯** of its own (a right-click on a computer
does the same) for Download, Rename and Delete; a delete always asks first.
Drop files anywhere on the page to put them in the folder that is open, or
onto a folder to put them there; a dropped folder keeps its folders. The page
wears the light or dark theme the phone or computer is in.

Leaving the screen rescans the shelf, so games that arrived are on it.

A card with no games on it opens on the two ways to add them, side by side:
**A** starts USB File Transfer and **Y** Wi-Fi File Transfer. On the Pixel 2
the cable and the Wi-Fi adapter use the same port, so the way that cannot
start now is dimmed and says why, and lights up as you plug in or unplug.
When the first games arrive, by either screen or any other way, the home
shows them, and Zyper cheers.

### SSH over the cable

While **USB File Transfer** is open on the Pixel 2, the computer on the
cable can also open a shell on the handheld:

```
ssh root@10.42.0.1
```

The password is the PIN on the screen. To skip it next time, give the
handheld your key once, with the PIN:

```
ssh-copy-id root@10.42.0.1
```

The key stays on the card, in `.userdata/pixel2/ssh/`, with the handheld's
own host key, so the computer knows it the next time. The screen's **SSH**
row says `waiting`, or how many are signed in. SSH only listens on the
cable's address, never on Wi-Fi, and only while the screen is open: closing
it ends every session. Five wrong PINs, on the page and in SSH together,
turn SSH off (`locked`) until the screen opens again, and end any session
that is open. It does not start while a Wi-Fi adapter is in (`not with
Wi-Fi`), which on the Pixel 2 cannot happen with the cable in its one port. Copying files with `scp` needs its `-O` flag, since the
handheld has no SFTP server: `scp -O game.zip root@10.42.0.1:/roms/SNES/`.

A release image starts no SSH at boot and has no keys in it; this screen is
the only way in. A development image already has SSH on every interface with
the developer's key, so there the row says `dev image` and the PIN is not
used.

### Plugging into a computer

On the Pixel 2 the USB port only charges until you say otherwise: a computer
sees no device at first. When a computer is plugged in, the shelf asks once,
with three tiles (left and right to move, A to choose; it starts on Charge
Only):

| | |
|---|---|
| **Charge Only** | nothing more; B, or no answer, means the same |
| **File Transfer** | opens USB File Transfer, with the link to the computer for as long as that screen is open |
| **Developer (SSH)** | development images only: the link stays on until the cable comes out, for the image's own SSH |

A wall charger is never asked about, and unplugging always goes back to
charging only. USB File Transfer in MENU turns the link on by itself too.

**Developer Mode** (MENU, under Show Tutorial, development images only) keeps
the link on whenever a computer is plugged in, with no question, so
`ssh root@10.42.0.1` with the developer's key always works. It is on by
default, and kept on the card as `.userdata/pixel2/developer`, which the
system reads at boot. A release image has no Developer Mode: it never turns
the link on at boot, whatever that file says.

## Audio

Sound can come out of four places, and ZyperOS picks in a fixed order:

**a USB-C headphone adapter, then wired headphones, then Bluetooth, then the
speaker.**

The adapter is the Pixel 2's, whose USB-C port takes one the moment it is
plugged in; the Brick's port does not. Bluetooth is the Brick's; the Pixel 2
has none.

A cable wins outright, in every setting. Someone who physically plugged
something in has said what they want more plainly than any menu can, and a
headset that merely happens to be connected has not said anything at all.

The **Audio Output** row has two positions rather than three. `Auto` follows
the rule above; `Speaker` refuses Bluetooth and nothing else - a cable still
works through it. There is no third "Headset" position because it would do
nothing `Auto` does not already do: `Auto` takes a headset whenever one is
connected, and neither setting can route to one that is not there. The row
shows where the sound actually went - `auto (wired)` - because `Auto` on its
own names a rule, not a place you can hear.

The **Audio Channels** row is for anyone who hears with one ear. `mono` plays
the two channels as one in both ears, so nothing a game or a song puts on one
side alone is lost. `mono left` and `mono right` put that sound in one ear
only, with the other silent, on headphones, a Bluetooth headset or a USB-C
adapter. On the speaker they play as `mono`, since a speaker is not on one
ear. It covers games and MP4 alike, and takes effect as you change it.

The wired jack has its own volume range, not the speaker's. The two are about
9 dB apart, and the level is re-mapped the moment a cable goes in or out, so
plugging in mid-game does not arrive at nine decibels louder than you left it.

### Menu music

Music plays behind the shelf and the menus, never over a game, a port or MP4:
your own MP3 and Ogg files in `Roms/BGM`, or ZyperOS's ten 8-bit tracks while
that folder has none. **MENU > Menu Music**:

| Row | What it is |
|---|---|
| **Music** | `On` or `Off`. `On` on a new device |
| **Volume** | 10% to 100%, under the system volume. `50%` to start |
| **Track** | `Shuffle`, which plays every track once before any plays again, or one track over and over |
| **Song Pop-up** | `On` or `Off`: whether Zyper says each new song's name on the shelf, and MP4's track when L2 or R2 moves it. `On` to start |
| **Playing** | the song playing; A goes to the next |

On the systems row and a games shelf, **R2** is the next song and **L2** the one
before, back through the last 32 heard; after going back, R2 goes forward through
them again before anything new. With **Track** on one song, L2 and R2 move the
setting to the song before or after it in the folder, which then plays over and
over. A press while the music is off, or quiet under a video snap's sound, is
refused with a sound. While MP4 plays behind the shelf the menu music is quiet,
and L2 and R2 move MP4 instead (below); with MP4 paused they are the menu
music's again.

Each time a new song starts on the shelf - by itself, by shuffle or by a press -
Zyper pops up at the top of the screen for two seconds with its name: the game or the
artist it is from above, from the file's own tags (ID3 in an MP3, the comments
in an Ogg), and the song's title below, or the file's name, tidied, when it has
no tags. A name too long for the panel scrolls, and the pop-up stays until it has
been read. It never blocks a button, and a menu, a game or MP4 puts it away. A
song that changes while a menu is open is not announced afterwards.

### Bluetooth

A paired headset reconnects by itself at boot and mid-session, and game audio
follows it without relaunching anything. The bond survives a reboot.

**Pairing is on the Bluetooth screen.** Put the headset in pairing mode, press
**Y** to search, **A** to pair and connect, **X** to forget. The toggle at the
top turns the radio on and off. Connecting a headset there disconnects any
other, because choosing one is the point; a headset that reconnects by itself
never pushes another off.

Only devices that advertise a name are listed. A scan in an ordinary room finds
a dozen BLE beacons and somebody's television, and BlueZ names everything it
cannot identify after its own address - so the list would otherwise be
unusable. A device you have already paired is always shown, named or not.

**Forget means forget.** The bond, the ALSA PCM and BlueZ's cached copy of the
device all go. Scanning leaves a cache entry for everything in range, so
leaving the screen sweeps the ones you never paired with.

The one thing that matters underneath is the agent: headsets pair "Just Works"
and need `NoInputNoOutput`. With the default agent every attempt fails with an
authentication error that looks like a broken key, a broken chip, or broken
headphones, and is none of them.

**A headset paired now carries sound straight away**, a running game's
included.

**The volume keys set the headset's own volume**, through its absolute volume
control, so there is still one volume and not two in series. The headset's
volume buttons move the Brick's level the other way. On a few headsets, the
Brick's buttons set only the starting volume; after that, change it on the
headset itself. In a game, a press on the headset changes the level without
showing the bar.

**The headset's play, pause and skip buttons control MP4**, wherever you are,
a game included.

One thing behaves differently on a Bluetooth sink and is not a bug:

- **there is roughly 100-150 ms of latency**, from SBC, the radio and the
  headset's own buffer. That is what Bluetooth audio costs on any device and
  nothing here can tune it away.

If a headset is switched off or walks out of range mid-game, sound falls back
to the speaker within a second or two and the game keeps running. It never ends
a game to report an audio problem. That fallback is the emulator's own - it
does not wait for the launcher to notice the headset is gone.

## MP4

MP4 plays the music, the audiobooks and the videos on the card. Its card at the end of
the shelf opens MP4,
the same as SELECT does: Now Playing when something is playing or paused, and
otherwise a shelf of album covers, the way a console's opens onto its games -
in a horizontal row, with Up/Down jumping from one initial to the next. A on an
album opens its tracks. MENU, anywhere in MP4, has the album count, Show,
Sort By, Album Art (below), and Rescan Folder for music copied on while the
Brick was running.

**Show** switches the shelf between music and audiobooks, and is there only
when the card has both. MP4 opens on whichever you left it on, except that
SELECT with a book playing lands on the books, on that book.

**Sort By** puts the shelf in order by artist, the way the folders are, or by
album title, two of the same title going in their artists' order. The jump
goes by the initial of whichever it is: an artist's, or an album's.

**SELECT** opens MP4 from anywhere that is not a running game: the
shelf, every menu, and the in-game menu, where the game waits paused
underneath. SELECT again closes it from any screen in MP4, and the music keeps
playing. B goes back one screen: Now Playing to the album's tracks, the tracks
to the shelf, and the shelf out of MP4. The exceptions are Wi-Fi File Transfer and a
Box Art fetch, which only keep working while they are on screen.

Albums go in `Music/`, a folder per artist and one inside it per album:
`Music/Radiohead/The Bends/01 Planet Telex.mp3`. A folder of
tracks straight under `Music/` is filed under its artist when its tags say
clearly who that is
(the album artist, or the artist every track agrees on), moving to
`Music/<artist>/<album>/` with its cover; one with no clear answer - a
podcast, a mix - stays an album of its own. Songs straight in `Music/`, in no
folder at all, are moved into `Music/Singles/` the next time the shelf is
read, a name already there getting " (2)", and Singles has a cover of its own
at `Music/.media/Singles.png` until you replace it. Wi-Fi File Transfer
reaches `Music/` too, and albums sent that way are on the shelf when you leave
its screen. MP3, M4A, M4B, MP4, AAC, FLAC, Ogg, Opus and WAV play. Tracks play in
file-name order, and the number at the front of a file name is left off the
name shown.

**Audiobooks** go in `Audiobooks/`, in the same two shapes: an author's folder
with a folder per book, or a book's folder on its own. A book is one M4B or a
folder of files, which play in name order. A on a book goes straight to Now
Playing and carries on where you left it: its place is kept every thirty
seconds while it plays, whenever it pauses or moves to the next file, and when
the Brick turns off. B from there is the book's files, and A on one starts it
from its beginning. A book played to its end says Finished under its cover,
and A on it starts it over. Books always play in order, so on their Now
Playing Y changes the speed instead of the play mode, and on books Sort By is
by author or by title and there is no Album Art. A book's cover is the one its files carry, or the picture in its
folder: one named cover or folder, or the only one there.

**Lock Screen**, for a pocket: press **X** on any MP4 screen while something
plays, or pick Lock Screen in MP4's menu. With music or a book the screen goes
dark, which on a book playing measured about 6 hours of listening becoming 10;
a video stays in sight. Every button is ignored, POWER included, except the
volume keys and a Bluetooth headset's play, pause and skip. The volume keys
change the volume in the usual steps and show the volume bar: with music the
screen lights for as long as the bar shows, about a second and a half after
the last press, and then goes dark again; over a video the bar shows on the
picture. Hold **SELECT + START** together for two seconds to unlock. If the
music stays paused past Auto Off, the device turns itself off with a book's
place saved.

With something playing or paused, SELECT goes straight to **Now Playing**: the
cover, the track, where in it you are, and what comes next.

While it plays you can leave MP4 and keep listening. On the systems row and a
games shelf, **L2** and **R2** then do what L1 and R1 do on Now Playing - the
track before or the next, or a book's chapter - and Zyper pops up with what
plays now: the artist and the title, or the book and its chapter. R2 on the
last track of an album playing in order has nowhere to go, and says so with a
sound.

| | |
|---|---|
| **A** | pause, or play |
| **L1/R1** | previous / next track, or chapter in a book that has them. More than three seconds in, L1 starts the track or chapter over |
| **Left/Right** | back or ahead ten seconds; held, a minute a step after a second and five minutes after three |
| **Y** | the play mode for music; the speed for a book, 1x to 2x and 0.75x, kept for each book |
| **B** | the album's tracks |
| **MENU** | MP4's menu |
| **SELECT** | close MP4 |

**Y changes the play mode on Now Playing**, and only there: in order, then
repeat all, then repeat one, then shuffle, and round again. Its mark sits
beside the track count on that screen, which is the one place a mode is shown -
so the mode is set where it can be read. Shuffle starts from the track you
chose and plays the rest in a random order, and when
it runs out it shuffles again and carries on - never starting the new round
with the song that ended the last. The mode is kept across restarts.

**While music plays, a running game is silent.** Only the game's own sound:
the music, the volume keys and the mute switch all carry on as before. Pause
the music, or let the album end, and the game is heard again within a tenth of
a second. SELECT in the in-game menu is how to switch between them mid-game.

An album's tracks are a list over its shelf, with what is playing under a rule
at the foot. A on a track plays the album from there and opens Now Playing; on
the track already playing it just opens Now Playing. The shoulders and the
d-pad's sides do what they do on Now Playing. Auto Off waits while music plays,
the way it does on the charger.

Covers come from the music. The first time an album is shown, MP4 takes the
picture its files carry and keeps it beside the album, in
`Music/<artist>/.media/`, the way box art sits in `Roms/<system>/.media`. An
album whose files carry none gets a generated card.

**Album Art**, in MENU on MP4's shelf, fetches covers over Wi-Fi for every
album with none, or with one smaller than the shelf draws it - 461px. It asks
MusicBrainz which record the album is, from the artist and album folder names
and the number of tracks - the track count is what tells a same-named single,
or another band, from the album - and takes that record's front cover from
the Cover Art Archive at 500px. An album it has supplied is remembered and not
asked about again; one it could not find is asked about on every run, since a
cover may be added later. MusicBrainz takes a request a second, so a large
library takes a while: seven albums took a minute on the Brick.

## The shelf

Systems use animated pixel console art on an open background. Games use a
straight row of box art. The selected item grows and the others stay quiet,
with no perspective or reflection on these pages.

The shelf always runs horizontally. Left and right move between systems or games.
L1 and R1 jump three systems on the systems row, coming round past either end
as a step does, and a screenful on a games shelf.
Up and down jump by initial on a games or album shelf. A opens a system or starts a game.
B returns to the systems row. Fancy Pants and UI Direction are removed.
Old saved layout and card-style values do not change this behavior.

MENU > Home Screen selects Spotlight or Gallery. MENU > System Order puts the
systems A to Z by name (3DO, 32X, Arcade, Atari 2600 ...) or by their release
dates, as before. Favorites stays first, and Ports and MP4 stay last. Spotlight puts the system name
beside its animated console sprite, with nearby systems below. Gallery puts the
focused sprite between its neighbors and shows the full system name below.
Spotlight uses short names, such as GBA and N64. These labels do not rename any
folder. Both layouts use the same controls and remember the same selected system.

Appearance selects Light, Dark or Auto. Console sprites retain their source
colors in both themes. Only the focused system animates. The top bar shows the
clock at the left, a background song when one is playing or paused, and Wi-Fi
and the battery at the right. A long song title shortens before the status
marks. MENU > Battery Icon shows the battery as its mark alone (Bars), with
its charge beside it (Percentage) or with the time left beside it (Time Left).
Time Left reads how long each percent has taken to fall, averaged over about
ten minutes, and counts to where ZyperOS saves and powers off (2%), not to 0%.
It shows the percentage until two falls have been seen, a few minutes after a
start or after the cable comes out, and it never shows more than a day. On the
cable it shows the time to full, or to the Charge Limit, from the gauge's
current as the charging screen does, and the percentage while that settles or
where the charger's gauge cannot say.

The games page uses a horizontal cover row. The focused cover is larger than its
neighbors. Its full scraped title, play time and achievement count appear below.
A resumes an available saved game or starts it otherwise. Y toggles Favorite;
X opens game details. A game without a cover keeps a readable text fallback.

The details' last row is **Delete Game**. It asks first, naming the game and
saying that its saves, states, cover and video go with it and that this cannot
be undone; the cursor starts on Cancel, and B is Cancel. The game's files go -
its folder for a game in one, the tracks a top-level `.cue`, `.gdi` or `.toc`
names, an arcade set's `.zip` - with its saves in `Saves/`, its save states and
their pictures, its cover and video snap, its cached achievement set, and its
details, favorite and play time in the library. Each path is built from the
game's own name, never matched by a pattern; a file another game on the card
still uses stays, a BIOS set such as `neogeo.zip` is never deleted, and a
sheet that names a file outside the game's folder stops the whole delete. The
shelf moves to the next game, or the one before at the end; a shelf left with
no games goes from the home. Ports have no Delete Game.

Card art comes from, in order:

1. box art in `Roms/<system>/.media/<name>.png`, whether you put it there or
   MENU > Scraper fetched it. With a ScreenScraper account the fetch asks
   ScreenScraper first. It stores the game's name, year, genre and synopsis.
   Without an account, or for a game ScreenScraper does not have, it asks
   libretro: by file name, and where no name matches, by checksum: a zip
   records its ROM's CRC, which names the game in No-Intro's list - the catalog
   libretro files its covers under - so a file still carrying an older name
   finds its box anyway. On the main card that found 29 more, every one the
   right game. With a SteamGridDB key (MENU > Scraper > SteamGridDB, or pasted on the
   File Transfer page), what libretro still has no cover for is asked of
   SteamGridDB last, by name, and taken only when the name is a close match:
   a portrait 600x900 grid in PNG or JPEG. Fetched art is shrunk to the size it is drawn at - libretro
   ships covers at its own resolution, which on some systems is two to three
   times the pixels this screen can show, and every one of those pixels is
   decode time on every scroll past. Art you put there yourself is never
   touched, whatever size it is - only what the fetch brings in;
2. **the autosave preview** - the frame you were looking at when you stopped,
   which for a game in progress is a better card than any box;
3. a generated slab: the system's color, the title, and the title's first
   letter enormous and barely there behind it.

**Game names.** A valid ScreenScraper reply supplies the displayed game name.
The launcher stores the name even if no cover exists or its download fails.
It shows the cleaned filename when no stored name exists.
It keeps the ROM filename and all launch, save, cover, favorite, and play-time keys.

The shelf updates when you leave the scrape screen, including after a stopped run.
No restart is necessary. The selected game stays selected on its system and Favorites shelves.
Name order uses the displayed name. Equal names use the filename to keep a stable order.
Play Time, Last Played, and Recently Added keep their primary order.

With a ScreenScraper account, the bulk scrape requests missing names even when covers already exist.
These requests keep the existing covers, including covers that you added.
The progress line counts saved names separately from downloaded covers.
Games with both a cover and a stored title keep the normal skip policy.
Use a game's Replace action to request its metadata and cover again.
The libretro cover fallback does not supply a stored display name.

**Adding a cover by hand.** Some games have no cover anywhere the fetch looks -
fan translations, homebrew, a dump no catalog lists. Put one in yourself over
Wi-Fi File Transfer: open `Roms/<system>/.media/` in its file browser and upload a
PNG named exactly like the ROM file, with `.png` in place of its extension -
`Black Castle.gb` wants `Black Castle.png`. Box Art never overwrites a cover
that is already there, and Replace only swaps yours out when ScreenScraper or
libretro has a cover to put in its place. Keep it near 512 pixels on the long
side: a cover you add is drawn as it is and never resized, so a 2000-pixel scan
is decoded at full size whenever its card comes into view.

Pressing A on a game with an autosave does not start it, it continues it.
