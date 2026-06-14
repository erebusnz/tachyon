#include "korg_ms.h"
#include <string.h>

/* ---- Korg/protobuf field IDs (from KorgmultisampleConstants) ---- */
/* Chunk-3 top level */
#define ID_AUTHOR     0x12
#define ID_CATEGORY   0x1A
#define ID_COMMENT    0x22
#define ID_SAMPLE     0x2A
#define ID_UUID       0x3A
/* Sample sub-message */
#define ID_START      0x10
#define ID_LOOP_START 0x18
#define ID_END        0x20
#define ID_LOOP_TUNE  0x45
#define ID_ONE_SHOT   0x48
#define ID_BOOST_12DB 0x50
/* Key-zone */
#define ID_KEY_BOTTOM 0x10
#define ID_KEY_TOP    0x18
#define ID_KEY_ORIG   0x20
#define ID_FIXED      0x28
#define ID_TUNE       0x35
#define ID_LEVEL_L    0x3D
#define ID_LEVEL_R    0x45
#define ID_COLOR      0x50

/* ---- little cursor over a byte buffer ---- */
typedef struct {
    const uint8_t *p;
    uint32_t       len;
    uint32_t       pos;
    bool           err;
} rd_t;

static uint8_t rd_u8(rd_t *r)
{
    if (r->pos >= r->len) { r->err = true; return 0; }
    return r->p[r->pos++];
}

static uint32_t rd_u32le(rd_t *r)
{
    uint32_t v = rd_u8(r);
    v |= (uint32_t)rd_u8(r) << 8;
    v |= (uint32_t)rd_u8(r) << 16;
    v |= (uint32_t)rd_u8(r) << 24;
    return v;
}

/* protobuf base-128 varint (little-endian). Returns value, *n = bytes used. */
static uint32_t rd_varint(rd_t *r, int *n)
{
    uint32_t v = 0;
    int      shift = 0, used = 0;
    for (;;) {
        uint8_t b = rd_u8(r);
        used++;
        v |= (uint32_t)(b & 0x7F) << shift;
        if ((b & 0x80) == 0) break;
        shift += 7;
        if (shift > 28) break;
    }
    if (n) *n = used;
    return v;
}

/* Read a 1-byte-length ASCII string into dst (always NUL-terminated). */
static void rd_str1(rd_t *r, char *dst, int cap)
{
    int len = rd_u8(r);
    for (int i = 0; i < len; i++) {
        uint8_t c = rd_u8(r);
        if (i < cap - 1) dst[i] = (char)c;
    }
    int keep = len < cap - 1 ? len : cap - 1;
    dst[keep] = '\0';
}

/* Parse one ID_SAMPLE (key-zone) block; appends a zone to out. */
static void parse_sample(rd_t *c, kms_t *out)
{
    if (out->zone_count >= KMS_MAX_ZONES) { c->err = true; return; }
    kms_zone_t *z = &out->zones[out->zone_count];
    memset(z, 0, sizeof *z);
    z->key_high = 127;
    z->key_root = 60;

    int      vn;
    uint32_t block_len = rd_varint(c, &vn);   /* outer key-zone length */

    rd_u8(c); rd_u8(c);                        /* skip sample sub-msg tag + len */
    if (rd_u8(c) != 0x0A) { c->err = true; return; }  /* filename ASCII marker */

    int name_len = rd_u8(c);
    for (int i = 0; i < name_len; i++) {
        uint8_t ch = rd_u8(c);
        if (i < KMS_FILE_LEN - 1) z->file[i] = (char)ch;
    }
    z->file[name_len < KMS_FILE_LEN - 1 ? name_len : KMS_FILE_LEN - 1] = '\0';

    /* Bytes remaining for sample + key-zone params (see reference). */
    int rest = (int)block_len - 3 - name_len - 1;

    /* Sample params (IDs ascending; stop when an ID decreases). */
    int last = 0;
    while (rest > 0) {
        uint32_t save = c->pos;
        uint8_t  id   = rd_u8(c);
        if (c->err) return;
        if (id < last) { c->pos = save; break; }
        rest--;
        switch (id) {
        case ID_START:      { int n; z->start = rd_varint(c, &n); rest -= n; } break;
        case ID_LOOP_START: { int n; (void)rd_varint(c, &n);      rest -= n; } break;
        case ID_END:        { int n; z->end   = rd_varint(c, &n); rest -= n; } break;
        case ID_LOOP_TUNE:  c->pos += 4; rest -= 4; break;
        case ID_ONE_SHOT:   c->pos += 1; rest -= 1; break;
        case ID_BOOST_12DB: c->pos += 1; rest -= 1; break;
        default: c->err = true; return;
        }
        last = id;
    }

    /* Key-zone params. */
    last = 0;
    while (rest > 0) {
        uint32_t save = c->pos;
        uint8_t  id   = rd_u8(c);
        if (c->err) return;
        if (id < last) { c->pos = save; break; }
        rest--;
        switch (id) {
        case ID_KEY_BOTTOM: z->key_low  = rd_u8(c); rest -= 1; break;
        case ID_KEY_TOP:    z->key_high = rd_u8(c); rest -= 1; break;
        case ID_KEY_ORIG:   z->key_root = rd_u8(c); rest -= 1; break;
        case ID_FIXED:      c->pos += 1; rest -= 1; break;
        case ID_TUNE:       c->pos += 4; rest -= 4; break;
        case ID_LEVEL_L:    c->pos += 4; rest -= 4; break;
        case ID_LEVEL_R:    c->pos += 4; rest -= 4; break;
        case ID_COLOR:      c->pos += 5; rest -= 4; break;  /* reads 5, accounts 4 (ref) */
        default: c->err = true; return;
        }
        last = id;
    }

    if (!c->err) out->zone_count++;
}

bool kms_parse(const uint8_t *buf, uint32_t len, kms_t *out)
{
    memset(out, 0, sizeof *out);

    rd_t r = { buf, len, 0, false };
    if (rd_u8(&r) != 'K' || rd_u8(&r) != 'o' || rd_u8(&r) != 'r' || rd_u8(&r) != 'g')
        return false;

    /* Three length-prefixed chunks; chunk index 2 is the multisample data. */
    const uint8_t *c2 = NULL;
    uint32_t       c2len = 0;
    for (int i = 0; i < 3; i++) {
        uint32_t sz = rd_u32le(&r);
        if (r.err || (uint64_t)r.pos + sz > len) return false;
        if (i == 2) { c2 = buf + r.pos; c2len = sz; }
        r.pos += sz;
    }
    if (!c2) return false;

    rd_t c = { c2, c2len, 0, false };
    if (rd_u8(&c) != 0x0A) return false;          /* name ASCII marker */
    rd_str1(&c, out->name, KMS_NAME_LEN);

    while (c.pos < c.len) {
        uint8_t id = rd_u8(&c);
        if (c.err) break;
        switch (id) {
        case ID_AUTHOR:
        case ID_CATEGORY:
        case ID_COMMENT: {
            char tmp[8];
            rd_str1(&c, tmp, sizeof tmp);          /* skip metadata string */
            /* rd_str1 truncates into tmp but advances the cursor by full len */
            break;
        }
        case ID_SAMPLE:
            parse_sample(&c, out);
            break;
        case ID_UUID: {
            int n = rd_u8(&c);
            c.pos += (uint32_t)n;
            break;
        }
        default:
            return false;                          /* unknown top-level ID */
        }
        if (c.err) return false;
    }

    return out->zone_count > 0;
}
