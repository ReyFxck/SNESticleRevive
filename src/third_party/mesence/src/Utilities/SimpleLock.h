#pragma once
#include "pch.h"

class SimpleLock;

class LockHandler
{
private:
    SimpleLock *_lock;
    bool _released = false;
public:
    LockHandler(SimpleLock *lock);
    void Release();
    ~LockHandler();
};

#ifdef PS2_PORT
// The PS2 frontend runs the Mesen core on a single EE thread.  A tiny
// re-entrant counter is enough here and avoids libstdc++ thread primitives in
// hot paths (and the linker dependencies they bring with them).
class SimpleLock
{
private:
    uint32_t _lockCount = 0;
public:
    SimpleLock();
    ~SimpleLock();

    LockHandler AcquireSafe();
    void Acquire();
    bool TryAcquire(uint32_t msTimeout);
    bool IsFree();
    bool IsLockedByCurrentThread();
    void WaitForRelease();
    void Release();
};
#else
#include <thread>
class SimpleLock
{
private:
    thread_local static std::thread::id _threadID;

    std::thread::id _holderThreadID;
    uint32_t _lockCount;
    atomic_flag _lock;

    bool WaitForAcquire(uint32_t msTimeout);

public:
    SimpleLock();
    ~SimpleLock();

    LockHandler AcquireSafe();

    void Acquire();
    bool TryAcquire(uint32_t msTimeout);
    bool IsFree();
    bool IsLockedByCurrentThread();
    void WaitForRelease();
    void Release();
};
#endif
