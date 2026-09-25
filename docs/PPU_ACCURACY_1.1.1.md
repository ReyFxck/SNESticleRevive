# SNESticle Revive 1.1.1 — PPU accuracy work

This document tracks the Mode 4/5/6, SETINI and high-resolution work being
developed on `dev/1.1.1-ppu-accuracy`.

## Reference policy

The implementation is maintained in SNESticle Revive and is cross-checked
against public SNES hardware documentation and MesenCE. SNESticle Aurora is
also credited as a useful PS2-oriented behavioral/comparison reference.

Reference projects:

- MesenCE: https://github.com/nesdev-org/MesenCE
- SNESticle Aurora: https://github.com/itsveenee/SNESticleAurora
- SNESdev uncommon graphics mode list:
  https://snes.nesdev.org/wiki/Uncommon_graphics_mode_games

## Current 1.1.1 status

| Area | Status in this branch | Notes |
|---|---|---|
| Mode 4 | Implemented / needs regression testing | BG1 8bpp + BG2 2bpp; Mode 4 one-word H/V offset-per-tile behavior retained. |
| Mode 5 | Implemented / needs regression testing | BG1 4bpp + BG2 2bpp; separate even/odd hi-res phases are decoded in the 256-wide PS2 carrier. |
| Mode 6 | Implemented / needs regression testing | Corrected BG1 from 8bpp to 4bpp; horizontal hi-res and separate H/V offset-per-tile paths added. |
| Pseudo-hires | Implemented as PS2 presentation path | Main/sub alternating dots are collapsed to the 256-wide carrier for CRT-like blending. |
| Overscan | Implemented / needs timing tests | SETINI 224/239 visible-line selection is latched per frame and the visible loop follows it. |
| Interlace timing | Implemented first pass | Field bit now changes at vertical wrap; NTSC/PAL field lengths and the extra interlace line are accounted for. |
| Native 448/478-line interlace picture | Pending | Revive's current PS2 output texture is still 256x256, so true double-height presentation needs a deeper GS/output change. |
| OBJ interlace | Partial | SETINI state is tracked and OBJ data is invalidated on changes; full interlaced OBJ vertical semantics still need dedicated work. |

## Useful game tests

These are practical regression targets taken from the SNESdev uncommon-mode
catalogue.

### Mode 4 / offset-per-tile

- **Bust-a-Move / Puzzle Bobble** — Mode 4 and vertical offset-per-tile on the playfield.
- **Rock 'n' Roll Racing** — Mode 4 on the equipment-buying screen.
- **Chrono Trigger** — horizontal OPT shimmer on the Black Omen intro effect;
  vertical OPT on the title/menu effects.
- **Yoshi's Island** — vertical OPT in 1-7 "Touch Fuzzy Get Dizzy" and moving
  platforms in 6-4.
- **Super Mario RPG** — vertical OPT in the Moleville mine-cart sections.
- **Tetris Attack** — vertical OPT for independently shifted columns.

### Mode 5 / high-resolution text

The PS2 renderer keeps even and odd dots separate. Stable lines may reuse an
exact final 512-dot result, but any VRAM, OAM, CGRAM or visual-register change
falls back to the complete renderer before presentation.

- **Secret of Mana** — Mode 5 menu.
- **Seiken Densetsu 3 / Trials of Mana** — Mode 5 text boxes and menus.
- **Dark Law: Meaning of Death** — Mode 5 text boxes, menus and status bar.
- **Donkey Kong Country** — Mode 5 for the small Rareware logo after it shrinks.
- **Porky Pig's Haunted Holiday** — Mode 5 introductory rooms.

### Pseudo-hires

- **Jurassic Park** — HUD overlay and notification box transparency.
- **Kirby's Dream Land 3** — foreground transparency.
- **Breath of Fire II** — shading in the intro town.

### Mode 6 / interlace

- **Lufia II: Rise of the Sinistrals** — Mode 6 with interlace during the end credits.

### Mode 5 + interlace

- **RPM Racing / Radical Psycho Machine Racing** — high-resolution gameplay
  throughout the non-Japanese release.
- **Desert Fighter / Air Strike Patrol** — high-resolution mission briefing text.
- **Power Drive** — high-resolution intro/logo/text screens.
- **Super Formation Soccer 94** — high-resolution menus.
- **Syvalion** — high-resolution text screens.

### Overscan (239 visible lines)

- **SNES Test Program** — switches to overscan immediately.
- **Tetris & Dr. Mario** — uses overscan in NTSC and PAL.
- **Tom & Jerry** — uses overscan in NTSC and PAL.
- **Yoshi's Cookie** — uses overscan in NTSC and PAL.
- **Rendering Ranger R2** — uses 239-line overscan in the Japanese release.

## Suggested test order

1. Secret of Mana — fast Mode 5 text sanity check.
2. Bust-a-Move / Puzzle Bobble — Mode 4 + vertical OPT.
3. Jurassic Park — pseudo-hires transparency.
4. RPM Racing — Mode 5 + interlace stress test.
5. Lufia II end credits — Mode 6 + interlace.
6. SNES Test Program — overscan and display-mode validation.

Do not mark a feature complete from emulator-only testing. Real PS2 hardware
remains the preferred validation target; NetherSX2/ARMSX2 are useful for
regression triage.
