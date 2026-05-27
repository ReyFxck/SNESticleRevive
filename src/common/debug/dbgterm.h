#pragma once

#ifdef __cplusplus
extern "C" {
#endif

void DbgInit(void);
void DbgSetPhase(const char *func, const char *msg);
void DbgLog(const char *fmt, ...);
void DbgOk(const char *func, const char *msg);
void DbgErr(const char *func, const char *msg);
void DbgShow(void);
void DbgPanic(const char *func, const char *msg);
void DbgDumpStack(void);

#ifdef __cplusplus
}
#endif

#define DBG_PHASE(msg) DbgSetPhase(__FUNCTION__, (msg))
#define DBG_OK(msg)    DbgOk(__FUNCTION__, (msg))
#define DBG_ERR(msg)   DbgErr(__FUNCTION__, (msg))
