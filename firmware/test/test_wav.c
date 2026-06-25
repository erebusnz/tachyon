#include "unity.h"
#include "wav.h"
#include <string.h>

/* ---- little-endian WAV byte-buffer builder ---- */
static uint8_t  buf[1024];
static uint32_t len;

static void b_reset(void) { len = 0; memset(buf, 0, sizeof buf); }
static void b_tag(const char *t) { memcpy(buf + len, t, 4); len += 4; }
static void b_u16(uint16_t v) { buf[len++] = v & 0xFF; buf[len++] = (v >> 8) & 0xFF; }
static void b_u32(uint32_t v) {
    buf[len++] = v & 0xFF;        buf[len++] = (v >> 8) & 0xFF;
    buf[len++] = (v >> 16) & 0xFF; buf[len++] = (v >> 24) & 0xFF;
}

static void b_riff(void) { b_tag("RIFF"); b_u32(0 /* size unused by reader */); b_tag("WAVE"); }

static void b_fmt(uint16_t fmt, uint16_t ch, uint32_t rate, uint16_t bits) {
    uint16_t block    = (uint16_t)(ch * (bits / 8));
    uint32_t byterate = rate * block;
    b_tag("fmt "); b_u32(16);
    b_u16(fmt); b_u16(ch); b_u32(rate); b_u32(byterate); b_u16(block); b_u16(bits);
}

/* Write a data chunk header + payload of `nsamp` int16 samples; returns the
 * byte offset of the payload (expected data_offset). */
static uint32_t b_data_s16(const int16_t *s, uint32_t nsamp) {
    uint32_t bytes = nsamp * 2u;
    b_tag("data"); b_u32(bytes);
    uint32_t off = len;
    memcpy(buf + len, s, bytes); len += bytes;
    return off;
}

static FIL mkfil(void) { FIL f; f.buf = buf; f.size = len; f.pos = 0; return f; }

void setUp(void) { b_reset(); }
void tearDown(void) {}

static const int16_t SAMPLES[4] = { 100, -200, 300, -400 };

/* ---------------- header parsing ---------------- */

static void test_parse_basic_mono16(void)
{
    b_riff();
    b_fmt(1, 1, 44100, 16);
    uint32_t off = b_data_s16(SAMPLES, 4);

    FIL f = mkfil();
    wav_info_t info;
    TEST_ASSERT_EQUAL(FR_OK, wav_parse(&f, &info));
    TEST_ASSERT_EQUAL_UINT16(1, info.format);
    TEST_ASSERT_EQUAL_UINT16(1, info.channels);
    TEST_ASSERT_EQUAL_UINT32(44100, info.sample_rate);
    TEST_ASSERT_EQUAL_UINT16(16, info.bits_per_sample);
    TEST_ASSERT_EQUAL_UINT32(off, info.data_offset);
    TEST_ASSERT_EQUAL_UINT32(8, info.data_bytes);
    TEST_ASSERT_EQUAL_UINT32(4, info.frames);
}

static void test_parse_skips_unknown_chunk(void)
{
    b_riff();
    b_fmt(1, 1, 48000, 16);
    /* an unrelated chunk between fmt and data must be skipped */
    b_tag("LIST"); b_u32(4); b_u32(0xDEADBEEF);
    uint32_t off = b_data_s16(SAMPLES, 4);

    FIL f = mkfil();
    wav_info_t info;
    TEST_ASSERT_EQUAL(FR_OK, wav_parse(&f, &info));
    TEST_ASSERT_EQUAL_UINT32(48000, info.sample_rate);
    TEST_ASSERT_EQUAL_UINT32(off, info.data_offset);
    TEST_ASSERT_EQUAL_UINT32(4, info.frames);
}

static void test_parse_odd_chunk_padding(void)
{
    b_riff();
    b_fmt(1, 1, 22050, 16);
    /* odd-sized chunk (3 bytes) gets a pad byte; reader must word-align past it */
    b_tag("fact"); b_u32(3); buf[len++] = 1; buf[len++] = 2; buf[len++] = 3; buf[len++] = 0;
    uint32_t off = b_data_s16(SAMPLES, 4);

    FIL f = mkfil();
    wav_info_t info;
    TEST_ASSERT_EQUAL(FR_OK, wav_parse(&f, &info));
    TEST_ASSERT_EQUAL_UINT32(off, info.data_offset);
    TEST_ASSERT_EQUAL_UINT32(4, info.frames);
}

static void test_parse_frames_stereo_division(void)
{
    b_riff();
    b_fmt(1, 2, 44100, 16);             /* stereo: frame = 4 bytes */
    int16_t st[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };
    b_data_s16(st, 8);                  /* 16 bytes / 4 = 4 frames */

    FIL f = mkfil();
    wav_info_t info;
    TEST_ASSERT_EQUAL(FR_OK, wav_parse(&f, &info));
    TEST_ASSERT_EQUAL_UINT16(2, info.channels);
    TEST_ASSERT_EQUAL_UINT32(4, info.frames);
}

static void test_parse_rejects_bad_magic(void)
{
    b_tag("XXXX"); b_u32(0); b_tag("WAVE");
    b_fmt(1, 1, 44100, 16);
    b_data_s16(SAMPLES, 4);

    FIL f = mkfil();
    wav_info_t info;
    TEST_ASSERT_EQUAL(FR_INT_ERR, wav_parse(&f, &info));
}

static void test_parse_rejects_missing_data(void)
{
    b_riff();
    b_fmt(1, 1, 44100, 16);             /* no data chunk -> hits EOF */

    FIL f = mkfil();
    wav_info_t info;
    TEST_ASSERT_EQUAL(FR_INT_ERR, wav_parse(&f, &info));
}

/* ---------------- PCM read ---------------- */

static void test_read_mono_s16_values(void)
{
    b_riff();
    b_fmt(1, 1, 44100, 16);
    b_data_s16(SAMPLES, 4);

    FIL f = mkfil();
    wav_info_t info;
    TEST_ASSERT_EQUAL(FR_OK, wav_parse(&f, &info));

    int16_t dst[4] = {0};
    uint32_t got = 0;
    TEST_ASSERT_EQUAL(FR_OK, wav_read_mono_s16(&f, &info, dst, 4, &got));
    TEST_ASSERT_EQUAL_UINT32(4, got);
    TEST_ASSERT_EQUAL_INT16_ARRAY(SAMPLES, dst, 4);
}

static void test_read_mono_s16_partial(void)
{
    b_riff();
    b_fmt(1, 1, 44100, 16);
    b_data_s16(SAMPLES, 4);

    FIL f = mkfil();
    wav_info_t info;
    wav_parse(&f, &info);

    int16_t dst[2] = {0};
    uint32_t got = 0;
    TEST_ASSERT_EQUAL(FR_OK, wav_read_mono_s16(&f, &info, dst, 2, &got));
    TEST_ASSERT_EQUAL_UINT32(2, got);
    TEST_ASSERT_EQUAL_INT16(100,  dst[0]);
    TEST_ASSERT_EQUAL_INT16(-200, dst[1]);
}

static void test_read_mono_rejects_stereo(void)
{
    b_riff();
    b_fmt(1, 2, 44100, 16);
    int16_t st[4] = { 1, 2, 3, 4 };
    b_data_s16(st, 4);

    FIL f = mkfil();
    wav_info_t info;
    wav_parse(&f, &info);

    int16_t dst[4]; uint32_t got = 99;
    TEST_ASSERT_EQUAL(FR_INT_ERR, wav_read_mono_s16(&f, &info, dst, 4, &got));
    TEST_ASSERT_EQUAL_UINT32(0, got);
}

static void test_read_mono_rejects_8bit(void)
{
    b_riff();
    b_fmt(1, 1, 44100, 8);
    int16_t pcm[2] = { 0x0201, 0x0403 };   /* 4 bytes of 8-bit payload */
    b_data_s16(pcm, 2);

    FIL f = mkfil();
    wav_info_t info;
    wav_parse(&f, &info);

    int16_t dst[4]; uint32_t got = 99;
    TEST_ASSERT_EQUAL(FR_INT_ERR, wav_read_mono_s16(&f, &info, dst, 4, &got));
    TEST_ASSERT_EQUAL_UINT32(0, got);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_parse_basic_mono16);
    RUN_TEST(test_parse_skips_unknown_chunk);
    RUN_TEST(test_parse_odd_chunk_padding);
    RUN_TEST(test_parse_frames_stereo_division);
    RUN_TEST(test_parse_rejects_bad_magic);
    RUN_TEST(test_parse_rejects_missing_data);
    RUN_TEST(test_read_mono_s16_values);
    RUN_TEST(test_read_mono_s16_partial);
    RUN_TEST(test_read_mono_rejects_stereo);
    RUN_TEST(test_read_mono_rejects_8bit);
    return UNITY_END();
}
