# Pixel UI home screen

## Approved direction and boundaries

I want a modern, clean home screen with quick motion, using my Pixel UI
Smart Folders archive. Do not put its sprites inside the old system cards.
The selected preview is Spotlight. Settings must allow a home-screen choice;
the implementation offers Spotlight and Gallery, both horizontal, beside the
existing Appearance choice (Light, Dark, Auto). No Fancy Pants, vertical, or
Cubic paths return. Use the existing ZyperOS startup wordmark at the upper left,
without the dog head, at about 160 logical pixels wide. Light mode uses
accent-ink for the Z and text for the remaining letters; dark uses accent/text.
A background song must appear in the top bar with Wi-Fi, clock, and battery.

The interactive reference is
`/Users/tarikbc/.codex/visualizations/2026/11/04/01a1a84d-08b7-7dd0-b287-7accace90676/zyperos-home.html`.
It uses the supplied GIFs. Prototype counts and game lists are illustrative;
production uses the library. My actual track, Posters, is sample content,
not a value to hard-code. The preview's settings panel is only the relevant
rows; production keeps the rest of the existing Settings menu.

Use DESIGN.md roles, Lexend, the 1024x768 logical canvas, and existing input
semantics. The handheld has no touch. This changes system navigation, not the
physical ROM identity, save paths, emulator controls, or game cover art.

## Layout and motion

Spotlight: logo at upper left; current track in the middle of the top bar;
status corner at upper right. System name and count at the left, a large free
console sprite at the right, and five small unframed system sprites below.
The selected dock sprite has the accent underline. The A legend opens games.
MENU opens Settings; SELECT opens Muse as it does now. System names are always
one line. Spotlight uses short labels such as GBA and N64; Gallery uses full
system names. Keep canonical system names and file identities unchanged.
Long track names end with an ellipsis; they never
push clock, Wi-Fi or battery off screen. An absent track leaves no empty chip.

Gallery: the focused sprite is centered, larger than its two neighboring
sprites, with the title and count below. Preserve the same header and controls.
No perspective cards, slab, reflection, card borders, or colored floor rail.

Only the selected system animates. Neighbor sprites show a still frame.
Honor supplied frame delays and loop seams, and stop requesting frames when
the systems screen is absent. Selection moves use DS_MS_MOVE and ds_out;
interrupted movement begins at its current rendered position. Do not block an
input, decoder, or music update while motion runs. Rendering restores SDL
target, viewport, scale, clip and blend state wherever it changes them.

## Art sources and credits

The user supplied `pixel-ui-smart-folders.zip`, version 1.4. Its `logo.gif`
files depict the consoles, often with controllers; they are not wordmarks.
Use those, with the animated heart icon for Favorites. No overlays.
Credit BenJelter with https://benjelter.itch.io/console-icon-pack in README,
third-party notices, and the asset folder. Retain YOjeremyRaps's additions and
animation credit from the archive metadata. Keep generated additions separate.

The pack covers 28 of 37 configured systems, plus Favorites. Missing:
Game & Watch, Vectrex, MSX2, 3DO, Neo Geo CD, WonderSwan Color, NAOMI,
Atomiswave, Ports, and the synthetic Muse shelf. Generated drafts require
hardware-reference review before import. WSC's corrected source follows the
user photograph and lives at `res/system-art/source/WSC.png`. Do not import the
inaccurate silver draft. The other generated images remain drafts outside the
repo. Do not substitute a different machine as if it were the missing system.

## Implementation tasks

1. Asset compiler and metadata. Import only sources actually used by configured
   systems plus Favorites and Muse. Keep exact source attribution in a manifest.
   Compile bounded PNG atlases, first-frame posters, and frame-duration tables.
   Source colors remain unchanged between themes; use nearest sampling. An atlas
   contains only sprite pixels, never whole-screen or whole-card frames.
   Validate dimensions, alpha, durations, unique coverage, and total output size.
   Report unresolved artwork explicitly until it is supplied.
2. Home rendering and persistent setting. Add a focused renderer module with
   explicit data inputs, plus the smallest main/menu/db hooks. Remove all old
   system-card loading/generation paths after replacement assets are complete.
   Preserve game fallback-card generation. Reuse the existing worker loader or
   document and measure any cold decode on focus. Keep atlas residency bounded.
3. Header. Use the generated startup letter shapes, without the dog head, with
   the corrected accent-ink/text colors in light mode and accent/text in dark.
   Read Muse's existing cached state for the current song, without extra blocking
   socket calls. Render a music mark and title only when relevant. Track changes,
   pause, resume, and stop must request a frame even on a still home screen.
   Paused playback uses a paused mark; no activity animation while paused.
4. Finish missing art, delete the old system marks and posters, delete the
   obsolete card generator and checks, and update payload/host fixture paths.
   The repository and installed payload must carry only the replacement art.
5. Document final tokens and behavior in DESIGN.md. Check both layouts/themes,
   long and empty names, one/two/many systems, rapid reversals, menu returns,
   theme changes, missing/corrupt art fallback, and background music transitions.
   Use real host screenshots and focused regression tests. Run native and one
   complete headless make check. Independent review, PR against main, then the
   integration chat tests on Pixel 2 before merging.

## Coordination

## Games page decision, 2026-11-04

I selected **Cover row** from
`/Users/tarikbc/.codex/visualizations/2026/11/04/01a1a84d-08b7-7dd0-b287-7accace90676/zyperos-games.html`.
Implement that design after the home renderer, using real game data:

- Keep the same small wordmark, current-track indicator, clock, Wi-Fi and battery.
- Show the full system name and selected game position above the covers.
- Use a straight horizontal row with one large focused cover and smaller, quieter
  neighbors. Preserve each cover's aspect ratio. No tilted perspective, reflection,
  extra card frame, or backdrop that competes with the art.
- Put the scraped game title below the row, with play time and achievement count.
  Titles remain full game names. Fit/wrap long titles without colliding with the
  A action or footer. The preview's values are examples, not production constants.
- A says Resume when the existing resume logic can actually resume, otherwise
  Play. Preserve the existing launch/resume semantics and saving behavior.
- B returns to systems, Y toggles Favorite, X opens existing game details. Keep
  MENU, SELECT, shoulder jumps and the remaining controls working.
- Use DS_MS_MOVE/ds_out; handle wrap and rapid direction reversal without moving
  an offscreen neighbor through the focused art. Retain bounded worker loading.
- Missing art uses the existing text fallback in the current theme. Absent play
  or achievement data does not invent zeros or prevent opening the game.
- Include wide, tall and square covers, long titles, missing art, one/two/many
  games, music transitions, both themes, and resume availability in host checks.

This page is selected for all home-layout choices; do not add another setting.
Game details remain the existing X page unless a later request changes them.

Branch `pixel-home` currently includes reviewed cleanup `afe5dda` (PR #35).
Keep its relationship explicit until #35 merges. No device or plastron actions
from this worktree. The integration chat owns About's storage/battery fields.
Do not edit About. Current other branches own scraped titles and menu motion.
Do not commit unrelated work, add co-author trailers, or invent device results.
