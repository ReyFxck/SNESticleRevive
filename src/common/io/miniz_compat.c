#include "miniz_compat.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "miniz.h"

/* All ROM-source paths reachable by the project (cdfs:/, mc0:/,
   mass:/, host:/, ...) are exposed by iomanX once
   init_ps2_filesystem_driver() has run. We can therefore use newlib
   stdio uniformly. We still slurp the entire compressed file into a
   heap buffer and hand it to miniz's mem-reader rather than letting
   miniz fopen() it itself, because miniz's fopen path emits
   fseek/ftell/fread patterns that some iomanX backends are picky
   about (cdfs.irx in particular). The cost is one extra copy of the
   compressed archive, which is a few MB tops. */

static int read_file_to_alloc(const char *path, void **out_buf, int *out_size)
{
        FILE *fp;
        long size;
        size_t n;
        void *buf;

        if (!path || !path[0])
                return -1;

        fp = fopen(path, "rb");
        if (!fp)
                return -1;

        if (fseek(fp, 0, SEEK_END) != 0)
        {
                fclose(fp);
                return -1;
        }
        size = ftell(fp);
        if (size <= 0)
        {
                fclose(fp);
                return -1;
        }
        if (fseek(fp, 0, SEEK_SET) != 0)
        {
                fclose(fp);
                return -1;
        }

        buf = malloc((size_t)size);
        if (!buf)
        {
                fclose(fp);
                return -1;
        }

        n = fread(buf, 1, (size_t)size, fp);
        fclose(fp);

        if ((long)n != size)
        {
                free(buf);
                return -1;
        }

        *out_buf = buf;
        *out_size = (int)size;
        return (int)size;
}

/* Walks the variable-length gzip header (RFC 1952 section 2.3) and
   returns the byte offset where the raw DEFLATE stream starts, or -1
   if the buffer doesn't look like a valid gzip stream. */
static int parse_gzip_header(const unsigned char *data, int len)
{
        unsigned char flags;
        int off;

        if (len < 10)
                return -1;
        if (data[0] != 0x1F || data[1] != 0x8B || data[2] != 8)
                return -1;

        flags = data[3];
        off = 10;

        if (flags & 0x04) /* FEXTRA */
        {
                int xlen;
                if (off + 2 > len) return -1;
                xlen = data[off] | (data[off + 1] << 8);
                off += 2 + xlen;
                if (off > len) return -1;
        }
        if (flags & 0x08) /* FNAME */
        {
                while (off < len && data[off] != 0) off++;
                if (off >= len) return -1;
                off++;
        }
        if (flags & 0x10) /* FCOMMENT */
        {
                while (off < len && data[off] != 0) off++;
                if (off >= len) return -1;
                off++;
        }
        if (flags & 0x02) /* FHCRC */
        {
                off += 2;
                if (off > len) return -1;
        }
        return off;
}

int MinizReadGZToBuffer(const char *path, void *out_buf, int out_max)
{
        void *gz_data = NULL;
        int gz_size = 0;
        int hdr;
        int deflate_len;
        size_t produced;

        if (!out_buf || out_max <= 0)
                return -1;

        if (read_file_to_alloc(path, &gz_data, &gz_size) <= 0)
                return -1;

        hdr = parse_gzip_header((const unsigned char *)gz_data, gz_size);
        if (hdr < 0 || gz_size <= hdr + 8)
        {
                free(gz_data);
                return -1;
        }

        /* The DEFLATE payload lives between [hdr .. gz_size-8); the
           trailing 8 bytes are the gzip CRC32 + ISIZE which miniz
           does not need. */
        deflate_len = gz_size - hdr - 8;

        produced = tinfl_decompress_mem_to_mem(
                out_buf, (size_t)out_max,
                (const unsigned char *)gz_data + hdr, (size_t)deflate_len,
                0 /* raw deflate, not zlib-wrapped */);

        free(gz_data);

        if (produced == TINFL_DECOMPRESS_MEM_TO_MEM_FAILED)
                return -1;

        return (int)produced;
}

int MinizReadZipFirstMatch(const char *path,
                           void *out_buf,
                           int out_max,
                           char *out_filename,
                           int filename_max,
                           int (*name_filter)(const char *name))
{
        void *zip_data = NULL;
        int zip_size = 0;
        mz_zip_archive zip;
        mz_uint num_files;
        mz_uint i;
        int result = -1;

        if (!out_buf || out_max <= 0)
                return -1;

        if (read_file_to_alloc(path, &zip_data, &zip_size) <= 0)
                return -1;

        memset(&zip, 0, sizeof(zip));
        if (!mz_zip_reader_init_mem(&zip, zip_data, (size_t)zip_size, 0))
        {
                free(zip_data);
                return -1;
        }

        num_files = mz_zip_reader_get_num_files(&zip);
        for (i = 0; i < num_files; i++)
        {
                mz_zip_archive_file_stat st;
                if (!mz_zip_reader_file_stat(&zip, i, &st))
                        continue;
                if (st.m_is_directory)
                        continue;
                if (st.m_uncomp_size == 0)
                        continue;
                if (st.m_uncomp_size > (mz_uint64)out_max)
                        continue;
                if (name_filter && !name_filter(st.m_filename))
                        continue;

                if (mz_zip_reader_extract_to_mem(
                                &zip, i, out_buf,
                                (size_t)st.m_uncomp_size, 0))
                {
                        result = (int)st.m_uncomp_size;
                        if (out_filename && filename_max > 0)
                        {
                                strncpy(out_filename, st.m_filename, (size_t)(filename_max - 1));
                                out_filename[filename_max - 1] = '\0';
                        }
                        break;
                }
        }

        mz_zip_reader_end(&zip);
        free(zip_data);
        return result;
}
