#ifndef MESENCE_BRIDGE_H
#define MESENCE_BRIDGE_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* The C boundary keeps the C++20 NES core out of the legacy frontend ABI. */
typedef struct MesenceCore MesenceCore;
typedef void (*MesenceAudioSink)(void *, const int16_t *, const int16_t *, uint32_t);
MesenceCore *MesenceCreate(void);
void MesenceDestroy(MesenceCore *core);
int MesenceLoad(MesenceCore *core, const void *rom, uint32_t bytes);
void MesenceUnload(MesenceCore *core);
int MesenceReset(MesenceCore *core, int hard);
int MesenceFrame(MesenceCore *core, const uint8_t pads[2], uint32_t *pixels,
                  uint32_t pitch, MesenceAudioSink audio, void *context);
uint8_t *MesenceSram(MesenceCore *core, uint32_t *bytes);
uint32_t MesenceFrameCount(MesenceCore *core);
uint32_t MesenceFrameRate(MesenceCore *core);
/* Snapshot buffers are allocated on request, never as a graphics cache. */
int MesenceSnapshot(MesenceCore *core);
int MesenceAllocateState(MesenceCore *core, uint32_t bytes);
uint8_t *MesenceStateData(MesenceCore *core, uint32_t *bytes);
int MesenceRestore(MesenceCore *core);
#define MESENCE_MAX_STATE_BYTES (10u * 1024u * 1024u)
#define MESENCE_STATE_FORMAT 0x4d434531u /* MCE1: incompatible with InfoNES snapshots */
#ifdef __cplusplus
}
#endif
#endif
