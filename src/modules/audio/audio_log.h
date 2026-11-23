/*
 * Runtime diagnostic capture for real PS2 hardware.
 *
 * DLog() always keeps its historical EE-SIO output. Diagnostic builds
 * (SNES_DIAGNOSTICS > 0 -> SNDBG_LOG=1) can additionally stream the same
 * records to emulog.txt beside the running ELF. The hot DLog() path never
 * performs filesystem I/O: it only appends to an EE-RAM staging buffer.
 * MainLoopProcess() flushes that buffer in large blocks between iterations.
 */
#ifndef _AUDIO_LOG_H
#define _AUDIO_LOG_H

#ifdef __cplusplus
extern "C" {
#endif

void DLog(const char *fmt, ...);

/* Called once from main() after argv[0] has been normalized. No I/O occurs;
 * the strings are only retained so the menu can use exactly the ELF origin.
 */
void DLogCaptureSetBootPath(const char *bootPath, const char *bootDir);

/* Runtime file logging exists only in diagnostic builds. A destination is
 * accepted only when it is the ELF directory and is not mc:/cdfs:/rom:.
 * There is deliberately no device/path fallback.
 */
int          DLogCaptureDestinationIsAllowed(void);
int          DLogCaptureGetPath(char *outPath, int outPathBytes);
int          DLogCaptureIsEnabled(void);
unsigned int DLogCaptureBytes(void);
unsigned int DLogCaptureDroppedBytes(void);
unsigned int DLogCaptureFlushCount(void);

/* Start truncates/opens <ELF-dir>/emulog.txt. Pump performs at most one
 * buffered block write and is intended to be called outside frame timing.
 * Stop forces the final partial block and closes the file.
 */
int          DLogCaptureStart(void);
void         DLogCapturePump(void);
int          DLogCaptureStopAndSave(char *outPath, int outPathBytes);

#ifdef __cplusplus
}
#endif

#endif /* _AUDIO_LOG_H */
