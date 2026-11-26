# Round 14 — Mode 1 BG dependency and mask cache hot paths

Incremental patch against the original source plus Rounds 7, 8, 9, Ultra 12 (includes 10/11), and 13. No changes to boot/HostFS, audio, OBJ renderer, GS packet format, VU0, or VU1.

- Keep the exact row-dependency stamp across unrelated VRAM writes when the conservative entire Mode 1 BG map/CHR region has not changed. Only eligible 33-cell, register-aligned 2/4bpp Mode 1 layouts use this shortcut; 16x16 CHR coverage includes the extra quadrant page, including VRAM wrap. If a region changes, walk the exact dependencies again. Preserve existing memo state/config and epoch wrap checks. Include tile count in the memo match. Coarse region generation is cached per BG per VRAM epoch by the existing helper. Cost may increase in workloads with frequent relevant writes; compare diag2 validate cycles before/after.
- Classify full opacity across all 264 meaningful carrier bits (including fine-X tail). Restore a full 256-bit opacity mask directly rather than shifting. Skip mosaic processing only when both restored opacity and priority masks are uniform, preserving mixed priority and partial-tail cases. No priority or window semantics changed.
- CI's existing test_hotpath.sh now also runs vramstamp_test.cpp, with independent unconditional exact dependency walk comparisons, changed map references, count and epoch wrap tests.

## Verification
- 12,288 dependency comparisons (plus same number of fine-X/Y memo checks), 3,570 simulated writes outside entire region; existing coverage, configuration and wrap tests pass. The reference is the existing exact row walker, not hardware.
- 768 new uniform/partial/mixed mask lines per configuration, three configurations (2,304 lines); independent per-pixel opacity and priority oracle. Existing hotpath tests pass, including 4,096 Mode 7 BG1/EXTBG compositions per configuration, HDMA/CGRAM, scroll/matrix, OAM and mosaic.
- 22 portable regression executables passed, ROM Lab portable build passed, final test_hotpath.sh passed after adding CI coverage, git diff --check clean.
- No PS2DEV compiler or PS2 runtime in this environment; native build, visual output, and real performance are unverified. No claimed FPS improvement. Compare [snes-bg-line-cost] avg validate/restore/store and [snes-frame], [ps2-cadence] on identical Mode 1 scenes, using the same diagnostic level. Use release for gameplay. Keep Round 13 artifacts until the boot issue is diagnosed.
