# SNESticle Revived

Revived and actively-maintained source of **SNESticle**, the long-rumored
**Super Nintendo (SNES) emulator** written by **Icer Addis (iaddis)**.

SNESticle was famously hidden inside the **GameCube** version of EA's
**Fight Night Round 2 (2005)**, where it ran **Super Punch-Out!!**. The
community reverse‑engineered and extracted that build in **2022**, and Sardu
released the source under the **MIT license**. This repository keeps that code
alive: reorganized into logical directories, fixed, extended, and made easy to
build and study today.

On top of the SNES core, the project now also integrates **InfoNES** to bring
**NES** emulation to the **PlayStation 2**.

> Primary target: **PlayStation 2** (EE/IOP, gsKit). Development is done on
> Debian via Termux — no full desktop required.

---

## Features

**Systems**
- **SNES** — the original SNESticle core (65816 ASM CPU, SPC700, PPU).
- **NES** — via **InfoNES** (`src/nes/`), with audio wired to the PS2 audio path.

**SNES special chips (coprocessors)** — clean‑room reimplementations, MIT‑safe:
- **DSP‑1 / DSP‑1B** — Pilotwings, Super Mario Kart, etc. (`sndsp1`)
- **DSP‑2** — Dungeon Master (`sndsp2`)
- **CX4** — Mega Man X2 / X3 (`sncx4`)
- **OBC1** — Metal Combat (`snobc1`)
- **S‑DD1** — Star Ocean, Street Fighter Alpha 2 (`snsdd1`)
- **S‑RTC** — Daikaijuu Monogatari II (`snsrtc`)

**PlayStation 2 platform**
- gsKit‑based video backend with a **Video Config** screen.
- Multiple video modes: **480i** (default, universally compatible), **480p**
  (GSM / HDMI), **240p / 288p** (CRT), plus screen offset and widescreen.
- **Cover art** in the ROM browser — box art / screenshots from PNG files,
  decoded by **upng** (a bundled single‑file decoder, no external libs). See
  [Cover art](#cover-art-capas).
- Audio via SjPCM / audsrv.
- Controller / memory‑card / IRX bring‑up aligned to **Open‑PS2‑Loader** style.
- Netplay code (`src/modules/netplay/`).

---

## Controls

The PS2 pad maps to an SNES controller. **L2 + R2** (pressed together) toggles
between the game and the menu at any time.

**In a game**

| Button | SNES |
|:------:|------|
| <img src="docs/controls/dpad.svg" height="20" alt="D-Pad"> | D‑Pad |
| <img src="docs/controls/cross.svg" height="20" alt="Cross"> | B |
| <img src="docs/controls/circle.svg" height="20" alt="Circle"> | A |
| <img src="docs/controls/square.svg" height="20" alt="Square"> | Y |
| <img src="docs/controls/triangle.svg" height="20" alt="Triangle"> | X |
| <img src="docs/controls/l1.svg" height="20" alt="L1"> / <img src="docs/controls/r1.svg" height="20" alt="R1"> | L / R |
| <img src="docs/controls/select.svg" height="20" alt="Select"> | Select |
| <img src="docs/controls/start.svg" height="20" alt="Start"> | Start |
| <img src="docs/controls/l2.svg" height="20" alt="L2"> + <img src="docs/controls/r2.svg" height="20" alt="R2"> | Open the menu |

**Menu & ROM browser**

| Button | Action |
|:------:|--------|
| <img src="docs/controls/dpad.svg" height="20" alt="D-Pad"> Up / Down | Move the selection |
| <img src="docs/controls/cross.svg" height="20" alt="Cross"> or <img src="docs/controls/start.svg" height="20" alt="Start"> | Launch the highlighted ROM (or open a folder) |
| <img src="docs/controls/triangle.svg" height="20" alt="Triangle"> | Go up one folder (`..`) |
| <img src="docs/controls/square.svg" height="20" alt="Square"> | Page up — *or swap the cover image when cover art is on (see below)* |
| <img src="docs/controls/circle.svg" height="20" alt="Circle"> | Page down |
| <img src="docs/controls/select.svg" height="20" alt="Select"> | File menu (copy / paste / delete) |
| <img src="docs/controls/l1.svg" height="20" alt="L1"> / <img src="docs/controls/r1.svg" height="20" alt="R1"> | Switch screen (Browser ⇆ Network ⇆ Menu ⇆ Log) |
| <img src="docs/controls/l2.svg" height="20" alt="L2"> + <img src="docs/controls/r2.svg" height="20" alt="R2"> | Return to the game |

**Video Config screen**

| Button | Action |
|:------:|--------|
| <img src="docs/controls/dpad.svg" height="20" alt="D-Pad"> Up / Down | Select an option |
| <img src="docs/controls/dpad.svg" height="20" alt="D-Pad"> Left / Right | Change its value |
| <img src="docs/controls/square.svg" height="20" alt="Square"> | Reset the screen offset |
| <img src="docs/controls/cross.svg" height="20" alt="Cross"> or <img src="docs/controls/start.svg" height="20" alt="Start"> | Save settings to the memory card |

---

## Cover art (capas)

The ROM browser can show box art / screenshots beside the game list.

- Enable it in **Video Config → Cover Art** (press ✕ to save — it persists
  across boots).
- Put a PNG with the **same name as the ROM** anywhere the browser looks:
  - **next to the ROM** — e.g. `Super Mario Kart (USA).png`
  - in the **libretro thumbnail folders** (beside the ROMs, or inside a
    `COVERS_PATH` folder): `Named_Boxarts/`, `Named_Titles/`, `Named_Snaps/`
  - with a **numeric suffix** for extra images: `Game-1.png`, `Game-2.png`, …
- Press **□** in the browser to cycle through whatever a game has, in order:
  box art → title screen → gameplay snap → extra `-N` images.
- To keep every cover in **one shared folder** instead of next to each ROM,
  build with `COVERS_PATH`:
  ```bash
  make COVERS_PATH=mass:/snes/covers
  ```
  Covers are then looked up under `mass:/snes/covers/` (including its
  `Named_Boxarts/` etc.), with the ROM's own folder as a fallback.
- When you build an ISO with `ROMS=`, PNGs in the ROM folder are bundled
  automatically.

**Supported PNG formats:** RGB / RGBA (8‑ or 16‑bit), grayscale, and
palette/indexed (1/2/4/8‑bit). **Interlaced (Adam7) PNGs are not supported** —
re‑save those as non‑interlaced. Keep covers small (≈256 px) to save memory and
decode time; they are cached in RAM and prefetched so browsing stays smooth.

---

## Building (PlayStation 2)

You need **PS2SDK** installed. Follow the
[ps2dev](https://github.com/ps2dev/ps2dev.git) instructions and use the
**latest** PS2SDK.

```bash
cd ~/SNESticleRevive

# Just build the ELF
make                 # single worker
make JOBS=3          # parallel build (3 workers)

# Build a bootable ISO with a ROM folder and copy everything out
make iso ROMS=/path/to/roms OUT=/path/to/output JOBS=3

# See every option
make help

# Clean build folder
make clean
```

Produces `SNESticle.elf` (and a packed ELF / ISO for the `iso` target).

### Handy build flags

| Flag | What it does |
|------|--------------|
| `JOBS=N` | Number of parallel compile workers (also honored by `make iso`). |
| `VERBOSE=1` | Show the **full** warning/error text (no truncation). |
| `PROFILE=1` | Compile the on‑screen profiler in — press **R3** in‑game to capture one frame's per‑section timing. |
| `OUT=/path` | Copy the final ELF/ISO to this folder. |
| `ROMS=/path` | ROM folder to embed when building an ISO. |
| `PACK=0` | Build the ISO using the unpacked ELF. |
| `COVERS_PATH=path` | Shared cover‑art folder baked into the build (e.g. `mass:/snes/covers`). See [Cover art](#cover-art-capas). |

> Note: changing a flag like `PROFILE=1` does **not** force a recompile on its
> own (make only tracks file timestamps). Run `make clean` first when toggling
> compile flags.

---

## What's been done recently

- **Coprocessors**: added DSP‑1, DSP‑2, CX4, OBC1, S‑DD1 and S‑RTC, each
  written clean‑room and verified bit‑exact host‑side against public references
  before being committed (no GPL/Snes9x code in this MIT repo).
- **NES (InfoNES) integration**: full PS2 platform layer (render, input, audio,
  one‑frame stepper), with the InfoNES core kept 1:1 with upstream.
- **Video**: gsKit migration, the Video Config screen, multiple modes, and a
  **safe 480i default** (240p stays available for CRT users).
- **Cover art**: the ROM browser shows box art / screenshots from PNG files,
  via a bundled single‑file decoder (RGB/RGBA, grayscale, and palette/indexed).
  Decoded covers are kept in a small RAM cache and neighbours are prefetched, so
  browsing stays smooth even from a CD; toggle it in Video Config, point it at a
  shared folder with `COVERS_PATH`, and cycle box/title/gameplay with □.
- **Boot / input**: controller and IRX bring‑up reworked to behave on real
  hardware, not just emulators.
- **Build system**: parallel jobs, `VERBOSE`, `PROFILE`, friendlier `make help`,
  and ISO builds that honor `JOBS`.
- **Bug fixes**: C++17 / build warnings cleaned up, plus three real
  out‑of‑bounds bugs fixed in the InfoNES core (`APU_Reg`, mapper 19 & 45 arrays)
  and a sequence‑point UB fixed in the 6502 core.

---

## Known issues / still missing

**SNES**
- **Final Fight 2** — large (32×32) / page‑1 object sprites render garbled. The
  OBJ fetch/render path has been verified correct against hardware references
  (bsnes/Anomie) and host‑side; the cause is suspected to be the VRAM
  data/upload feeding it. **Under investigation.**
- Some large / special‑chip titles may still freeze or misbehave.
- **Missing chips**: SA‑1 and SuperFX (GSU) are not implemented.

**NES (InfoNES)**
- **Performance**: heavy scenes can push a frame over the 16.6 ms budget, which
  vsync then locks to **30 fps**; this also knocks audio and per‑scanline
  effects out of sync. (Use `PROFILE=1` + R3 to locate hotspots.)
- **Super Mario Bros 3** — the MMC3 status‑bar split can glitch when scrolling
  (InfoNES uses a scanline‑approximated MMC3 IRQ, not A12‑accurate).
- Audio timing can be off until the game settles (related to the 30 fps issue).

**Video**
- **240p is not a standard HDMI/DTV mode** — passive PS2→HDMI adapters and most
  modern TVs will not lock onto it (no signal). The default is **480i**; pick
  240p in the Video Config screen only on a CRT or a 240p‑capable scaler.

> Some bugs only reproduce on **real PS2 hardware** (emulators like NetherSX2 /
> PCSX2 are more forgiving), which makes them harder to track down.

---

## Project layout

```
src/snes/      SNES core (cpu, spc, ppu, coprocessors)
src/nes/       NES core (InfoNES: core, cpu, apu, mappers, system)
src/platform/  PlayStation 2 platform (gs, system, input, ui)
src/modules/   shared modules (audio, netplay, ...)
src/common/    shared helpers (render, base, io, debug)
tools/         host‑side test harnesses (chip + OBJ verification)
```

---

## Credits

- **[iaddis/SNESticle](https://github.com/iaddis/SNESticle)** — Icer Addis, the original emulator.
- **[tmaul/SNESticle](https://github.com/tmaul/SNESticle)** — many later improvements.
- **[Wolf3s/SNESticle](https://github.com/Wolf3s/SNESticle)** — fork used as one of the bases for this repository.
- **Sardu** — for releasing the recovered source under the MIT license (2022).
- **[jay-kumogata/InfoNES](https://github.com/jay-kumogata/InfoNES)** — the NES core integrated here.
- **[upng](https://github.com/elanthis/upng)** — Sean Middleditch & Lode Vandevenne; the bundled single‑file PNG decoder used for cover art (zlib license). Extended in this repo with palette/indexed support.
- **[hugorsgarcia/PS2SNESticle](https://github.com/hugorsgarcia/PS2SNESticle)** — **Hugo Garcia**, whose PS2 work was the reference for the controller / memory‑card / IRX bring‑up and the netplay module.
- **Open‑PS2‑Loader**, **picodrive‑PS2** and **uLaunchELF** — references for correct PS2 boot, IOP and video behavior.
- **ReyFxck** — this revival/fork and ongoing development.
- **Adriano Oliveira** — real‑hardware testing.
- **Control‑prompt icons** (`docs/controls/*.svg`) — original SVGs drawn for this repo; reuse freely.

---

## License

MIT — see [`LICENSE`](LICENSE).

## TODO

- [ ] **[PS2]** Replace precompiled IRX modules with PS2DEV‑generated ones. *(hard)*
- [ ] **[PS2]** Replace libcdvd with the latest PS2DEV libcdvd. *(medium)*
- [x] **[PS2]** Update the Makefile for newer PS2SDK versions.
- [ ] **[PS2]** Finish moving custom GS code to gsKit where possible. *(ongoing)*
- [ ] **[NES]** Profile and bring heavy scenes back to a stable 60 fps.
- [ ] **[SNES]** Track down the Final Fight 2 sprite data/upload bug.

<sub>Yes, we still have a lot of free time :)</sub>
