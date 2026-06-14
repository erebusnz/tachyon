#ifndef KORG_MS_H
#define KORG_MS_H

#include <stdint.h>
#include <stdbool.h>

/* Parser for Korg `.korgmultisample` files (Wavestate/Modwave "Sample Builder"
 * format). The format is a "Korg" magic followed by three length-prefixed
 * protobuf-ish chunks; chunk 3 holds the multisample name and a list of sample
 * key-zones. We extract, per zone, the member WAV file name and its key
 * mapping (low/high/root) plus the sample start/end (frame indices).
 *
 * Field semantics follow Jürgen Moßgraber's ConvertWithMoss reference
 * implementation (KorgmultisampleDetector / KorgmultisampleConstants). */

#define KMS_MAX_ZONES   24
#define KMS_NAME_LEN    48
#define KMS_FILE_LEN    64

typedef struct {
    char     file[KMS_FILE_LEN];  /* member WAV file name (within the folder) */
    uint32_t start;               /* sample start frame */
    uint32_t end;                 /* sample end frame (≈ frame count) */
    uint8_t  key_low;             /* zone bottom key (MIDI) */
    uint8_t  key_high;            /* zone top key */
    uint8_t  key_root;            /* zone root/original key */
} kms_zone_t;

typedef struct {
    char       name[KMS_NAME_LEN];
    int        zone_count;
    kms_zone_t zones[KMS_MAX_ZONES];
} kms_t;

/* Parse a whole-file buffer. Returns true on success with `out` populated. */
bool kms_parse(const uint8_t *buf, uint32_t len, kms_t *out);

#endif
