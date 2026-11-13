# Card Folder Art Implementation Plan

> **For agentic workers:** Use the original-art group contract. Keep source generation and runtime wiring separate until the integration reaches main.

**Goal:** Add original still artwork for seven card folders and use it in File Transfer and Storage.

**Architecture:** One static original-art group supplies the seven designs. The existing exporter writes one-pose PNGs. The runtime compiler and folder mappings use those keys.

**Tech Stack:** Built-in image generation, PNG, JavaScript Canvas, Python, C, and the File Transfer web UI.

**Spec:** my prompt for the card folders (2026-11-13), summed up under Global constraints.

## Global constraints

- Start from latest main (the art was made on `codex/card-folder-art`; the wiring is on `card-folder-art`).
- Add ROMS, BOOKS, VIDEOS, BIOS, SAVES, STATES, and BGM.
- Use generic objects without brand marks or label text.
- Keep every design static with one pose.
- Check readability at 40 and 56 pixels.
- Music keeps MP4's art. Other folders keep FOLDER_SMALL.
- Do not change another art group or animation.js.
- Open a PR with all seven designs at 256, 56, and 40 pixels.
- Do not merge the PR.

## Tasks

- [x] Generate and inspect seven source designs in groups/card-folders.
- [x] Export the designs and add them to the gallery.
- [x] Review all seven at 256, 56, and 40 pixels on light and dark backgrounds.
- [x] Record prompts, source hashes, and the independent visual review.
- [x] Run the original-art, symbol, browser, preview, and current check-systemart checks.
- [x] Update the branch from main after the runtime-art integration lands.
- [x] Add compiler EXTRA entries and rebuild installed icons and metadata.
- [x] Wire File Transfer, Storage, and BGM to their new keys.
- [x] Update portal checks and folder-art documentation.
- [x] Retake portal, portal-phone, and storage screenshots.
- [x] Run check-systemart and the full suite after runtime wiring.
- [x] Draft the commit and first-person PR text with the three-size artwork view.
- [x] Open the PR without merging it.

## Review focus

- Small details must remain readable at the actual UI sizes.
- No old folder-photo pixels may enter the new sources.
- Each exported design must have exactly one pose.
- Music and generic folders must retain their intended artwork.
- Roms/BGM must map to BGM in both real and test servers.
- The runtime compiler must consume the new exports before screenshots run.

## Base dependency

The artwork started at origin/main 18d044e, before the runtime integration (PR #110).
The wiring starts at origin/main eb5c1ed, which has it.

## Artwork verification

- Original-art check: all 57 designs pass hash, alpha, pose, and budget checks.
- Symbol pixel check: PICO-8 and TIC-80 preserve symbol pixels and nearby lettering.
- Browser check: all 57 designs pass fixed pixels, motion bounds, clipping, and loop checks.
- Browser controls, narrow layout, and reduced motion pass.
- All seven card folders remain static with one pose each.
- All 14 small previews pass dimensions, hashes, transparency, and margin checks.
- The review page loads all 21 images at their stated sizes. Both backgrounds and narrow layout pass.
- Current check-systemart passes 18 tests and verifies the legacy installed catalogue.
- The runtime compiler builds the seven card folders; the full suite passes.
