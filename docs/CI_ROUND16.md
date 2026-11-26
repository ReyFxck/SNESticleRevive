# Round 16 — three CI deliverables, shared dependencies

Apply on top of Round 13 or later. Only CI scripts and configuration change.

The workflow uses one Ubuntu 24.04 runner, preserving all portable regression
checks and all three linked-ELF CPU/echo/voice tests from the previous workflow.
It compiles three independent variants, with a clean object directory between
them, and uploads only these three packed ELFs:

- SNESticle-release / SNESticle.elf (diagnostics=0, target profile=1)
- SNESticle-diag1 / SNESticle-diag1.elf (diagnostics=1, target profile=0)
- SNESticle-diag2 / SNESticle-diag2.elf (diagnostics=2, target profile=0)

The VU0 shadow and boot-recovery switches still exist locally, but are explicitly
off for all three CI builds. Unpacked and symbol ELFs are internal intermediates,
not artifacts. Historical workflow artifacts are not deleted.

The PS2DEV cache key is stable across source changes, with a fallback to v2.
Python's environment cache is keyed by Ubuntu, architecture, Python version and
the pinned requirements file. Restored files are checked before reuse; installs
are conditional. Save caches before regression tests so failures after setup do
not discard successful dependency setup. An apt refresh is only attempted if
building a missing packer and the zlib link probe fails.

GitHub-hosted runners are ephemeral: a cache miss, eviction, new dependency
version or invalid environment can still require installation. A broken exact
cache can be invalidated by incrementing its key or deleting it in Actions.
PS2DEV is still sourced via the project's existing install target (latest archive
on a cold cache); this patch does not pin a new SDK revision or change code.
The variants are sequential, with compilation parallelism inside each one.
Wall-clock duration depends on runner resources/cache transfer; no benchmark of
actual CI time is claimed. No toolchain or game binary is bundled in this patch.
