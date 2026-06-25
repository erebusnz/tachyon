#ifndef FF_FAKE_H
#define FF_FAKE_H

/* Minimal in-memory stand-in for ChaN FatFs, just enough to exercise the
 * read-only paths wav.c uses (f_lseek / f_read / f_tell). The real ff.h is not
 * on the host include path for the test build; this fake takes its place.
 *
 * A FIL is backed by a fixed byte buffer set up by the test. f_read copies from
 * it, f_lseek/f_tell move and report the cursor. Reads past EOF short-count
 * (matching FatFs), which is what the WAV chunk walker relies on to detect the
 * end of file. */

#include <stdint.h>
#include <stddef.h>

typedef unsigned int UINT;
typedef uint32_t     FSIZE_t;

typedef enum {
    FR_OK = 0,
    FR_DISK_ERR,
    FR_INT_ERR,
    FR_NOT_READY,
    FR_NO_FILE,
    FR_INVALID_PARAMETER
} FRESULT;

typedef struct {
    const uint8_t *buf;   /* file contents (test-owned) */
    FSIZE_t        size;  /* total bytes */
    FSIZE_t        pos;   /* current cursor */
} FIL;

FRESULT f_lseek(FIL *fp, FSIZE_t ofs);
FRESULT f_read(FIL *fp, void *buff, UINT btr, UINT *br);
FSIZE_t f_tell(FIL *fp);

#endif
