# SNESticle Revived

Revived source code of **SNESticle**, the long-rumored **Super Nintendo Entertainment System emulator** created by **Icer Addis**.

This project focuses on restoring and cleaning up a **circa-2004** build of SNESticle.  
The emulator later became known for being hidden inside the **GameCube** version of **Electronic Arts' Fight Night Round 2 (2005)**, where it was used to run **Super Punch-Out!!**.  
Years later, that buried build was reverse engineered and extracted by the community in **2022**, which helped preserve an important piece of emulation history.

This repository is an attempt to keep that code alive, reorganize it, fix broken parts, and make it easier to build and study today.

## Current State

- Many files from the recovered codebase were missing, incomplete, or corrupted.
- Because of that, some features are still broken or may not behave correctly yet.
- The original source layout was messy, so this repository reorganizes the project into more logical directories.
- Right now, the main focus is **PlayStation 2** support.
- Development is currently being done in a **Debian environment running through Termux**, without access to a full desktop setup.

## PlayStation 2 Build

You need **PS2SDK** installed first.  
Follow the installation instructions from the [ps2dev repository](https://github.com/ps2dev/ps2dev.git).

> **Warning:** use the latest available PS2SDK version.

After that, go to the project directory and build it:

```bash
cd /SNESticleRevive
make

This should generate:

SNESticle.elf

To remove previous build files before rebuilding:

make clean
```

## Credits

[iaddis/SNESticle](https://github.com/iaddis/SNESticle) — original project

[tmaul/SNESticle](https://github.com/tmaul/SNESticle) — many later improvements

[Wolf3s/SNESticle](https://github.com/Wolf3s/SNESticle) — improved fork used as one of the bases for this repository


TODO

[PS2] Remove precompiled IRX modules and replace them with PS2DEV-generated ones. [Hard]

[PS2] Replace libcdvd with the latest PS2DEV libcdvd implementation. [Medium]

[PS2] Update the Makefile for newer PS2SDK versions. [Medium] (In progress)

[PS2] Remove some custom GS implementations and switch to gsKit where possible. [Medium]

[PS2] Port the codebase to C89. [Ongoing]


# Yes, we have a lot of free time :)
