#include <tamtypes.h>
#include <stdlib.h>
#include <kernel.h>
#include <stdio.h>
#include <timer.h>
#include <string.h>
#include "netsys.h"
#include "netprint.h"

NetSysSemaT NetSysSemaNew(int initcount)
{
    ee_sema_t sem_info;
    int sem;

    sem_info.init_count = initcount;
    sem_info.max_count = 1;
    sem_info.option = 0;

    sem = CreateSema(&sem_info);
    if (sem <= 0) 
    {
        printf("CreateSema failed %i\n", sem);
        return 0;
    }
    return sem;
}

void NetSysSemaDelete(NetSysSemaT sema)
{
    if (sema == 0) 
    {
        printf("Trying to delete illegal sema (%d)\n", sema);
        return;
    }
    DeleteSema(sema);
}

int NetSysSemaWait(NetSysSemaT sema)
{
    return WaitSema(sema);
}

int NetSysSemaSignal(NetSysSemaT sema)
{
    return SignalSema(sema);
}

int NetSysThreadStart(void *pThreadFunc, int priority, void *arg)
{
    ee_thread_t server_thread;
    int threadid;

    // Use a sensible priority for the EE, e.g. 64. 
    // The argument 'priority' from IOP might be 32, which is higher on EE (smaller is higher priority).
    // Let's ensure the priority is within the EE user range (usually 0 to 127).
    if (priority < 32) priority = 32;

    server_thread.func         = pThreadFunc;
    server_thread.stack        = malloc(0x4000); // 16KB stack
    server_thread.stack_size   = 0x4000;
    server_thread.gp_reg       = &_gp;
    server_thread.initial_priority = priority;
    server_thread.attr         = 0;
    server_thread.option       = 0;

    threadid = CreateThread(&server_thread);
    if (threadid <= 0)
    {
        NetPrintf("NetServer: Failed to create thread!\n");
    } else
    {
        // start server thread
        StartThread(threadid, (void *)arg);
    }
    return threadid;
}

int NetSysGetSystemTime(void)
{
    // Return time in microseconds. CPU frequency is 294.912 MHz
    // cpu_ticks() / 295 approximates microseconds.
    u32 ticks = cpu_ticks();
    return (int)(ticks / 295);
}
