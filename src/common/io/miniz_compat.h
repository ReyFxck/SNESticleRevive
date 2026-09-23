/*
 * Copyright (c) 1997-2004-2022 Icer Addis
 * Re-Worked By ReyFxck, Claude Aí, ChatGPT
 *
 * Description:
 *   Declares the miniz compat interface for shared input and output support.
 */

#ifndef MINIZ_COMPAT_H
#define MINIZ_COMPAT_H

/* Minimal helpers that replace the gz / zip APIs the SNESticle code
   used to call into zlib + minizip-unzip. Backed by miniz instead.
   Both helpers go through newlib stdio (fopen / fread), which after
   init_ps2_filesystem_driver() resolves through iomanX, so PS2 paths
   like cdfs:/, host:/, mass:/ and mc0:/ all work transparently. */

#ifdef __cplusplus
extern "C" {
#endif

/* Detailed negative results used by the UI to explain archive failures. */
#define MINIZ_READ_BAD_ARCHIVE   (-1001)
#define MINIZ_READ_NO_MATCH      (-1002)
#define MINIZ_READ_TOO_LARGE     (-1003)
#define MINIZ_READ_EXTRACT_FAIL  (-1004)
#define MINIZ_READ_IO_FAIL       (-1005)
#define MINIZ_READ_NO_MEMORY     (-1006)

/* Reads a `.gz` file at `path`, decompresses the deflate stream into
   `out_buf`, returning the number of decompressed bytes (>0) or -1
   on any failure (open / parse / decompress). At most `out_max`
   bytes are written. */
int MinizReadGZToBuffer(const char *path,
                        void *out_buf,
                        int out_max);

/* Opens the zip at `path`, walks the central directory and decompresses
   the first non-directory entry whose name is accepted by `name_filter`
   (or the first entry if `name_filter` is NULL) into `out_buf`.
   Returns the uncompressed byte count (>0) or -1 if no entry matched
   or any miniz / IO step failed. The matched file's name (within the
   archive) is written into `out_filename` if non-NULL, truncated to
   `filename_max` bytes incl. NUL. */
int MinizReadZipFirstMatch(const char *path,
                           void *out_buf,
                           int out_max,
                           char *out_filename,
                           int filename_max,
                           int (*name_filter)(const char *name));

#ifdef __cplusplus
}
#endif

#endif /* MINIZ_COMPAT_H */
