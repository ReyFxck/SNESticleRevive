#ifndef _EMBEDDED_IRX_H
#define _EMBEDDED_IRX_H

#ifdef __cplusplus
extern "C" {
#endif

/* Look up an embedded IRX by basename (e.g. "CDVD.IRX"). The match is
   case-insensitive against the basename of the requested path, so
   "host:CDVD.IRX", "cdrom:\\CDVD.IRX", "rom0:CDVD.IRX" etc. all hit the
   same entry. Returns 0 and fills *out_data / *out_size on hit, or
   returns -1 if no embedded module matches. */
int  EmbeddedIrxFind(const char *path,
                     const unsigned char **out_data,
                     unsigned int         *out_size);

/* Loads an embedded IRX onto the IOP via SifExecModuleBuffer. Returns
   the module ID on success or a negative error on failure. */
int  EmbeddedIrxLoad(const unsigned char *data,
                     unsigned int         size,
                     int                  arg_len,
                     const char          *args);

/* Loads the memory-card IRX stack (sio2man + mcman + mcserv) onto the
   IOP from the buffers embedded in this ELF.  Replaces
   ps2_drivers::init_memcard_driver(true) so the IRX images are pinned
   to whatever the in-tree PS2SDK supplies and the load sequence is
   visible in source.  Returns 0 on success or a negative module-load
   error code (matches MEMCARD_INIT_STATUS_*).  Safe to call multiple
   times -- subsequent calls are no-ops. */
int  MemCardLoadEmbeddedIrx(void);

/* Loads the modern PS2SDK pad stack (padman.irx + mtapman.irx) onto
   the IOP from the buffers embedded in this ELF.  Designed to stack
   on top of the modern sio2man.irx already loaded by
   MemCardLoadEmbeddedIrx() -- DO NOT call this before the memcard
   bring-up, padman depends on sio2man's SIO2 transport.

   Replaces the previous \"rom0:XSIO2MAN + rom0:XMTAPMAN + rom0:XPADMAN\"
   BIOS path, which conflicted with the modern sio2man already loaded
   for the memcard stack and was the root cause of \"controller works
   in emulator but not on real PS2 hardware\".

   Returns 0 on success or a negative module-load error code:
       -1  padman.irx  failed
       -2  mtapman.irx failed (non-fatal: caller may ignore)
   Safe to call multiple times -- subsequent calls are no-ops. */

int  PadLoadEmbeddedIrx(void);
int  NetIfLoadEmbeddedIrx(void);

/* USB + BDM moderno: substitui o init_usb_driver() do ps2_drivers,
   lendo FAT/exFAT/MBR/GPT e enumerando massN: por drive. */
int  UsbBdmLoadEmbeddedIrx(void);

/* HD INTERNO (APA): dev9 + ps2atad + ps2hdd.  Carga PREGUICOSA -- NUNCA
   no boot (a init desses modulos e' sincrona e pode travar consoles sem
   HD, dando tela preta).  Chamada so' quando o usuario entra em hdd0:
   no browser, e somente se HddSupportIsEnabled() (opcao liga/desliga,
   padrao DESLIGADO, igual ao "HDD device start mode" do OPL).

   HddSupportIsEnabled / SetEnabled: estado do toggle (persistido no
   video.cfg).  HddLoadEmbeddedIrx: faz a carga (no-op se desligado ou
   ja carregado); retorna 0 em sucesso, <0 se desligado/falhou. */
int  HddSupportIsEnabled(void);
void HddSupportSetEnabled(int enabled);
int  HddLoadEmbeddedIrx(void);

/* MMCE (MemCard PRO2 / SD2PSX via mmceman) -> mmce0:/mmce1:.  Mesma logica
   preguicosa/opcional do HDD: nunca carrega no boot.  Carga feita ao entrar
   em mmce0:/mmce1:, so' se MmceSupportIsEnabled() (toggle persistido). */
int  MmceSupportIsEnabled(void);
void MmceSupportSetEnabled(int enabled);
int  MmceLoadEmbeddedIrx(void);

/* Mass (USB): a stack USB sempre sobe no boot; este flag controla a
   listagem de mass0:/mass1: e a carga do mx4sio.  Host (host:): so'
   listagem (sem modulo).  Mx4sioLoadIfEnabled: carrega mx4sio_bd APOS a
   config se Mass estiver ligado (chamado em mainloop_init, fora do boot). */
int  MassStorageIsEnabled(void);
void MassStorageSetEnabled(int enabled);
int  HostIsEnabled(void);
void HostSetEnabled(int enabled);
int  Mx4sioLoadIfEnabled(void);

#ifdef __cplusplus
}
#endif

#endif
