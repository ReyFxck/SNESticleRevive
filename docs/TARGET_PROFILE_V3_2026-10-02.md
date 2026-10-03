# Target profiler v3

The supplied 2608 screenshot reports CORE 48.9 ms and PPU 5.2 ms, SPC 0.7 ms, MIX 1.7 ms. These inclusive line-render and APU measurements do not assign most of the core time. They cannot establish that Mode 7, sprites, or audio transport dominates the frame.

With `SNES_TARGET_PROFILE=1 SNES_DIAGNOSTICS=0`, the optional HUD now measures the active CPU executor, MDMA, HDMA, renderer BeginRender and EndRender separately. CPU is inclusive of memory/register traps and the rendering/APU work they request; DMA includes register side effects. Existing OBJ belongs to PPU, FETCH/BG/OUT are renderer stages, and IOP belongs to MIX when enqueue runs there. Rows cannot be summed as disjoint costs. Values remain mean wall time per presentation iteration using EE Count at 294.912 MHz. No CPU opcode counter or per-pixel probe is added.

The eight-row panel ends at logical line 216 to remain inside the 224-line SNES aperture. The normal build compiles out all timer reads and accumulations. This is diagnostic attribution, not a performance improvement or proof that any crash is fixed.

Validation: PS2 cross-build passed (166 source files, no errors). Normal-build disassembly and host targetprofile test are checked with the comparable audio builds. Physical PS2 measurements remain pending.
