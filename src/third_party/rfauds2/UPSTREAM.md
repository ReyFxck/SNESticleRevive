# RFAuds2 transport snapshot

Source: https://github.com/ReyFxck/RFAuds2/tree/9c5e5b4fe48e7f1afd89c5dde12325d8cd0f6076
MIT license, preserved in LICENSE. The public headers, EE client and IOP sources are copied without edits. The resampler/bus mixer is not linked: SNESticle continues using its existing audio conversion and DSP.

The embedded `irx/rfauds2.irx` was compiled from these IOP sources with GCC 15.2, PS2SDK source rules 4996c6f63d42d2cf5d68dfa2f7bb74b9e3c653f5. SHA-256: bd2b4324c093c7262c5a0b7d653488f8da542796740317b1703cb90d0fc1c859.

Rebuild with `tools/build-rfauds2-irx.sh` after setting PS2DEV, PS2SDK, PS2SDKSRC and PATH to the IOP toolchain. The installed SDK alone may omit the IOP source build rules. The default application build embeds the pinned binary, as it does for its storage drivers; RFAUDS2_IRX_PATH remains overridable. No download happens during a build.

This is an experimental backend candidate. Host RPC tests cannot certify SPU2/DMA/interrupt behavior on a physical FAT or Slim.
