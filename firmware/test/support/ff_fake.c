#include "ff.h"
#include <string.h>

FRESULT f_lseek(FIL *fp, FSIZE_t ofs)
{
    /* The reader only seeks within the file (or exactly to EOF). Clamp rather
     * than extend — this is a read-only fake. */
    fp->pos = (ofs > fp->size) ? fp->size : ofs;
    return FR_OK;
}

FRESULT f_read(FIL *fp, void *buff, UINT btr, UINT *br)
{
    FSIZE_t avail = fp->size - fp->pos;
    UINT    n     = (btr < avail) ? btr : (UINT)avail;   /* short-count at EOF */
    memcpy(buff, fp->buf + fp->pos, n);
    fp->pos += n;
    *br = n;
    return FR_OK;
}

FSIZE_t f_tell(FIL *fp)
{
    return fp->pos;
}
