# Original System Art Implementation Plan

> **For agentic workers:** Use superpowers:executing-plans for implementation work. This batch produces review artwork and animation previews.

**Goal:** Increase Game Boy detail and create the next original animated system art batch.

**Architecture:** Keep the generated base illustrations immutable. Animate screen content and mechanical layers over those bases. Store sources, prompts, and preview code together before runtime integration.

**Tech Stack:** Built-in image generation, PNG assets, JavaScript Canvas, HTML previews.

**Spec:** `docs/superpowers/specs/2026-11-12-original-system-art-design.md`

## Global Constraints

- No public icon pack pixels in the new artwork.
- No reduced hardware photos as replacement artwork.
- Game Boy preview: 256 × 256 pixels, unchanged display size.
- Original startup sequences on built-in screens.
- Media loading or power motion for systems without screens.
- Preserve static body, controller, and cable pixels during animation.
- Keep preview artwork separate from installed assets until export review.

## Review Focus

- Closed lid must match the original closed frame.
- Open lid must remain attached to the rear hinge.
- Moving parts must stay inside the canvas.
- Screen animation must stay inside the LCD opening.
- Preview controls must work with reduced motion and at narrow widths.

## Tasks

- [x] Save approved source drafts, prompts, and provenance under `res/system-art/original/`.
- [x] Increase the Game Boy preview to 256 pixels and compare screen clarity.
- [x] Generate the PlayStation optical bay and animate its existing lid about the rear hinge.
- [x] Generate SNES and Game Boy Advance sources in the approved style.
- [x] Add the 3DO tray and SNES cartridge animations.
- [x] Add the Game Boy Advance startup preview from documented reference behavior.
- [x] Inspect the previews and verify that static pixels remain fixed.
- [x] Save the review page and show the updated animation samples.

## Result

The five-system review batch is complete. Runtime installation remains a later step.
The perspective review led to corrected PS1 hinge motion, 3DO tray motion, SNES insertion depth, and native screen proportions.
GBA startup lettering and timing remain a visual study.
Frame, loop, clipping, controls, narrow-layout, and reduced-motion checks passed.
