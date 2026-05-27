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
