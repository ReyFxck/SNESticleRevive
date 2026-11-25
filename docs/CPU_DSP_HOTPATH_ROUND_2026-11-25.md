# CPU/DSP hot-path round

Baseline: the preceding font and PPU hot-path patches. The latest SMW diag2 capture has 267 windows / 8,010 frames, 3,615,125 guest cycles/frame (81.58 FPS core capacity), 58.46 FPS guest cadence and 201 intervals over 150% of the 60 Hz budget. These are guest counters, not Android display FPS. At nominal 294,912,000 Hz, 120 FPS core capacity would require 2,457,600 cycles/frame, approximately 32% less than this baseline. Do not change the actual SNES speed, presentation target, frameskip, audio rate or timing to achieve this goal.

## Changes

- Ordinary-cartridge R5900 interpreter: do not reconstruct logical bus addresses on cached opcode and operand fetches merely for the SA-1 recorder that is compiled out. Move the final cycle charge into existing return/dispatch delay slots. Remove the redundant add-zero instruction from zero-tail opcode endings. Slow/trapped fetch and bank-wrap paths are unchanged. The shared SA-1 assembly preprocesses to exactly the same instructions as before.
- DSP: at the native 32 kHz rate, pitch scaling is the identity (including PMON); retain the existing multiply/divide for other rates. A one-tap FIR fast path skips seven zero-coefficient products per stereo side while preserving the entire doubled echo history, address and feedback. Skip accumulation only if both voice volumes are zero, **after** updating envelope, sample phase, PMON and OUTX. Cache the synchronized FLG/NOV registers per mix block. Preserve historical arithmetic, rounding and clamping. Scalar PS2 stereo voice assembly fills two wasted instruction slots without changing arithmetic.
- CI: add mixer golden checks, SA-1 and GSU host suites, and a linked scalar voice instruction model alongside the existing linked CPU/echo checks. No new runtime chip optimizations or layout changes for SA-1 or GSU.

## Local verification

- Host CPU built-in open-bus, 24-bit wrap, interrupt and interruptible MVN checks passed. External opcode vectors were not supplied (0 vector tests).
- Generic Clang MIPS64r2 N32 assembly of the plain interpreter succeeded. `.text` shrank from 48,120 to 46,832 bytes (1,288 bytes). This is a code-size observation, not an EE cycle benchmark. The shared SA-1 preprocessed code is identical; generic Clang rejects six existing `sltiu ...,0x8000` instructions in that shared image, so the real PS2DEV assembler and existing linked CPU test are still required. The shared image is not changed by this patch. Review these immediate comparisons when working on SA-1; no semantic repair is bundled here.
- DSP golden hashes match the preceding version at 32/44.1/48 kHz for both original and extended 256 PMON-mask scenarios. Extended checks alternate full and one-tap FIR, zero volume, EDL zero, key transitions and PMON. All six hashes passed.
- Actual generic MIPS voice object: 2,048 randomized instruction-model checks passed including signed volume, accumulator overflow, zero/negative sample count and memory bounds. Linked PS2 ELF check is also wired into CI. This model does not validate EE timing or hardware audio.
- SA-1 host suite passed; state serializer compiled. GSU host suite passed including 17,960 fuzz cases; bitplanes 98,304 cases passed. These are not full-game or linked-chip tests.
- No PS2DEV toolchain or actual PS2/NetherSX2 runtime available locally. New FPS not yet measured; check CI then compare matching scenes and test release with HUD off.

## Next test matrix

Use owned ROM dumps or public homebrew test ROMs, record game CRC, settings, build commit, start state and inputs. Ordinary: SMW (overworld, stages, transitions), Mode 7 (F-Zero / Super Mario Kart), transparency/HDMA, hires/interlace, PAL. SA-1: Super Mario RPG, Kirby Super Star, Kirby's Dream Land 3. GSU: Star Fox, Yoshi's Island, Stunt Race FX. Test audio, sprite/priority correctness, save/load, controls and stability, record core capacity, worst windows and cadence. Do not infer SA-1 or SuperFX performance from SMW. Use the same starting state/inputs when comparing builds and separate guest timing from host wall-clock performance.
