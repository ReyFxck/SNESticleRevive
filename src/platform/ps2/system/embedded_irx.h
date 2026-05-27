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

/* Loads the network IRX stack (ps2dev9 + netman + smap + ps2ip) onto
   the IOP from the buffers embedded in this ELF.  Replaces the
   previous IRX search-path bring-up that tried to load PS2IPS.IRX /
   PS2IP.IRX / PS2SMAP.IRX from host:, cdrom:, mc0:, etc.

   Returns 0 on success or a negative module-load error code:
       -1  ps2dev9.irx failed
       -2  netman.irx  failed
       -3  smap.irx    failed
       -4  ps2ip.irx   failed
   Safe to call multiple times -- subsequent calls return the cached
   result of the first attempt. */
int  NetIfLoadEmbeddedIrx(void);

#ifdef __cplusplus
}
#endif

#endif
