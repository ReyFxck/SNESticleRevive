/* nessystem.h
 *
 * Emu::System wrapper for the PS2 MesenCE NES core.
 *
 * MesenCE is the only NES backend.  The wrapper mirrors SnesSystem so the
 * existing PS2 frontend can dispatch through Emu::System* for both consoles.
 */

#ifndef _NESSYSTEM_H
#define _NESSYSTEM_H

#include "types.h"
#include "emusys.h"
#include "nesrom.h"
#include "../mesence/mesence_bridge.h"

class NesMMU;

class NesSystem : public Emu::System
{
public:
    NesSystem();
    ~NesSystem();

    virtual void  SetRom(Emu::Rom *pRom);
    virtual void  Reset();
    virtual void  SoftReset();

    virtual void  ExecuteFrame(Emu::SysInputT *pInput,
                               class CRenderSurface *pTarget,
                               class CMixBuffer *pMixBuf,
                               ModeE eMode);

    virtual Int32 GetStateSize();
    virtual void  SaveState(void *pState, Int32 nStateBytes);
    virtual void  RestoreState(void *pState, Int32 nStateBytes);

    virtual Int32  GetSRAMBytes();
    virtual Uint8 *GetSRAMData();

    virtual const char *GetString(StringE eString);
    virtual Uint32 GetSampleRate();

    Bool          SnapshotState();
    Bool          AllocateState(Uint32 bytes);
    Uint8        *GetSnapshotData();
    Uint32        GetSnapshotBytes();
    Bool          RestoreSnapshot();
    Uint32        GetFrameRate();
    Bool          IsRomReady() const { return m_bRomReady; }

    /* FDS plumbing is still frontend-only; MesenCE disk switching is a
       separate follow-up.  Keep the existing frontend API stable for now. */
    NesMMU       *GetMMU() { return NULL; }
    void          SetNesDisk(NesDisk *pDisk) { m_pNesDisk = pDisk; }

private:
    struct MesenceCore *m_pCore;
    NesRom    *m_pNesRom;
    NesDisk   *m_pNesDisk;
    Bool       m_bRomReady;
    Uint32     m_uFrameTick;
};

#endif
