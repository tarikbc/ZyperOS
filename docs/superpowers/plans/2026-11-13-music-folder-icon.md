# Music and ROMs Folder Art Implementation Plan

> **For agentic workers:** Use superpowers:executing-plans for this supplied design. The user requests one original icon and its folder mappings.

**Goal:** Give the card's Music folder its own static vinyl record design under MUSIC.

**Architecture:** Extend the existing card-folders group. Export eight static designs through the browser after the group changes. Compile MUSIC and map only Music folder surfaces to it. MP4 keeps MUSE.

**Tech Stack:** Built-in image_gen, JavaScript Canvas, Python, C, File Transfer web UI.

**Spec:** The user's 2026-11-13 Music icon prompt. The full image prompt and correction belong in groups/card-folders.json.

## Global constraints

- Start from origin/main in a new worktree on music-icon.
- Use a generic vinyl record and blank mustard sleeve, with a teal label.
- Preserve source/MUSIC.png unchanged from image_gen.
- Use one static pose with transparent margins.
- Redraw ROMS from scratch with visible gold contacts, as the user requested during work. Keep other designs and MP4 behavior unchanged.
- Show all eight designs at 256, 56, and 40 pixels on light and dark backgrounds in the PR.
- Use first person in the PR. Add no Co-Authored-By trailer. Do not merge or push to main.
- Draft commit and PR text for the user before publishing, as requested earlier.

## Tasks

- [x] Inspect the source, add MUSIC to groups/card-folders.js and its prompt/review record, and update source-index.json and manifest.json.
- [x] Export the group through export.js and tools/systemart-original-pack.py. Verify the other six card images and MP4 remain unchanged.
- [x] Add MUSIC to card-folders-review.html and its size previews. Review all sizes on both backgrounds.
- [x] Add MUSIC to tools/systemart.py EXTRA and the original checker. Compile with make system-art.
- [x] Change PLACE_ICON.music and STORE_ART_MUSIC to MUSIC. Inspect every MUSE art reference and retain the MP4 ones.
- [x] Update the portal assertion, DESIGN.md, and README folder mappings and counts.
- [x] Run the original-art and symbol checks, make check-portal, and CHECK_JOBS=3 make check.
- [x] Review the complete diff and prepare first-person PR text with the comparison images.
- [x] Obtain approval of the Concise commit and PR text for publication.

## Review focus

- Music must remain distinct from Videos, BGM, Audiobooks, and game discs at 40 pixels.
- The sidebar and folder list use MUSIC. Existing phone chips remain text-only. Storage includes the Music row and media-detail breadcrumb.
- MP4's shelf and music player must retain MUSE.
- Changing a group renderer hash requires all eight group exports to remain valid without changing existing artwork.
- The compiled atlas and still must remain within existing memory budgets.

## Scope update and review

The user requested a new ROMS design with visible pins. The user selected a wide purple cartridge with original artwork on its paper label. The selected source is ROMS-game.png. Previous sources stay in history records.
The review found that phone chips use text only. The supplied user request does not change this behavior.
The stale Storage artwork comment now matches MUSIC.

## Verification

- CHECK_JOBS=3 make check passed all 161 checks after the MUSIC fixture update.
- The first suite run found the missing MUSIC fixture; the fixture and C contract now include MUSIC separately from MUSE.
- Independent review passed the final purple ROMS artwork at 256, 56, and 40 pixels on both backgrounds.
- Original source images and the other six card designs remain unchanged.
- Final art checks, browser checks, and screenshots are refreshed after the selected ROMS export.

## Publication

The user approved the Concise draft. Commit on music-icon, push that branch, and open a PR against main. Keep the PR open.
