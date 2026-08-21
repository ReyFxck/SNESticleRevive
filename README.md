<p align="center" style="margin-bottom: 4px;">
  <img src="docs/controls/SNESTICLE.png" alt="SNESTICLE" width="500">
</p>

<p align="center" style="margin-top: 0;">
  <b><font size="7">
    SNESticle Revive PlayStation 2<br>
    Homebrew and S/NES emulator!
  </font></b>
</p>

⚠️Temporarily Closed⚠️
I don't have much free time left in my life, and it's not yielding much return anymore; hours and hours for little result...
Don't worry, it's nobody's fault. Of course, some people are kind of "playing the critic," but this isn't an emulator meant to run everything, since it was created for only one specific game.
And it wasn't made exclusively for Playstation 2©, but also for the GameCube, which, if I remember correctly, is a bit more powerful. Remember, this is a project just for fun and was also leaked by the author himself, Ardu.
I'll come back to this someday :) Don't worry, and maybe the NES will get a worthy treatment (even though I don't care much for the NES).
For now, have fun with the Fork below! By @Itsveenee

https://github.com/itsveenee/SNESticleAurora

---

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

---

## 📚 Table of Contents

- [⚠️ Notes](#️-notes)
- [🚀 Features](#-features)
- [🎮 Controls](#-controls)
- [🖼 Cover Art](#-cover-art)
- [🎵 Menu Music & Audio](#-menu-music--audio)
- [💾 Storage & Devices](#-storage--devices)
- [🔨 Building](#-building-playstation-2)
- [📝 What's been done recently](#-whats-been-done-recently)
- [🐞 Known issues](#-known-issues--still-missing)
- [📂 Project layout](#-project-layout)
- [❤️ Credits](#-credits)
- [📜 License](#-license)

---

## ⚠️ Notes

> [!WARNING]
> **Attention**
>
> **Primary target:** **PlayStation 2** (EE/IOP, gsKit). Development is done on PS2SDK, and the add-on works on all devices that support the PS2SDK.
>
> There's no need to create ISOs in this version; only do so if you want to distribute them to the community.
>
> **Please don't remove the credits of Icer Addis (iaddis), the original creator of SNESticle, or my credits (ReyFxck), maintainer of SNESticle Revive, from the homebrew.**
---
## 🚀 Features
<details>
<summary>🕹️ SNES Progress</summary>

**Systems**
- **SNES** — the original SNESticle core (65816 ASM CPU, SPC700, PPU).
- **NES** — via **InfoNES** (`src/nes/`), with audio wired to the PS2 audio path.

**SNES special chips (coprocessors):**
- **DSP‑1 / DSP‑1B** — Pilotwings, Super Mario Kart, etc. (`sndsp1`) — clean‑room
- **DSP‑2** — Dungeon Master (`sndsp2`) — clean‑room
- **DSP‑4** — Top Gear 3000 (`sndsp4` + `dsp4emu`), **HLE / self‑contained** (no
  external files). The full track‑projection math is the **ZSNES** DSP‑4 HLE
  (GPLv2, © ZSNES Team), ported here with attribution — which is why this fork
  is now **GPLv2** (see [License](#license)).
- **CX4** — Mega Man X2 / X3 (`sncx4`)
- **OBC1** — Metal Combat (`snobc1`)
- **S‑DD1** — Star Ocean, Street Fighter Alpha 2 (`snsdd1`)
- **S‑RTC** — Daikaijuu Monogatari II (`snsrtc`)
</details>
<details>
<summary>🎮 PS2 Progress</summary>

**PlayStation 2 platform**
- gsKit‑based video backend with a **Video Config** screen.
- Two interlaced video modes: **480i** (default, universally compatible) and
  **1080i** with a centred 4:3 viewport, plus screen offset, overscan and
  widescreen. The former 240p/288p and 480p paths were removed to keep the GS
  setup on the stable 640x480 interlaced framebuffer path.
- Switchable SNES colour profiles: **Original** (default) and the emulator's
  restored **Composite** YIQ calibration; the choice can be previewed live.
- **Cover art** in the ROM browser — box art, title screens, gameplay snaps,
  logos and manual extras from PNG files, decoded by **upng**. Libretro art can
  be fetched automatically with `COVER=y`. See [Cover art](#-cover-art).
- The ROM browser consumes each driver's directory records directly, avoids a
  separate `stat`/disc seek per CDFS entry, and grows its list dynamically.
  Directory types are read in the normalized `FIO_S_*` format returned by
  iomanX, with a full-path probe only for drivers that return an unknown type.
  Its embedded streaming CDFS driver removes PS2SDK's fixed 256-entry ISO
  table. Folders are shown as `> NAME/`; the marker and final slash remain
  visible while only a long middle name is truncated or scrolled.
- Launching a plain, ZIP or GZ ROM uses bulk `fileXio` reads instead of small
  stdio RPCs. The loader no longer opens every ROM twice or clears an unused
  8 MiB buffer before parsing it, reducing launch latency on slow devices.
- **Menu music** — tracker tunes (`.mod` / `.xm`) play in the ROM browser and
  pause menu, with volume and synthesis‑rate controls. See
  [Menu music & audio](#menu-music--audio).
- Audio via **audsrv**, with separate **Game Volume** and **Menu Music**
  controls in the Video Config screen. Its stop/resume state is explicit, so
  menu and SNES audio no longer depend on opening a NES game after boot.
- **SNES and NES cartridge save states** — five slots; USB, memory-card, MMCE
  and internal-HDD storage; ROM and CRC validation; and two-bank writes that
  preserve the previous valid state.
- **Battery SRAM** — SNES and NES saves are separated under
  `mc0:/SNESticle/SNES/` and `mc0:/SNESticle/NES/`; old SNES saves in the
  v1.0.3 root layout are loaded and migrated without deleting the original.
- Controller / memory‑card / IRX bring‑up aligned to **Open‑PS2‑Loader** style.
- **Storage**: USB (×2), external HDD/SSD and **MX4SIO** SD cards as
  `mass0:`/`mass1:`; the internal **HDD** (`hdd0:`); memory cards
  (`mc0:`/`mc1:`) including **MMCE** carts (MemCard PRO 2 / SD2PSX) as
  `mmce0:`/`mmce1:`. Reads FAT16/FAT32/**exFAT** with MBR/GPT partition
  tables via the bundled BDM stack. See [Storage & devices](#storage--devices).
- Netplay code (`src/modules/netplay/`).
</details>

---

## 🎮 Controls

The PS2 pad maps to an SNES controller. **L2 + R2** (pressed together) toggles
between the game and the menu at any time, flushing changed SRAM when the menu
opens. The menu is displayed first; a pending SRAM write starts two rendered
frames later and reports completion without the old fixed one-second pause.

<details>
<summary>🎮 In Game</summary>

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
| <img src="docs/controls/l2.svg" height="20" alt="L2"> + <img src="docs/controls/cross.svg" height="20" alt="Cross"> | Save state to the current slot |
| <img src="docs/controls/l2.svg" height="20" alt="L2"> + <img src="docs/controls/circle.svg" height="20" alt="Circle"> | Load state from the current slot |
| <img src="docs/controls/l2.svg" height="20" alt="L2"> + <img src="docs/controls/r2.svg" height="20" alt="R2"> | Open the menu and flush changed SRAM |

</details>

<details>
<summary>📂 Menu & ROM Browser</summary>

**Menu & ROM browser**

| Button | Action |
|:------:|--------|
| <img src="docs/controls/dpad.svg" height="20" alt="D-Pad"> Up / Down | Move the selection |
| <img src="docs/controls/cross.svg" height="20" alt="Cross"> or <img src="docs/controls/start.svg" height="20" alt="Start"> | Launch the highlighted ROM (or open a folder) |
| <img src="docs/controls/triangle.svg" height="20" alt="Triangle"> | Go up one folder (`..`) |
| <img src="docs/controls/square.svg" height="20" alt="Square"> | Page up — *or swap the cover image when cover art is on (see below)* |
| <img src="docs/controls/circle.svg" height="20" alt="Circle"> | Page down |
| <img src="docs/controls/select.svg" height="20" alt="Select"> | File menu (copy / paste / delete) |
| <img src="docs/controls/l1.svg" height="20" alt="L1"> / <img src="docs/controls/r1.svg" height="20" alt="R1"> | Switch screen (Browser ⇆ State Manager ⇆ Network ⇆ Menu ⇆ Log ⇆ Video Config), including while a game is paused. |
| <img src="docs/controls/l2.svg" height="20" alt="L2"> + <img src="docs/controls/r2.svg" height="20" alt="R2"> | Return to the game |

</details>

<details>
<summary>💾 Save States</summary>

**Battery SRAM**

Cartridge battery saves use separate directories inside the emulator's PS2
memory-card save: `mc0:/SNESticle/SNES/<game>.srm` and
`mc0:/SNESticle/NES/<game>.srm`. NES SRAM is enabled only for iNES ROMs whose
header contains the battery flag and stores the complete 8 KiB cartridge RAM.
Opening the in-game menu with **L2 + R2** flushes changed SRAM.
The UI no longer waits for the memory card before entering the menu: the write
is deferred by two visible frames, while the menu-music I/O helper keeps an
already-loaded track feeding `audsrv` during the synchronous card operation.

For backward compatibility, an SNES save missing from `SNES/` is looked up at
the old `mc0:/SNESticle/<game>.srm` location. It is loaded normally and a copy
is written into `SNES/` on the next SRAM save; the old file is never deleted.

**First save-state destination**

The first in-game **L2 + Cross** press opens a small, temporary **Save State
Location** screen. Select **Auto**, **USB**, **Memory Card**, **MMCE**, or
**Internal HDD** and press Cross: the target is remembered, the first state is
saved, and the screen closes back to the game automatically. Press Circle to
cancel. All five choices are always shown, so a removable device can be chosen
as the future default even when it is not inserted during setup. Saving to a
currently unavailable target reports a normal error instead of silently
changing the preference.

Later **L2 + Cross** presses save directly and **L2 + Circle** loads directly.
This temporary selector pauses without flushing SRAM; **L2 + R2** remains the
dedicated menu/SRAM shortcut. The choice is stored in `state.cfg`. Existing
`mc0:/SNESticle/state.cfg` and `mc1:` files retain priority; without a writable
card the emulator can persist it beside a writable standalone ELF, then under
`SNESticle/state.cfg` on `mass0:`/`mass1:`/legacy `mass:` (USB or MX4SIO) or
an enabled MMCE slot. If a disc/ISO boot later opens a ROM from another local
unit such as `mass2:` or an HDD partition, that ROM device is also used and its
choice is recovered as soon as the ROM is opened. CDFS and SMB remain read-only
and are never selected as configuration or state-write targets.

**Auto** first tries the device that supplied the ROM, then the available
`massN:`, `mc0:`/`mc1:` and enabled `mmce0:`/`mmce1:` devices. **USB** covers
USB flash drives, external USB HDD/SSD and MX4SIO devices exposed as `massN:`.
**Memory Card** tries both PS2 slots. **MMCE** tries both MMCE slots when MMCE
support is enabled in Video Config. **Internal HDD** writes to the same mounted
APA/PFS partition as the current ROM. It can be selected as a default at any
time, but an actual HDD save requires the current ROM to have been opened from
an enabled internal-HDD partition. Auto always uses quick-save slot 1; with
only a PS2 memory card available it falls through to that card, preferring
`mc0:`.

If a selected PS2 memory card is present but unformatted, the emulator asks
before formatting it. The safe **No / Cancel** option is selected by default,
and the warning makes clear that formatting erases the entire card. The same
confirmation is used when a changed SRAM needs the unformatted `mc0:` card.

**State Manager**

The regular L1/R1 tab is a file manager available both on the initial homebrew
screens and while a game is paused:

| Option | Action |
|--------|--------|
| **Browse State Files** | Open the state folder for the selected storage device. The separate browser hides unrelated files; press Select to open the file menu and delete a state, or L1 to return. |
| **Storage** | Cycle through `mass0:`, `mass1:`, the legacy `mass:` alias, `mc0:`, `mc1:`, enabled MMCE slots, and the enabled internal HDD. |
| **Quick Slot** | Select quick-save slot 1–5 for an explicit device. Auto stays on slot 1. |
| **Ask Save Location Again** | Forget the current target so the next L2 + Cross asks again. |

Internal-HDD management first opens its APA partition list; enter the desired
partition and then `SNESticle/states`. Each slot has an `a` and a `b` bank;
SNES uses `.sNa`/`.sNb` and NES uses `.nNa`/`.nNb`. Deleting either matching
file from the state browser removes both banks automatically. On a PS2 memory
card these banks remain directly in `mcN:/SNESticle/`, as before; the `SNES/`
and `NES/` subdirectories are for SRAM only.

Each slot keeps two banks. A new bank is committed only after its complete
payload has been written, and every load checks the format version, ROM CRC,
ROM size and payload CRC. If the newest bank is incomplete or corrupt, the
older valid bank is used automatically. New banks use fast deflate compression
and reuse the header scan without rereading the payload after a successful
write, reducing slow device I/O; existing uncompressed version-1 banks remain
loadable.

Save states cover **base SNES hardware** and **iNES cartridge games**. NES
states preserve CPU, RAM, PPU, CHR RAM, pAPU, cartridge SRAM, active bank
mappings, mapper RAM, mapper registers/latches and IRQ counters; bank mappings
are stored as region/offset references instead of process pointers, so a state
remains valid after restarting the emulator. FDS states are not supported.

SNES games using DSP, SuperFX, CX4, OBC1, S‑DD1, S‑RTC or Super Game Boy
hardware are still rejected with an explicit message until those coprocessor
states are serialized.

</details>

<details>
<summary>⚙️ Video Config</summary>

**Video Config screen**

| Button | Action |
|:------:|--------|
| <img src="docs/controls/dpad.svg" height="20" alt="D-Pad"> Up / Down | Select an option |
| <img src="docs/controls/dpad.svg" height="20" alt="D-Pad"> Left / Right | Change its value |
| <img src="docs/controls/square.svg" height="20" alt="Square"> | Reset the screen offset |
| <img src="docs/controls/cross.svg" height="20" alt="Cross"> or <img src="docs/controls/start.svg" height="20" alt="Start"> | Save settings to the memory card |

</details>

---

## 🖼️ Cover art

<details>
<summary>Show details</summary>


The ROM browser can display box art, title screens, gameplay snapshots and
logos next to the game list. Enable **Video Config → Cover Art**, then press ✕
to save the setting. Only **one image is displayed at a time**; installing all
four types makes them available for cycling rather than drawing four images
simultaneously.

### 1. Most important rule: match the ROM name

The PNG filename must match the ROM filename with only its final extension
removed:

| ROM | Required artwork filename |
|-----|---------------------------|
| `Super Mario World (USA).sfc` | `Super Mario World (USA).png` |
| `Super Mario World (USA).zip` | `Super Mario World (USA).png` |
| `Super Mario Bros. (World).nes` | `Super Mario Bros. (World).png` |

Matching is case-insensitive, but every other part of the name matters. For
example, a manually installed `Super Mario World (USA).png` does not match a
ROM named `Super Mario World (U) [!].smc`.

The project uses these artwork collections:

- [Libretro — Super Nintendo / SNES](https://thumbnails.libretro.com/Nintendo%20-%20Super%20Nintendo%20Entertainment%20System/)
- [Libretro — Nintendo Entertainment System / NES](https://thumbnails.libretro.com/Nintendo%20-%20Nintendo%20Entertainment%20System/)

Each collection contains four artwork types recognized by SNESticle Revive:

| Directory | Artwork |
|-----------|---------|
| `Named_Boxarts/` | Game box or cover art |
| `Named_Titles/` | Title screen |
| `Named_Snaps/` | In-game screenshot |
| `Named_Logos/` | Transparent game logo or icon |

### 2. Two supported layouts

For a single image, place the PNG next to the ROM:

```text
ROMS/
├── Super Mario World (USA).sfc
└── Super Mario World (USA).png
```

To use every artwork type for the same game, repeat the matching filename in
all four directories:

```text
ROMS/
├── Super Mario World (USA).sfc
├── Named_Boxarts/
│   └── Super Mario World (USA).png
├── Named_Titles/
│   └── Super Mario World (USA).png
├── Named_Snaps/
│   └── Super Mario World (USA).png
└── Named_Logos/
    └── Super Mario World (USA).png
```

You can also add extra custom images in the ROM directory:

```text
Super Mario World (USA)-1.png
Super Mario World (USA)-2.png
...
Super Mario World (USA)-9.png
```

In the browser, press **□** to cycle through available images in this order:
PNG next to the ROM → box art → title screen → gameplay snap → logo → extras
`-1` through `-9`. Missing types are skipped automatically.

> Libretro replaces the characters ``&*/:`<>?"\|`` with `_` in thumbnail
> filenames. When installing artwork manually, rename the downloaded PNG to
> the exact base name of your ROM. The automatic downloader described below
> handles this conversion for you.

### 3. USB, MX4SIO, MMCE, HDD, memory cards, SMB and CDFS

The directory layout is identical on every device; only the path prefix
changes:

| Device | Example ROM directory |
|--------|-----------------------|
| USB drive, USB HDD/SSD or MX4SIO | `mass0:/ROMS/` or `mass1:/ROMS/` |
| Standard memory card | `mc0:/ROMS/` or `mc1:/ROMS/` |
| MemCard PRO 2 / SD2PSX (MMCE) | `mmce0:/ROMS/` or `mmce1:/ROMS/` |
| Internal APA/PFS HDD | `hdd0:/PARTITION/ROMS/` |
| Read-only network share | `smb:/ROMS/` |
| Disc or ISO | `cdfs:/ROMS/` |

For USB, HDD, MMCE, memory cards or SMB, copy the ROM and the `Named_*`
directories to the device/share using the layout shown above. The browser
indexes the PNG files when entering the directory, avoiding a slow storage
scan on every selection change. See [SMB ROM loading](#smb-rom-loading-replaces-host)
for the required `SMB.CNF` and server settings.

`make covers` and `COVER=y` also generate a small `COVERS.IDX` beside the
ROMs. It lets CDFS and other slow devices load one sequential index instead of
enumerating all four artwork directories. `COVERS.IDX` and the `Named_*`
directories are hidden from the game list. Manual layouts without an index
still work through the compatible directory-scan fallback.

For **CDFS**, first prepare a normal directory on the computer and pass it to
the Makefile. All existing PNG files and subdirectories are preserved under
`cdfs:/ROMS/`:

```bash
make iso ROMS=/path/to/ROMS OUT=/path/to/output
```

Another option is a shared `covers/` directory next to the ELF. It can contain
plain PNG files and/or the four `Named_*` directories. To compile with a
different shared path, use:

```bash
make clean
make COVERS_PATH=mass:/SNESticle/covers
```

The shared path is searched first. The ROM directory and `covers/` next to the
ELF remain available as fallback locations.

### 4. Automatic download with `COVER=y`

When building an ISO, `COVER=y` (or `cover=y`) detects SNES/NES games from the
ROM extension, inspects `.zip` contents when possible and searches Libretro
for all four artwork types:

```bash
make iso \
  ROMS=/path/to/ROMS \
  OUT=/path/to/output \
  COVER=y
```

- `COVER=n` is the default: it never accesses the network.
- Matching tries the exact name, Libretro's `_` character replacement, names
  without trailing tags and common region aliases such as `(U)` → `(USA)`.
- Existing valid artwork is never downloaded again or overwritten.
- Unknown ROMs and games without a match are reported and skipped; ISO
  creation continues.
- PNG files are added only to the temporary ISO tree. The original ROM bytes
  are never changed.
- A compact `COVERS.IDX` is generated automatically to reduce pauses while
  browsing artwork, especially on CDFS.
- Use `COVER_SYSTEM=snes` or `COVER_SYSTEM=nes` to force a system when a `.zip`
  cannot be identified automatically. `COVER_JOBS=6` controls the number of
  parallel downloads.

To prepare a directory for any other device without creating an ISO:

```bash
make covers ROMS=/path/to/ROMS
```

This command creates `Named_Boxarts`, `Named_Titles`, `Named_Snaps` and
`Named_Logos` inside each directory containing ROMs. It adds PNG files only;
ROMs are not renamed, opened for writing or modified. Run it again after
manually adding or removing artwork to refresh `COVERS.IDX`, or delete only
`COVERS.IDX` to make the emulator scan the directories itself.

**Supported formats:** 8-bit or 16-bit RGB/RGBA PNG, grayscale PNG and
1/2/4/8-bit palette/indexed PNG. **Adam7 interlacing is not supported** and is
rejected automatically by the downloader. For manually prepared artwork, save
without interlacing and prefer images around 256 px to reduce memory use and
decoding time.

</details>

---

## 🎵 Menu music & audio

<details>
<summary>Show details</summary>


Background music plays in the ROM browser and the pause menu — tracker modules
in **`.mod`** (Amiga ProTracker) and **`.xm`** (FastTracker II) formats, decoded
on the EE by the bundled official [**libxmp-lite 4.7.2**](https://github.com/libxmp/libxmp/releases/tag/libxmp-4.7.2)
source. It applies the
original ProTracker/FastTracker effect, tempo, instrument and loop rules and
includes the upstream fixes made after the old 4.5.0 PS2 fork, including a
channel-unmute regression that could make notes disappear. Classic MODs also
set the upstream-recommended default pan to 50 before loading; this keeps
hard-left/right instruments audible through imperfect HDMI/TV mono downmix.
This setting affects menu tracker music only, not SNES or NES game audio.

Drop one or more tracks in any of these folders. Memory-card and mass-storage
folders are indexed immediately; enabled MMCE/HDD sources are checked once
through their own device probes. CD/DVD is checked a little later with
non-blocking readiness polls so booting an ISO cannot stall while the drive is
still detecting the disc. Subfolders are supported (up to four levels), and
the resulting index is cached:

- the `BGM_PATH` folder (if you built with one — see below)
- `mc0:/SNESticle/bgm`, `mc1:/SNESticle/bgm`
- `mmce0:/SNESticle/bgm`, `mmce0:/bgm`
- `mmce1:/SNESticle/bgm`, `mmce1:/bgm`
- `mass:/SNESticle/bgm`, `mass:/bgm`
- the first enabled internal-HDD APA/PFS partition containing
  `SNESticle/bgm` or `bgm` (for example `hdd0:/+OPL/SNESticle/bgm`)
- `cdfs:/BGM` (inside the ISO)

MMCE folders are scanned only while **MMCE Cards** is enabled and only on
ports which answer the hardware PING. Enabling MMCE in Video Config triggers a
one-time scan during the current session, so reopening the homebrew is not
required to discover its tracks.

Internal-HDD folders are scanned only while **HDD Support** is enabled. The
driver remains off by default; once enabled, the player enumerates the main PFS
partitions and stops at the first one containing tracks. Playlist entries keep
their full `hdd0:/PARTITION/...` identity, so opening another HDD partition in
the browser does not break the next track.

A **random track** is picked at boot, and a **different one each time you leave
a game** and return to the menu (when more than one track is present).

**Video Config → Audio**

| Option | Range | Notes |
|--------|-------|-------|
| **Game Volume** | 0–100 | Loudness of the emulated SNES/NES audio. **100 = the default** (matches Snes9x); 0 mutes. Applies to both cores. |
| **Menu Music** | Off / 1–100 | Background‑music volume. **0 = Off** — the player isn't loaded and uses no RAM. Shows **Searching** while CD/DVD detection is pending, then **No Track** only when no playable `.mod`/`.xm` is found. |
| **Frequency** | 16–48 kHz | Synthesis rate of the menu music (the output is always resampled to 48 kHz). Higher = better quality but more CPU; **24 kHz** is the default and safest setting for a steady frame rate. |

Menu-side filesystem calls on the PS2 are synchronous. While the browser is
enumerating a large folder, a cover is being read/decoded, a settings file is
being written, or SMB is connecting, a small EE helper thread services only
the already-loaded tracker and `audsrv`; it never opens or scans a filesystem
itself. Modal messages also continue updating the player. The helper sleeps
completely outside those I/O scopes, including during gameplay. Changing the
synthesis frequency restarts the loaded libxmp player in memory instead of
re-reading the module from disk.

All three persist to the memory card (press ✕ to save), and work the same for
SNES and NES (the menu and audio path are shared).

To bake a default tracks folder or synth rate into the build:

```bash
make BGM_PATH=mass:/snes/bgm    # where to look for .mod/.xm first
make BGM_PATH=hdd0:/+OPL/SNESticle/bgm
make BGM_RATE=24000             # 16000/22050/24000/32000/38000/44100/48000
```

When building an ISO, add `bgm=` (or `BGM=`) to bundle a folder of tracks (they
land in `cdfs:/BGM`). The build stops with an error if that folder contains no
`.mod`/`.xm`, which prevents accidentally producing a music-less ISO:

```bash
make iso roms=/path/to/roms bgm=/path/to/tracks
```

> **License:** `libxmp-lite` is MIT licensed. The vendored source, license and
> exact PS2-port revision are under `src/third_party/libxmp-lite/`.

</details>

---

## 💾 Storage & devices

<details>
<summary>Show details</summary>


The ROM browser lists local devices immediately. Optional hardware and network
sources are enabled in **Video Config → Storage / Devices** and initialized
only when selected, so a missing HDD, MMCE card, network cable or DHCP server
cannot stall normal boot.

| Device | What it is |
|--------|------------|
| `mass0:` / `mass1:` | **USB** (the PS2's two ports), USB **external HDD/SSD**, and **MX4SIO** SD cards — all block devices share the `massN:` namespace, numbered in detection order. |
| `hdd0:` | The **internal HDD** (PS2 Fat expansion bay), APA‑partitioned like HDD‑OSD / OPL. |
| `mc0:` / `mc1:` | **Memory cards** — including the original **MemCard PRO** (gen 1), which behaves as a normal card. |
| `mmce0:` / `mmce1:` | **MMCE** carts (**MemCard PRO 2**, **SD2PSX**) via `mmceman`. |
| `cdfs:` | The game/data disc (or the ISO this ELF was burned into). |
| `smb:` | One configured **read-only SMB network share** used for browsing and loading ROMs. |

**Filesystems / partitions:** the bundled **BDM** stack (`bdm` + `bdmfs_fatfs` +
`usbmass_bd`) reads **FAT16 / FAT32 / exFAT** with **MBR or GPT** partition
tables (so drives larger than 2 TB work), mirroring modern OPL. The internal
HDD additionally uses `ps2atad` + `ps2hdd` for the APA `hdd0:` device.

### SMB ROM loading (replaces `host:`)

The original iaddis `host:` entry was a development bridge for
**ps2link/ps2client HostFS**. It let the author load ROMs and IRX files from a
PC while developing, but it was never a normal network share. Its behaviour
depends on the launcher or emulator supplying HostFS; some implementations
return unreliable type metadata and make regular files appear as directories.
For that reason `host:` is no longer shown in the user ROM browser. The
internal direct-ELF/ps2link boot fallback remains available for developers.

SNESticle now embeds PS2SDK's `smbman` and exposes one configured share as
`smb:`. Network modules, DHCP and login start only when you explicitly connect
or select `smb:`; merely booting or opening the setup tab does not touch DEV9.

#### NetherSX2 2.2n+ DEV9 setup

In the screenshot where **API** and **Device** show **Not Set / Não definido**,
the virtual PS2 has no network link and SNESticle SMB cannot reach DHCP or the
server. In NetherSX2 open **App Settings → Settings → System → Networking** and
use:

| NetherSX2 setting | Value for a normal home network |
|--------------------|---------------------------------|
| **Enable DEV9 Ethernet** | On |
| **API** | `Sockets` |
| **Device** | `WiFi` when the Android device and SMB PC/NAS are on the same Wi-Fi/LAN; use `VPN` only when the phone routes the connection through a VPN, or the appropriate `SIM DATA` option for mobile data. |
| **DNS1 / DNS2 Mode** | `Auto (Default)` |
| **DNS1 / DNS2** | `0.0.0.0 (Default)` |

Manual DNS presets in that screen are intended for PS2 online-game servers;
SNESticle uses the numeric **Server IP** entered in its SMB Network tab and
does not need one. After applying the settings, fully restart the emulated PS2,
then configure **Server IP**, **Share**, user/password and port inside
SNESticle. The [official NetherSX2 2.2n release instructions](https://github.com/Trixarian/NetherSX2-patch/releases/tag/2.2n)
document the same DEV9 `Sockets` and device selection.

#### Recommended: configure it on the PS2

Use L1/R1 to open the **SMB Network** tab (the former Host screen), then set:

| Field | Meaning |
|-------|---------|
| **Server IP** | Numeric IPv4 address of the PC/NAS. Press Cross, choose an octet with Left/Right, and change it with Up/Down. |
| **Port** | Cross or Left/Right switches between the usual ports 445 and 139. |
| **Share** | Share name only, such as `roms`; it is not a local filesystem path. |
| **Username** | SMB account, or `GUEST` for a guest share. |
| **Password** | Leave empty for guest access. It is masked on screen. |

For Share/Username/Password, press Cross to edit, Left/Right to move the
cursor, Up/Down to choose a character, Cross to advance/add, Square to delete,
and Triangle to finish. Select **Save & Connect** when done. The emulator
automatically validates and writes `mc0:/SNESticle/SMB.CNF` (falling back to
`mc1:`). If neither card is writable, it tries the writable ELF directory,
`mass0:`/`mass1:`/legacy `mass:` (USB or MX4SIO), then enabled and detected
`mmce0:`/`mmce1:` storage. An ELF launched from an internal-HDD PFS partition
can save beside itself as well. It then enables SMB and attempts the
connection. The exact saved path and a specific connection error are shown on
screen. Circle reloads the saved file.

#### Advanced: create `SMB.CNF` manually

You can instead copy and edit [`SMB.CNF.example`](SMB.CNF.example):

```ini
SERVER_IP=192.168.1.100
SERVER_PORT=445
SHARE=roms
USER=GUEST
PASSWORD=
PASSWORD_TYPE=-1
```

`SERVER_IP` must be a numeric IPv4 address. `SHARE` is the share name, not a
filesystem path. Password modes are `-1` for guest/no password, `0` for legacy
plaintext and `1` to hash the supplied password locally before authentication.
If a non-empty password is supplied without `PASSWORD_TYPE`, mode `1` is used.
The compatible wLaunchELF names `smbServer_IP`, `smbServer_Port`,
`smbUsername`, `smbPassword`, `smbPasswordType` and `smbShare` are also
accepted.

Manual files are searched in this order:

- `mc0:/SNESticle/SMB.CNF` or `mc1:/SNESticle/SMB.CNF`;
- beside a writable standalone ELF;
- `mass0:/SNESticle/SMB.CNF`, `mass1:/SNESticle/SMB.CNF`, then the legacy
  `mass:/SNESticle/SMB.CNF` alias when Mass/USB or MX4SIO support is enabled;
- `mmce0:/SNESticle/SMB.CNF` or `mmce1:/SNESticle/SMB.CNF` for enabled MMCE
  slots that answer the hardware probe;
- the compatible shared `mc0:/SYS-CONF/SMB.CNF` or
  `mc1:/SYS-CONF/SMB.CNF` fallback;
- the root of a disc/ISO as `cdfs:/SMB.CNF`.

The on-console setup updates the writable file it loaded, or creates only an
emulator-owned `SNESticle/SMB.CNF` fallback. It never overwrites a shared
wLaunchELF file under `SYS-CONF`. Emulator-owned files on local storage take
priority over shared/read-only bundled files, so changing a server does not
require rebuilding the ISO.

For an ISO build, it can be copied to the root automatically:

```bash
make iso ROMS=/path/to/roms SMB_CONFIG=/path/to/SMB.CNF
```

For a manual file, enable **Video Config → SMB (Network)**, save with Cross and
open `smb:` in the browser. The **Save & Connect** action in the SMB Network
tab enables that option automatically. Statuses such as **No SMB.CNF**,
**DHCP Timeout**, **Auth Error** or **Share Error** identify a failed stage.

The browser deliberately disables copy, paste and delete on `smb:`. ROMs,
ZIPs and their PNG artwork are read from the share; SRAM and save states still
go to the configured local memory-card/USB destination. Configure the server
share itself as read-only too. A minimal Samba share is:

```ini
[roms]
    path = /srv/ps2-roms
    browseable = yes
    read only = yes
    guest ok = yes
```

> `smbman` implements **SMB1/NT1**, not SMB2/3. If the server requires it,
> `server min protocol = NT1` is a global Samba setting. SMB1 is obsolete and
> unsafe on an untrusted network: use a dedicated read-only share on an
> isolated/trusted LAN and never expose it to the internet. An emulator also
> needs working DEV9/SMAP Ethernet emulation; providing HostFS alone is not
> enough.

> **Build note:** the complete USB group (`usbd_mini`, `bdm`,
> `bdmfs_fatfs`, `usbmass_bd`) and SIO2 group (`sio2man`, memory-card,
> pad/multitap, MMCE and MX4SIO) are pinned under [`irx/`](irx/). A build no
> longer changes USB behaviour according to whichever PS2SDK happens to be
> installed. USB uses the FreeUsbd-based `usbd_mini` selected by OPL for its
> BDM loader; the browser also retries a selected `massN:` for up to three
> seconds while a slow drive finishes mounting. Internal-HDD modules remain
> supplied by PS2SDK because they are loaded only when HDD support is enabled.
> MMCE slots are listed only after a successful hardware PING.
>
> MMCE and MX4SIO both hook SIO2 and are therefore mutually exclusive. Turning
> one on turns the other setting off. If the opposite driver is already
> resident, Video Config shows **Restart** and applies the change safely on the
> next boot.
>
> Each storage module prints its load result on the boot splash
> (`bdm.irx = 0`, `hdd (hdd0:) = N`, …), so a failure is visible in a photo of
> the screen. On a console without an internal HDD the `dev9`/`hdd` probe just
> reports "no hardware" and boot continues — it does not hang.

</details>

---

## 🔨 Building (PlayStation 2)

<details>
<summary>Show details</summary>


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
| `COVER=y` / `cover=y` | Download matching Libretro boxart/title/snap/logo into the ISO; `n` is the offline default. |
| `COVER_SYSTEM=auto` | Detect SNES/NES automatically; use `snes` or `nes` to override ambiguous archives. |
| `COVER_JOBS=6` | Number of parallel thumbnail downloads. |
| `PACK=0` | Build the ISO using the unpacked ELF. |
| `COVERS_PATH=path` | Shared cover‑art folder baked into the build (e.g. `mass:/snes/covers`). See [Cover art](#-cover-art). |
| `BGM_PATH=path` | Folder scanned first for menu‑music `.mod`/`.xm` files. See [Menu music & audio](#menu-music--audio). |
| `BGM_RATE=hz` | Default menu‑music synthesis rate (e.g. `32000`). |
| `SMB_CONFIG=/path/SMB.CNF` | Copy a single-share SMB configuration into an ISO root without printing its credentials. |

> Note: changing a flag like `PROFILE=1` does **not** force a recompile on its
> own (make only tracks file timestamps). Run `make clean` first when toggling
> compile flags.

</details>

---

## 📝 What's been done recently

<details>
<summary>Show details</summary>


The cumulative notes for the current test version are available in
[`CHANGELOG_v1.0.4.md`](CHANGELOG_v1.0.4.md).


- **Coprocessors**: added DSP‑1, DSP‑2, CX4, OBC1, S‑DD1 and S‑RTC, each
  written clean‑room and verified bit‑exact host‑side against public references.
  **DSP‑4** (Top Gear 3000) is **HLE / self‑contained** (no external files): the
  bus protocol plus the full track‑projection math come from the **ZSNES** DSP‑4
  HLE (GPLv2, © ZSNES Team — zsKnight, _Demo_, pagefault, Nach), ported with
  attribution. Incorporating that GPLv2 code is why the project was relicensed
  from MIT to **GPLv2**.
- **NES (InfoNES) integration**: full PS2 platform layer (render, input, audio,
  one‑frame stepper). The five base 2A03 channels now use Shay Green's
  cycle-timed **Nes_Snd_Emu + Blip_Buffer** at 32 kHz, and video uses Mesen2's
  default NTSC 2C02 palette instead of the old saturated RGB555 table.
- **Video**: gsKit migration, the Video Config screen, multiple modes, and a
  **safe 480i default** (native 256x240 stays available for CRT users).
- **Cover art**: the ROM browser shows custom images plus Libretro boxart,
  title screens, gameplay snaps and logos from PNG files, with `-1` through
  `-9` manual extras. Decoded covers are cached and neighbours prefetched, so
  browsing stays smooth even from a CD. `make iso ... COVER=y` fetches all
  matching art into CDFS without touching the source ROMs; `make covers
  ROMS=...` prepares the same `Named_*` layout for any other device.
- **ROM browser**: switched CDFS, USB, memory cards, SMB, MMCE and PFS/HDD to
  direct directory records, eliminating the per-file `stat` round trip that
  made large CDFS folders especially slow. iomanX-normalized `FIO_S_*` mode
  bits identify directories consistently on every device; entries from unusual
  third-party drivers that report no type are checked by full path. A streaming
  fork of PS2SDK's CDFS driver removes its fixed 256-entry ISO table, the EE
  entry array grows on demand, and the list/teal footer have separate geometry
  so long directories never overwrite the status text.
- **Menu music & audio controls**: tracker music (`.mod` / `.xm`) plays in the
  ROM browser and pause menu via the PS2 port of **libxmp-lite**, decoded on the
  EE and continuously resampled to the SPU2's 48 kHz. Added **Game Volume**,
  **Menu Music** volume (0 = off, frees its RAM) and a synthesis **Frequency**
  picker in Video Config — all persisted, shared by SNES and NES. A random
  track plays at boot; returning from a game resumes the loaded decoder without
  a disk reload, and the playlist advances when a track completes. A sleeping
  EE helper prevents directory, cover, settings and SMB I/O from starving the
  menu stream.
- **Storage**: a pinned **BDM** stack with OPL's FreeUsbd-based `usbd_mini`
  replaces the old single‑USB path — two USB ports, external HDD/SSD and
  MX4SIO all appear as `mass0:`/`mass1:`, reading FAT16/FAT32/exFAT with
  MBR/GPT. Slow USB media receives a bounded mount retry. Added the internal
  HDD (`hdd0:`, APA) and MMCE carts (`mmce0:`/`mmce1:`, MemCard PRO 2 / SD2PSX).
  The unreliable user-facing `host:` device was replaced by a lazy, read-only
  `smb:` ROM share with bounded DHCP/login errors and correct file types.
  See [Storage & devices](#storage--devices).
- **Boot / input**: controller and IRX bring‑up reworked to behave on real
  hardware, not just emulators. Direct ELF boot also tolerates launchers that
  omit the executable path instead of crashing before video initialization.
- **Save states**: restored the dormant iaddis-era feature as a release menu
  with five slots, USB/memory-card selection, versioned files, ROM/CRC checks
  and power-loss-safe two-bank writes; v1.0.4 also serializes the NES CPU, PPU,
  pAPU, CHR RAM and complete mapper-private state.
- **Build system**: parallel jobs, `VERBOSE`, `PROFILE`, friendlier `make help`,
  and ISO builds that honor `JOBS`.
- **Bug fixes**: C++17 / build warnings cleaned up, plus three real
  out‑of‑bounds bugs fixed in the InfoNES core (`APU_Reg`, mapper 19 & 45 arrays)
  and a sequence‑point UB fixed in the 6502 core.

</details>

---

## 🐞 Known issues / still missing

<details>
<summary>Show details</summary>


**SNES**
- Save states currently support base-hardware games only; coprocessor games
  are blocked until each extra chip has complete serialization.
- **Final Fight 2** — correcting the rectangular OBSEL 6/7 sizes was necessary
  but did not fix the reported gameplay scene by itself. r17 removes a GIF-DMA
  race that let the GS read a scanline while the CPU reused its source buffer,
  preserves DMA mode phase across a 64 KiB bank wrap and aligns mirrored OBJ
  tile-fetch order with the 34-tile hardware limit. r18 additionally fixes the
  65816's 24-bit bus wrap after official vectors exposed indexed reads past
  `$FF:FFFF` landing in an empty page. OAM/VRAM burst paths and the CPU wrap
  have host coverage. r19 audits every 65816 opcode in emulation/native mode,
  fixes stack/direct-page/decimal/interrupt corner cases and aligns DMA/HDMA
  timing with MesenCE/Mesen2. The original scene still needs a PS2/NetherSX2 retest
  before it is considered fixed.
- **First Samurai / Final Fight 3** — r20 replaces the old low/high-pair
  approximation for `$210D-$2114` with the S-PPU's separate horizontal,
  shared H/V and Mode 7 latches. The r21 deep capture then exposed the direct
  cause of First Samurai's mosaic: mode-4 MDMA queued `$2116/$2117` address
  writes but applied `$2118/$2119` data immediately, so tile words landed at a
  stale VRAM address. MDMA PPU-port writes now retain transfer order and have a
  focused host regression test. The affected scenes still require a
  PS2/NetherSX2 visual retest before being marked fixed.
- **Wild Guns** — r19 implements the documented 24-clock NMI delay after MDMA
  and corrects the HDMA state machine/mode 5. The full-screen flicker reported
  after character selection still needs confirmation on the same NetherSX2
  setup before the issue is marked fixed.
- Some large / special‑chip titles may still freeze or misbehave.
- **SuperFX (GSU)** is experimental in v1.0.4: r15 corrects cache-window
  rotation, executable RAM banks `$60-$7F`, byte MMIO and the hot loop, but
  Star Fox/Yoshi and other boards still need game-by-game PS2 validation.
- **Missing chip**: SA‑1 is not implemented.

**NES (InfoNES)**
- Save states currently cover `.nes` cartridges; FDS state serialization is
  not available.
- **Performance**: heavy scenes can push a frame over the 16.6 ms budget, which
  vsync then locks to **30 fps**; this also knocks audio and per‑scanline
  effects out of sync. (Use `PROFILE=1` + R3 to locate hotspots.)
- **Super Mario Bros 3** — the MMC3 status‑bar split can glitch when scrolling
  (InfoNES uses a scanline‑approximated MMC3 IRQ, not A12‑accurate).
- The five base 2A03 channels are implemented. Expansion audio used by some
  Japanese/mapper-specific releases (VRC6, VRC7, MMC5, FDS and Sunsoft 5B) is
  not yet connected, so those releases can still miss instruments.

**Video**
- The Video Config screen exposes only **480i** and **1080i**. Legacy settings
  saved as 240p/288p or 480p are migrated automatically to 480i.

> Some bugs only reproduce on **real PS2 hardware** (emulators like NetherSX2 /
> PCSX2 are more forgiving), which makes them harder to track down.

</details>

---

## 📂 Project layout

<details>
<summary>Show details</summary>


```
src/snes/      SNES core (cpu, spc, ppu, coprocessors)
src/nes/       NES core (InfoNES: core, cpu, apu, mappers, system)
src/platform/  PlayStation 2 platform (gs, system, input, ui)
src/modules/   shared modules (audio, netplay, ...)
src/common/    shared helpers (render, base, io, debug)
tools/         host‑side test harnesses (chip + OBJ verification)
```

</details>

---

## ❤️ Credits

<details>
<summary>Show details</summary>


- **[iaddis/SNESticle](https://github.com/iaddis/SNESticle)** — Icer Addis, the original emulator.
- **[nesdev-org/MesenCE](https://github.com/nesdev-org/MesenCE)** — current
  Mesen2-derived reference for 65816, interrupt and SNES DMA/HDMA behavior
  used by the r19 audit.
- **[SingleStepTests/65816](https://github.com/SingleStepTests/65816)** —
  complete per-opcode state, memory and bus-cycle vectors used by
  `tools/cputest`.
- **[ZSNES Team](https://www.zsnes.com)** — zsKnight, _Demo_, pagefault, Nach; their GPLv2 DSP‑4 HLE (`chips/dsp4emu.c`) is ported here as `src/snes/core/dsp4emu.*` (Top Gear 3000 support).
- **[tmaul/SNESticle](https://github.com/tmaul/SNESticle)** — many later improvements.
- **[Wolf3s/SNESticle](https://github.com/Wolf3s/SNESticle)** — fork used as one of the bases for this repository.
- **Sardu** — for releasing the recovered source under the MIT license (2022).
- **[jay-kumogata/InfoNES](https://github.com/jay-kumogata/InfoNES)** — the NES core integrated here.
- **[game-music-emu](https://github.com/libgme/game-music-emu)** — Shay Green's Nes_Snd_Emu and Blip_Buffer used for the five cycle-timed base NES audio channels (LGPL-2.1+).
- **[Mesen2](https://github.com/SourMesen/Mesen2)** — reference/default NTSC 2C02 palette used by the NES renderer.
- **[upng](https://github.com/elanthis/upng)** — Sean Middleditch & Lode Vandevenne; the bundled single‑file PNG decoder used for cover art (zlib license). Extended in this repo with palette/indexed support.
- **[libxmp-lite](https://github.com/libxmp/libxmp)** — Claudio Matsuoka and Hipolito Carraro Jr; official 4.7.2 embedded MOD/XM replay source used for tracker effects, timing and loops (MIT). The earlier PS2 integration by tatokis was the original porting reference.
- **[hugorsgarcia/PS2SNESticle](https://github.com/hugorsgarcia/PS2SNESticle)** — **Hugo Garcia**, whose PS2 work was the reference for the controller / memory‑card / IRX bring‑up and the netplay module.
- **Open‑PS2‑Loader**, **picodrive‑PS2** and **uLaunchELF** — references for correct PS2 boot, IOP and video behavior.
- **ReyFxck** — this revival/fork and ongoing development.
- **Adriano Oliveira** — real‑hardware testing.
- **Control‑prompt icons** (`docs/controls/*.svg`) — original SVGs drawn for this repo; reuse freely.


</details>

---

## 📜 License

<details>
<summary>Show details</summary>


**GNU GPL v2** — see [`LICENSE`](LICENSE).

The original SNESticle source (Icer Addis, 2022) was MIT‑licensed; the MIT
permits relicensing, so this fork distributes the combined work under GPLv2 in
order to incorporate the ZSNES DSP‑4 HLE (GPLv2). The original MIT notice for
Icer Addis's portions is preserved verbatim inside [`LICENSE`](LICENSE).

- Copyright (c) 2022 Icer Addis (iaddis) — original SNESticle source
- Copyright (c) 2026 ReyFxck — SNESticleRevive fork
- DSP‑4 HLE (`src/snes/core/dsp4emu.*`): © 1997–2008 ZSNES Team (GPLv2)

</details>

---

# Yes, we still have a lot of free time :)
