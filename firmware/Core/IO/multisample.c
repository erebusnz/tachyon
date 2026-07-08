#include "multisample.h"
#include "wav.h"
#include "ff.h"
#include <stdio.h>
#include <string.h>

/* Shared PCM pool. The largest stock multisample (E0=2048 .. E7=16 frames over
 * 8 octaves) totals ~4080 frames; 8192 gives generous headroom. Lives in
 * CCM-RAM (zero-filled by startup): only the CPU reads it (wt_render), so it
 * costs no SRAM1 and never contends with the DMA buses. */
#define POOL_FRAMES   8192
__attribute__((section(".ccmbss")))
static int16_t s_pool[POOL_FRAMES];

/* Scratch for the (tiny, ~700 B) .korgmultisample file. */
static uint8_t s_kbuf[2048];

static bool ends_with_icase(const char *s, const char *suffix)
{
    size_t ls = strlen(s), lf = strlen(suffix);
    if (lf > ls) return false;
    const char *a = s + (ls - lf);
    for (size_t i = 0; i < lf; i++) {
        char ca = a[i], cb = suffix[i];
        if (ca >= 'A' && ca <= 'Z') ca += 32;
        if (cb >= 'A' && cb <= 'Z') cb += 32;
        if (ca != cb) return false;
    }
    return true;
}

/* Find the first "*.korgmultisample" in `folder`, returning its name in `out`. */
static bool find_korg_file(const char *folder, char *out, int cap)
{
    DIR     dir;
    FILINFO fno;
    if (f_opendir(&dir, folder) != FR_OK) return false;

    bool found = false;
    for (;;) {
        if (f_readdir(&dir, &fno) != FR_OK || fno.fname[0] == 0) break;
        if ((fno.fattrib & AM_DIR) == 0 &&
            ends_with_icase(fno.fname, ".korgmultisample")) {
            snprintf(out, cap, "%s", fno.fname);
            found = true;
            break;
        }
    }
    f_closedir(&dir);
    return found;
}

static FRESULT read_whole_file(const char *path, uint8_t *buf, uint32_t cap, uint32_t *len)
{
    FIL     f;
    FRESULT fr = f_open(&f, path, FA_READ);
    if (fr != FR_OK) return fr;
    UINT br = 0;
    fr = f_read(&f, buf, cap, &br);
    f_close(&f);
    *len = br;
    return fr;
}

bool multisample_load(const char *folder, multisample_t *ms)
{
    memset(ms, 0, sizeof *ms);

    char kfile[KMS_FILE_LEN];
    if (!find_korg_file(folder, kfile, sizeof kfile)) {
        printf("ms: no .korgmultisample in %s\r\n", folder);
        return false;
    }

    char path[160];
    snprintf(path, sizeof path, "%s/%s", folder, kfile);

    uint32_t klen = 0;
    if (read_whole_file(path, s_kbuf, sizeof s_kbuf, &klen) != FR_OK) {
        printf("ms: read failed %s\r\n", path);
        return false;
    }

    kms_t k;
    if (!kms_parse(s_kbuf, klen, &k)) {
        printf("ms: parse failed (%lu bytes)\r\n", (unsigned long)klen);
        return false;
    }

    snprintf(ms->name, sizeof ms->name, "%s", k.name);

    uint32_t pool_used = 0;
    for (int i = 0; i < k.zone_count; i++) {
        const kms_zone_t *kz = &k.zones[i];

        snprintf(path, sizeof path, "%s/%s", folder, kz->file);
        FIL f;
        if (f_open(&f, path, FA_READ) != FR_OK) {
            printf("ms: open failed %s\r\n", path);
            continue;
        }

        wav_info_t wi;
        if (wav_parse(&f, &wi) != FR_OK || wi.channels != 1 || wi.bits_per_sample != 16) {
            printf("ms: bad wav %s (ch=%u bits=%u)\r\n", kz->file, wi.channels, wi.bits_per_sample);
            f_close(&f);
            continue;
        }

        uint32_t room = POOL_FRAMES - pool_used;
        if (wi.frames > room) {
            printf("ms: pool full at zone %d\r\n", i);
            f_close(&f);
            break;
        }

        uint32_t got = 0;
        int16_t *dst = &s_pool[pool_used];
        FRESULT  fr  = wav_read_mono_s16(&f, &wi, dst, room, &got);
        f_close(&f);
        if (fr != FR_OK) {
            printf("ms: pcm read failed %s\r\n", kz->file);
            continue;
        }

        ms_zone_t *z = &ms->zones[ms->zone_count++];
        z->key_low     = kz->key_low;
        z->key_high    = kz->key_high;
        z->key_root    = kz->key_root;
        z->frames      = got;
        z->sample_rate = wi.sample_rate;
        z->samples     = dst;
        pool_used     += got;
    }

    return ms->zone_count > 0;
}

void multisample_dump(const multisample_t *ms)
{
    printf("multisample '%s': %d zones\r\n", ms->name, ms->zone_count);
    for (int i = 0; i < ms->zone_count; i++) {
        const ms_zone_t *z = &ms->zones[i];
        printf("  zone %d: key[%u..%u] root=%u frames=%lu rate=%lu\r\n",
               i, z->key_low, z->key_high, z->key_root,
               (unsigned long)z->frames, (unsigned long)z->sample_rate);
    }
}
