#include "wav.h"
#include <string.h>

static uint32_t rd_u32le(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

FRESULT wav_parse(FIL *f, wav_info_t *info)
{
    FRESULT fr;
    UINT    br;
    uint8_t hdr[12];

    fr = f_lseek(f, 0);
    if (fr != FR_OK) return fr;
    fr = f_read(f, hdr, sizeof hdr, &br);
    if (fr != FR_OK) return fr;
    if (br < 12 || memcmp(hdr, "RIFF", 4) != 0 || memcmp(hdr + 8, "WAVE", 4) != 0)
        return FR_INT_ERR;

    memset(info, 0, sizeof *info);
    int have_fmt = 0, have_data = 0;

    /* Walk the chunk list. Chunks are word-aligned (odd sizes get a pad byte). */
    while (!(have_fmt && have_data)) {
        uint8_t ch[8];
        fr = f_read(f, ch, sizeof ch, &br);
        if (fr != FR_OK) return fr;
        if (br < 8) break;                          /* end of file */

        uint32_t csz = rd_u32le(ch + 4);
        FSIZE_t  body = f_tell(f);                  /* start of chunk body */

        if (memcmp(ch, "fmt ", 4) == 0) {
            uint8_t fmt[16];
            UINT    n = (csz < sizeof fmt) ? csz : sizeof fmt;
            fr = f_read(f, fmt, n, &br);
            if (fr != FR_OK) return fr;
            if (br < 16) return FR_INT_ERR;
            info->format          = (uint16_t)(fmt[0] | (fmt[1] << 8));
            info->channels        = (uint16_t)(fmt[2] | (fmt[3] << 8));
            info->sample_rate     = rd_u32le(fmt + 4);
            info->bits_per_sample = (uint16_t)(fmt[14] | (fmt[15] << 8));
            have_fmt = 1;
        } else if (memcmp(ch, "data", 4) == 0) {
            info->data_offset = (uint32_t)body;
            info->data_bytes  = csz;
            have_data = 1;
        }

        /* Advance to the next chunk (skip body + pad). */
        fr = f_lseek(f, body + csz + (csz & 1));
        if (fr != FR_OK) return fr;
    }

    if (!have_fmt || !have_data) return FR_INT_ERR;
    uint32_t frame_bytes = (uint32_t)info->channels * (info->bits_per_sample / 8u);
    if (frame_bytes == 0) return FR_INT_ERR;
    info->frames = info->data_bytes / frame_bytes;
    return FR_OK;
}

FRESULT wav_read_mono_s16(FIL *f, const wav_info_t *info,
                          int16_t *dst, uint32_t max_frames, uint32_t *got)
{
    *got = 0;
    if (info->channels != 1 || info->bits_per_sample != 16) return FR_INT_ERR;

    uint32_t frames = info->frames < max_frames ? info->frames : max_frames;
    FRESULT  fr = f_lseek(f, info->data_offset);
    if (fr != FR_OK) return fr;

    UINT br;
    fr = f_read(f, dst, frames * sizeof(int16_t), &br);
    if (fr != FR_OK) return fr;
    *got = br / sizeof(int16_t);
    return FR_OK;
}
