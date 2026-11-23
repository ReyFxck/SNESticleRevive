# Real PS2 diagnostic file logging

`SNES_DIAGNOSTICS=1` and `SNES_DIAGNOSTICS=2` builds expose runtime diagnostic
file logging for real PS2 hardware. `SNES_DIAGNOSTICS=0` keeps the menu row
visible but disabled and does not allocate the staging buffer.

## Destination policy

There is exactly one possible destination: `emulog.txt` in the directory that
launched the current ELF.

Examples:

- `mass0:/APPS/SNESticle/SNESticle-diag2.elf`
  -> `mass0:/APPS/SNESticle/emulog.txt`
- `mmce0:/APPS/SNESticle/SNESticle-diag2.elf`
  -> `mmce0:/APPS/SNESticle/emulog.txt`
- `host:SNESticle-diag2.elf`
  -> `host:emulog.txt` when HostFS is writable.

The logger never scans for another storage device and never falls back to a
different path. `mc:/`, `mc0:/`, `mc1:/`, `cdfs:/`, `cdrom*:` and BIOS ROM
origins are explicitly blocked. If the exact ELF origin cannot be reopened or
is not writable, enabling file logging fails and no alternative path is tried.

For real storage namespaces the logger also verifies that the exact launching
ELF path still resolves after SNESticle's IOP reset. This is important for the
shared `massN:` namespace: it prevents a stale unit number from silently
creating the log on another mounted device.

## Buffered streaming

`DLog()` itself never performs filesystem I/O. While logging is On it only
copies complete diagnostic lines into a 512 KiB EE-RAM staging buffer.

When at least 64 KiB has accumulated, `MainLoopProcess()` performs one large
write after frame/profiler accounting. The file is kept open and the explicit
stdio buffer is disabled, so each pump is a real block write instead of many
small writes. The final partial block is flushed when logging is switched Off
or when the application exits normally.

This design gives two useful properties:

1. normal diagnostic timing is not polluted by a filesystem call on every
   `DLog()` record;
2. most of the log already exists on storage if the console crashes, with at
   most the current unflushed block/staging tail at risk.

If one frame somehow generates more than the 512 KiB staging capacity before
the next pump, excess bytes are counted as dropped and reported when logging
is stopped.

## Usage

1. Build/use an ELF with `SNES_DIAGNOSTICS=1` or `SNES_DIAGNOSTICS=2`.
2. Open **Settings -> Performance -> Diagnostic File Logging**.
3. Change **Off** to **On**.
4. The UI shows the exact path that will receive `emulog.txt`.
5. Play/reproduce the problem.
6. Return to the option and change it back to **Off**.
7. Copy `emulog.txt` from the same directory as the ELF.

The existing EE-SIO diagnostics remain unchanged, so NetherSX2 / PCSX2 and
ps2link logging continue to work in parallel.
