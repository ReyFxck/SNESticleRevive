#ifndef TEST_KERNEL_H
#define TEST_KERNEL_H
int RotateThreadReadyQueue(int priority);
typedef struct { int init_count, max_count; } ee_sema_t;
int CreateSema(ee_sema_t *semaphore);
int WaitSema(int id);
int SignalSema(int id);
#endif
