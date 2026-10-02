# Save-state HDD selection independent of ROM origin

The Save States storage picker already allows HDD browsing when its option is enabled. However, the quick-state root resolver accepted HDD only when the ROM path started with hdd0: or pfs0:. A USB/MC/MMCE ROM could not use an already mounted writable PFS partition after the user selected HDD storage.

HddIsMounted now exposes the runtime's existing enabled/loaded/mounted state without starting modules, changing a partition or issuing filesystem calls. The state resolver can use that existing pfs0: for a non-HDD ROM. An hdd0: ROM still remaps its original partition through HddMapPath, preserving the previous behavior if the browser has since visited another partition. A direct pfs0: root must still be mounted; a disabled, failed or reset driver is not treated as writable.

Mounting remains in the existing browser/BGM paths, with FIO_MT_RDWR. No arbitrary partition is chosen by saving. CDFS, SMB and host paths are not added as state-write targets; bank format, SRAM, serialization and fallback order are unchanged. The storage picker can still enter hdd0: before a partition is mounted, allowing the user to choose one.

Validation: exact extracted production resolver/query functions pass a private host fixture covering side-effect-free queries, USB with an existing selected partition, disabled/unloaded/reset states, hdd0: ROM-origin remap and failure, and direct PFS roots. PS2 normal cross-build passes. Physical writable HDD/partition selection still needs validation; this does not complete every state-storage/serialization feature.

Other discovery: the explicit picker lists mass0, mass1 and the mass alias; Auto supports mass2+ from the ROM origin. Enumeration of every independently connected mass unit remains a separate issue. Chips without complete saved state remain unsupported rather than producing an incomplete state.
