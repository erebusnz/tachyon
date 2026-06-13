#ifndef WAV_H
#define WAV_H

#include <stdint.h>
#include "ff.h"

/* Minimal RIFF/WAVE reader for the SD-card sample files.
 *
 * The stock multisample WAVs are 16-bit mono PCM single cycles. This reader
 * locates the `fmt ` and `data` chunks and exposes the PCM payload; callers
 * validate channels/bits for their needs. */

typedef struct {
    uint16_t format;            /* WAVE format tag (1 = PCM) */
    uint16_t channels;
    uint32_t sample_rate;       /* Hz */
    uint16_t bits_per_sample;
    uint32_t data_offset;       /* byte offset of PCM payload within the file */
    uint32_t data_bytes;        /* size of the data chunk in bytes */
    uint32_t frames;            /* data_bytes / (channels * bytes_per_sample) */
} wav_info_t;

/* Parse the header of an already-open file. Leaves the file position undefined.
 * Returns FR_OK on success, or an FatFs error / FR_INT_ERR on a malformed or
 * unsupported file. */
FRESULT wav_parse(FIL *f, wav_info_t *info);

/* Read up to `max_frames` frames of 16-bit mono PCM into `dst`. Requires a
 * mono 16-bit file. `*got` receives the number of frames read. */
FRESULT wav_read_mono_s16(FIL *f, const wav_info_t *info,
                          int16_t *dst, uint32_t max_frames, uint32_t *got);

#endif
