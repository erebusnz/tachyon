#ifndef MULTISAMPLE_H
#define MULTISAMPLE_H

#include <stdint.h>
#include <stdbool.h>
#include "korg_ms.h"

/* A loaded multisample: one folder's worth of single-cycle wavetables, with
 * each zone's PCM resident in RAM and its key mapping from the
 * .korgmultisample. This is the input to the wavetable oscillator (Phase 3). */

typedef struct {
    uint8_t        key_low;
    uint8_t        key_high;
    uint8_t        key_root;
    uint32_t       frames;        /* PCM frames resident in `samples` */
    uint32_t       sample_rate;   /* from the WAV header */
    const int16_t *samples;       /* 16-bit mono PCM, in the shared pool */
} ms_zone_t;

typedef struct {
    char     name[KMS_NAME_LEN];
    int      zone_count;
    ms_zone_t zones[KMS_MAX_ZONES];
} multisample_t;

/* Load the multisample in `folder` (e.g. "0:/sawtooth_..._resamp"): locate the
 * .korgmultisample, parse it, and load each member WAV's PCM into the shared
 * pool. Returns true on success. The PCM pool is overwritten on each call, so
 * only one multisample is resident at a time. */
bool multisample_load(const char *folder, multisample_t *ms);

/* Dump a loaded multisample's zone table over the debug console. */
void multisample_dump(const multisample_t *ms);

#endif
