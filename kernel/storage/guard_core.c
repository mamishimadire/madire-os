/* The write guard, the pure part (1.4 stage 5): see guard_core.h. */
#include "kernel/storage/guard_core.h"
#include "kernel/storage/blk_core.h"

static const char guard_magic[16] = {
    'M', 'A', 'D', 'I', 'R', 'E', '-', 'T', 'E', 'S', 'T', '-', 'D', 'I', 'S', 'K',
};
static const char guard_label[] = "madire-test";
static const char *const guard_models[] = { "QEMU HARDDISK", "VBOX HARDDISK" };

static uint32_t guard_le32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static uint64_t guard_le64(const uint8_t *p)
{
    return (uint64_t)guard_le32(p) | (uint64_t)guard_le32(p + 4) << 32;
}

static bool guard_streq(const char *a, const char *b)
{
    uint32_t i = 0;
    for (; a[i] && a[i] == b[i]; i++)
        ;
    return a[i] == b[i];
}

bool guard_model_virtual(const char *model, char *trimmed)
{
    uint32_t start = 0, end = 0, n = 0;
    bool longer = false;                /* more than GUARD_MODEL_MAX characters: never virtual */
    while (model[start] == ' ')
        start++;
    for (uint32_t i = start; model[i]; i++)
        if (model[i] != ' ') {
            if (i - start < GUARD_MODEL_MAX)
                end = i + 1;
            else
                longer = true;
        }
    for (uint32_t i = start; i < end; i++)
        trimmed[n++] = model[i];
    trimmed[n] = 0;
    if (longer)
        return false;
    for (uint32_t i = 0; i < sizeof(guard_models) / sizeof(guard_models[0]); i++)
        if (guard_streq(trimmed, guard_models[i]))
            return true;
    return false;
}

static void guard_set(struct guard_verdict *v, enum guard_cond cond, uint64_t got, uint64_t want)
{
    v->cond = cond;
    v->got = got;
    v->want = want;
}

/* (b): the marker in the partition's first sector. */
static void guard_marker(struct guard_verdict *v, uint64_t first, uint64_t sectors, const uint8_t *m)
{
    for (uint32_t i = 0; i < 16; i++)
        if (m[i] != (uint8_t)guard_magic[i]) {
            guard_set(v, GUARD_B_MAGIC, 0, 0);
            return;
        }
    uint32_t version = guard_le32(m + 16), len = guard_le32(m + 20), stored = guard_le32(m + 40);
    uint32_t crc = blk_crc32(0, m, 40);
    uint64_t mfirst = guard_le64(m + 24), msectors = guard_le64(m + 32);
    if (version != GUARD_MARKER_VERSION) {
        guard_set(v, GUARD_B_VERSION, version, GUARD_MARKER_VERSION);
    } else if (len != GUARD_MARKER_LEN) {
        guard_set(v, GUARD_B_LENGTH, len, GUARD_MARKER_LEN);
    } else if (stored != crc) {
        guard_set(v, GUARD_B_CRC, stored, crc);
    } else if (mfirst != first) {
        guard_set(v, GUARD_B_START, mfirst, first);
    } else if (msectors != sectors) {
        guard_set(v, GUARD_B_SECTORS, msectors, sectors);
    } else {
        for (uint32_t i = 44; i < GUARD_SECTOR; i++)
            if (m[i]) {
                guard_set(v, GUARD_B_TAIL, i, 0);
                return;
            }
    }
}

/* (c): the ext2 superblock's magic and label. */
static void guard_ext2(struct guard_verdict *v, const uint8_t *sb)
{
    uint32_t magic = (uint32_t)sb[56] | (uint32_t)sb[57] << 8, n = 0;
    if (magic != GUARD_EXT2_MAGIC) {
        guard_set(v, GUARD_C_MAGIC, magic, GUARD_EXT2_MAGIC);
        return;
    }
    const uint8_t *l = sb + 120;
    for (; n < GUARD_LABEL_LEN && l[n]; n++)
        v->label[n] = l[n] >= 0x20u && l[n] < 0x7Fu ? (char)l[n] : '?';
    v->label[n] = 0;
    bool ok = true;                     /* the name, then zero bytes to the field's end */
    for (uint32_t i = 0; ok && i < GUARD_LABEL_LEN; i++)
        ok = l[i] == (i < sizeof(guard_label) - 1 ? (uint8_t)guard_label[i] : 0u);
    if (!ok)
        guard_set(v, GUARD_C_LABEL, n, sizeof(guard_label) - 1);
}

bool guard_check(struct guard_verdict *v, const char *model, uint64_t first, uint64_t sectors,
                 const uint8_t *marker, const uint8_t *sb)
{
    guard_set(v, GUARD_OK, 0, 0);
    v->label[0] = 0;
    if (!guard_model_virtual(model, v->model)) {
        guard_set(v, GUARD_A_MODEL, 0, 0);
        return false;
    }
    if (sectors < GUARD_MIN_SECTORS || !marker || !sb) {
        guard_set(v, GUARD_B_SMALL, sectors, GUARD_MIN_SECTORS);
        return false;
    }
    guard_marker(v, first, sectors, marker);
    if (v->cond == GUARD_OK)
        guard_ext2(v, sb);
    return v->cond == GUARD_OK;
}

const char *guard_cond_letter(enum guard_cond cond)
{
    switch (cond) {
    case GUARD_OK:
        return "";
    case GUARD_A_MODEL:
        return "(a)";
    case GUARD_B_SMALL:
    case GUARD_B_MAGIC:
    case GUARD_B_VERSION:
    case GUARD_B_LENGTH:
    case GUARD_B_CRC:
    case GUARD_B_START:
    case GUARD_B_SECTORS:
    case GUARD_B_TAIL:
        return "(b)";
    case GUARD_C_MAGIC:
    case GUARD_C_LABEL:
        return "(c)";
    }
    return "?";
}
