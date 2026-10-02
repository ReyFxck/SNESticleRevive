# Linked PS2 assembly checks

These checks execute the assembly from an **unstripped PS2 ELF**. They do
not measure physical R5900 caches, GS waits, audio playback or FPS.

Install `unicorn==2.1.4` and `pyelftools` in a Python virtual environment,
then run from the repository root (host GCC is needed for the C fixture):

```
python tools/cputest/check_ps2_cpu.py build/SNESticle.elf
python tools/cputest/check_ps2_echo.py build/SNESticle.elf
```

The CPU check compares all 256 opcodes in the five E/M/X modes in both
plain and SA-1 host interpreter images. It also tests empty budgets,
multiple instructions and 8 KiB/64 KiB PC boundaries, comparing registers,
cycle counters, signals, open bus and the entire writable memory image.
SA-1 arbitration is disabled in this fixture; its separate suite covers
that scheduling. Decimal arithmetic helpers use the same C implementation
on both sides. Agreement is a regression check, not proof that every C
interpreter behavior is hardware accurate.

The echo check models the actual linked integer/MMI instructions and
branch delay slots. It compares signed main/echo volumes, extreme int32
mix values, sample ordering, saturation and destination guards against
`clamp((clamp(main >> 7) * mainVolume + echo * echoVolume) >> 6)`.
The scalar contract intentionally includes the existing PS2 main clamp;
it does not assume that the portable legacy echo loop is equivalent.
