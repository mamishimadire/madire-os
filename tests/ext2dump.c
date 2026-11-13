/* Madire OS: the ext2 read core as a host tool (1.4 stage 6; docs/plans/1.4.md,
 * S6). Built from the SAME pure file the kernel will use,
 * kernel/fs/ext2_core.c (never linked into the kernel by this tool); part
 * static compares its listing with debugfs's view of the same image
 * (tests/ext2_view.py) and runs it, built a second time with
 * -fsanitize=address,undefined, over a fixed set of corrupted images.
 *
 *   ext2dump [-l <bytes>] <filesystem image>
 *
 * Prints the superblock's figures, the feature policy (D11) and the state
 * (D12, reported), then one line per path, the root first:
 *   P <path> ino=<n> mode=<octal> size=<n> links=<n> flags=0x<hex> acl=<n> sha=<sha256 or ->
 * (sha: of a regular file's contents or a symbolic link's target; with -l,
 * files longer than twice the limit are hashed over their first and last
 * <bytes> only, "sha=part:<hex>"), an "E <path>: <error> (<why>)" line for
 * each problem, and "DONE entries=<n> errors=<n> bad_reads=<n>". Every
 * folder is entered once (a second entry naming one is an error, so a
 * corrupted image cannot make it loop) and at most 64 deep.
 *
 * Exit status: 0 everything read; 1 not mounted (the reason printed);
 * 2 problems found while reading (E lines); 3 usage, the file or the
 * SHA-256 self-test; 4 the core asked for bytes outside the image (never
 * expected: every read must lie inside the partition).
 *
 * SHA-256 is written here from FIPS 180-4 and checked at start against its
 * standard test vectors. */
#include <fcntl.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "kernel/fs/ext2_core.h"

/* ---- SHA-256 (FIPS 180-4) ---- */

struct dump_sha {
    uint32_t h[8];
    uint8_t buf[64];
    uint64_t len;
    uint32_t n;
};

static const uint32_t dump_k[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

static uint32_t dump_ror(uint32_t x, unsigned n)
{
    return (x >> n) | (x << (32u - n));
}

static void dump_sha_init(struct dump_sha *s)
{
    static const uint32_t h0[8] = {
        0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19,
    };
    memcpy(s->h, h0, sizeof h0);
    s->len = 0;
    s->n = 0;
}

static void dump_sha_block(struct dump_sha *s, const uint8_t *p)
{
    uint32_t w[64];
    for (unsigned i = 0; i < 16; i++)
        w[i] = (uint32_t)p[4 * i] << 24 | (uint32_t)p[4 * i + 1] << 16 | (uint32_t)p[4 * i + 2] << 8 | p[4 * i + 3];
    for (unsigned i = 16; i < 64; i++) {
        uint32_t s0 = dump_ror(w[i - 15], 7) ^ dump_ror(w[i - 15], 18) ^ (w[i - 15] >> 3);
        uint32_t s1 = dump_ror(w[i - 2], 17) ^ dump_ror(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    uint32_t a = s->h[0], b = s->h[1], c = s->h[2], d = s->h[3];
    uint32_t e = s->h[4], f = s->h[5], g = s->h[6], h = s->h[7];
    for (unsigned i = 0; i < 64; i++) {
        uint32_t t1 = h + (dump_ror(e, 6) ^ dump_ror(e, 11) ^ dump_ror(e, 25)) + ((e & f) ^ (~e & g)) + dump_k[i] + w[i];
        uint32_t t2 = (dump_ror(a, 2) ^ dump_ror(a, 13) ^ dump_ror(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }
    s->h[0] += a;
    s->h[1] += b;
    s->h[2] += c;
    s->h[3] += d;
    s->h[4] += e;
    s->h[5] += f;
    s->h[6] += g;
    s->h[7] += h;
}

static void dump_sha_update(struct dump_sha *s, const uint8_t *p, uint64_t n)
{
    s->len += n;
    while (n > 0) {
        if (s->n == 0 && n >= 64) {
            dump_sha_block(s, p);
            p += 64;
            n -= 64;
            continue;
        }
        uint32_t take = 64u - s->n;
        if (take > n)
            take = (uint32_t)n;
        memcpy(s->buf + s->n, p, take);
        s->n += take;
        p += take;
        n -= take;
        if (s->n == 64) {
            dump_sha_block(s, s->buf);
            s->n = 0;
        }
    }
}

/* The digest as 64 hex digits into hex (65 bytes). */
static void dump_sha_final(struct dump_sha *s, char *hex)
{
    uint64_t bits = s->len * 8u;
    uint8_t pad[72];
    uint32_t padn = (s->n < 56 ? 56u : 120u) - s->n;
    memset(pad, 0, sizeof pad);
    pad[0] = 0x80;
    for (unsigned i = 0; i < 8; i++)
        pad[padn + i] = (uint8_t)(bits >> (56 - 8 * i));
    uint64_t keep = s->len;
    dump_sha_update(s, pad, padn + 8u);
    s->len = keep;
    for (unsigned i = 0; i < 8; i++)
        snprintf(hex + 8 * i, 9, "%08" PRIx32, s->h[i]);
}

static int dump_sha_selftest(void)
{
    static const struct { const char *msg, *want; } v[] = {
        { "", "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855" },
        { "abc", "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad" },
        { "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq",
          "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1" },
    };
    for (unsigned i = 0; i < sizeof v / sizeof v[0]; i++) {
        struct dump_sha s;
        char hex[65];
        dump_sha_init(&s);
        dump_sha_update(&s, (const uint8_t *)v[i].msg, strlen(v[i].msg));
        dump_sha_final(&s, hex);
        if (strcmp(hex, v[i].want) != 0) {
            fprintf(stderr, "ext2dump: SHA-256 self-test %u failed: %s\n", i, hex);
            return -1;
        }
    }
    return 0;
}

/* ---- the image ---- */

static int dump_fd = -1;
static uint64_t dump_size;
static unsigned long dump_bad_reads;

static int dump_read(void *ctx, uint64_t off, uint32_t len, uint8_t *buf)
{
    (void)ctx;
    if (off > dump_size || len > dump_size - off) {
        dump_bad_reads++;
        return -1;
    }
    uint32_t done = 0;
    while (done < len) {
        ssize_t r = pread(dump_fd, buf + done, len - done, (off_t)(off + done));
        if (r <= 0)
            return -1;
        done += (uint32_t)r;
    }
    return 0;
}

/* ---- the walk ---- */

static struct ext2_fs dump_fs;
static struct ext2_bufs dump_bufs;      /* 1.5 stage 5: the read core's buffers (ext2_init) */
static uint8_t *dump_visited;
static unsigned long dump_entries, dump_errors;
static uint64_t dump_limit;
static char dump_path[8192];
static uint8_t dump_data[65536];

static void dump_error(const char *path, int err)
{
    dump_errors++;
    printf("E %s: %s (%s)\n", path[0] ? path : "/", ext2_err_str(err), dump_fs.why ? dump_fs.why : "-");
}

/* SHA-256 of bytes [from, to) of the file into s; 0 or an error. */
static int dump_hash_range(const struct ext2_inode *in, uint64_t from, uint64_t to, struct dump_sha *s)
{
    while (from < to) {
        uint64_t want = to - from;
        if (want > sizeof dump_data)
            want = sizeof dump_data;
        int64_t got = ext2_read(&dump_fs, in, from, dump_data, (uint32_t)want);
        if (got < 0)
            return (int)got;
        if (got == 0)
            return EXT2_ECORRUPT;       /* never: from < size */
        dump_sha_update(s, dump_data, (uint64_t)got);
        from += (uint64_t)got;
    }
    return 0;
}

/* sha: "<hex>", "part:<hex>" or "ERROR". */
static void dump_hash(const char *path, const struct ext2_inode *in, char *sha)
{
    struct dump_sha s;
    char hex[65];
    int r;
    dump_sha_init(&s);
    if (dump_limit && in->size > 2 * dump_limit) {
        r = dump_hash_range(in, 0, dump_limit, &s);
        if (r == 0)
            r = dump_hash_range(in, in->size - dump_limit, in->size, &s);
    } else {
        r = dump_hash_range(in, 0, in->size, &s);
    }
    if (r) {
        dump_error(path, r);
        strcpy(sha, "ERROR");
        return;
    }
    dump_sha_final(&s, hex);
    snprintf(sha, 80, "%s%s", dump_limit && in->size > 2 * dump_limit ? "part:" : "", hex);
}

static void dump_line(const char *path, const struct ext2_inode *in)
{
    char sha[80] = "-";
    uint32_t type = in->mode & EXT2_S_IFMT;
    if (type == EXT2_S_IFREG || type == EXT2_S_IFLNK)
        dump_hash(path, in, sha);
    dump_entries++;
    printf("P %s ino=%" PRIu32 " mode=%06o size=%" PRIu64 " links=%u flags=0x%" PRIx32 " acl=%" PRIu32 " sha=%s\n",
           path[0] ? path : "/", in->ino, (unsigned)in->mode, in->size, (unsigned)in->links, in->flags,
           in->file_acl, sha);
}

static void dump_walk(uint32_t dino, size_t plen, unsigned depth)
{
    struct ext2_inode dir;
    struct ext2_dir_iter it = { 0 };
    struct ext2_dirent de;
    memset(&dir, 0, sizeof dir);
    memset(&de, 0, sizeof de);
    int r = ext2_inode_get(&dump_fs, dino, &dir);
    if (r) {
        dump_error(dump_path, r);
        return;
    }
    for (;;) {
        r = ext2_dir_next(&dump_fs, &dir, &it, &de);
        if (r == 0)
            return;
        if (r < 0) {
            dump_error(dump_path, r);
            return;
        }
        if (strcmp(de.name, ".") == 0 || strcmp(de.name, "..") == 0)
            continue;
        size_t nl = strlen(de.name);
        if (plen + 1 + nl >= sizeof dump_path) {
            dump_errors++;
            printf("E %s/...: path too long\n", dump_path);
            continue;
        }
        dump_path[plen] = '/';
        memcpy(dump_path + plen + 1, de.name, nl + 1);
        struct ext2_inode in;
        memset(&in, 0, sizeof in);
        r = ext2_inode_get(&dump_fs, de.ino, &in);
        if (r) {
            dump_error(dump_path, r);
        } else {
            dump_line(dump_path, &in);
            if ((in.mode & EXT2_S_IFMT) == EXT2_S_IFDIR) {
                if (dump_visited[de.ino / 8] & (1u << (de.ino % 8))) {
                    dump_errors++;
                    printf("E %s: a folder already entered (inode %" PRIu32 ")\n", dump_path, de.ino);
                } else if (depth >= 64) {
                    dump_errors++;
                    printf("E %s: more than 64 folders deep\n", dump_path);
                } else {
                    dump_visited[de.ino / 8] |= (uint8_t)(1u << (de.ino % 8));
                    dump_walk(de.ino, plen + 1 + nl, depth + 1);
                }
            }
        }
        dump_path[plen] = 0;
    }
}

static void dump_flags_line(void)
{
    const struct ext2_fs *f = &dump_fs;
    printf("EXT2 rev=%" PRIu32 " block=%" PRIu32 " inode=%" PRIu32 " blocks=%" PRIu32 " inodes=%" PRIu32
           " first_ino=%" PRIu32 " reserved_gdt=%" PRIu32 "\n",
           f->rev, f->block_size, f->inode_size, f->blocks_count, f->inodes_count, f->first_ino,
           (f->compat & EXT2_COMPAT_RESIZE_INODE) ? f->reserved_gdt : 0u);
    printf("EXT2 groups=%" PRIu32 " blocks_per_group=%" PRIu32 " inodes_per_group=%" PRIu32 " label='%s'\n",
           f->groups, f->blocks_per_group, f->inodes_per_group, f->label);
}

int main(int argc, char **argv)
{
    int a = 1;
    if (a + 1 < argc && strcmp(argv[a], "-l") == 0) {
        dump_limit = strtoull(argv[a + 1], 0, 10);
        a += 2;
    }
    if (a + 1 != argc) {
        fprintf(stderr, "usage: ext2dump [-l <bytes>] <filesystem image>\n");
        return 3;
    }
    if (dump_sha_selftest() != 0)
        return 3;
    dump_fd = open(argv[a], O_RDONLY);
    struct stat st;
    if (dump_fd < 0 || fstat(dump_fd, &st) != 0) {
        perror(argv[a]);
        return 3;
    }
    dump_size = (uint64_t)st.st_size;
    struct ext2_ops ops = { dump_read, 0 };
    ext2_init(&dump_fs, &dump_bufs);
    int r = ext2_mount(&dump_fs, &ops, dump_size);
    printf("EXT2 mount: %s%s%s%s\n", ext2_err_str(r), r ? " (" : "", r && dump_fs.why ? dump_fs.why : "",
           r ? ")" : "");
    printf("EXT2 features compat=0x%" PRIx32 " incompat=0x%" PRIx32 " ro_compat=0x%" PRIx32
           "; unknown: compat=0x%" PRIx32 " incompat=0x%" PRIx32 " ro_compat=0x%" PRIx32 "\n",
           dump_fs.compat, dump_fs.incompat, dump_fs.ro_compat, dump_fs.compat_unknown, dump_fs.incompat_unknown,
           dump_fs.ro_compat_unknown);
    if (r == EXT2_EINCOMPAT)
        printf("EXT2 policy: not mounted (unknown INCOMPAT 0x%" PRIx32 ")\n", dump_fs.incompat_unknown);
    else if (r)
        printf("EXT2 policy: not mounted (%s)\n", ext2_err_str(r));
    else if (dump_fs.readonly)
        printf("EXT2 policy: read-only (unknown RO_COMPAT 0x%" PRIx32 ")\n", dump_fs.ro_compat_unknown);
    else
        printf("EXT2 policy: read-write\n");
    printf("EXT2 state: %s, error bit %s (0x%x)\n", dump_fs.state_clean ? "clean" : "not clean",
           dump_fs.state_error ? "set" : "not set", (unsigned)dump_fs.state);
    if (r) {
        printf("DONE entries=0 errors=0 bad_reads=%lu\n", dump_bad_reads);
        return dump_bad_reads ? 4 : 1;
    }
    dump_flags_line();
    if (dump_fs.compat & EXT2_COMPAT_RESIZE_INODE) {
        struct ext2_inode ri;
        memset(&ri, 0, sizeof ri);
        int rr = ext2_inode_get(&dump_fs, EXT2_RESIZE_INO, &ri);
        if (rr)
            dump_error("<7>", rr);
        else
            printf("EXT2 resize_inode: inode 7 mode %06o size %" PRIu64 " links %u, %" PRIu32
                   " reserved descriptor blocks (report only)\n",
                   (unsigned)ri.mode, ri.size, (unsigned)ri.links, dump_fs.reserved_gdt);
    }
    dump_visited = calloc((size_t)dump_fs.inodes_count / 8 + 1, 1);
    if (!dump_visited) {
        fprintf(stderr, "ext2dump: out of memory\n");
        return 3;
    }
    struct ext2_inode root;
    memset(&root, 0, sizeof root);
    r = ext2_inode_get(&dump_fs, EXT2_ROOT_INO, &root);
    if (r) {
        dump_error("", r);
    } else {
        dump_path[0] = 0;
        dump_line("", &root);
        dump_visited[EXT2_ROOT_INO / 8] |= (uint8_t)(1u << (EXT2_ROOT_INO % 8));
        dump_walk(EXT2_ROOT_INO, 0, 0);
    }
    printf("DONE entries=%lu errors=%lu bad_reads=%lu\n", dump_entries, dump_errors, dump_bad_reads);
    free(dump_visited);
    close(dump_fd);
    if (dump_bad_reads)
        return 4;
    return dump_errors ? 2 : 0;
}
