/* Unit tests for 1.4 stage 12 of kernel/shell/shell.c (docs/plans/1.4.md,
 * C10-C12, D19, G2): with the disk part's info, "sync" and "disk" join the
 * help lists (the files section, after "du"), "version" says the mode of
 * /home, "cd" alone goes to /home/user in Disk mode (to /home when the
 * volume has none), "df" adds the disk's block (Live mode word for word as
 * before), "du" counts the disk's KiB, "disk" shows the boot's findings,
 * "mv" between filesystems prints its own line instead of "error 18", and
 * a full disk or a file at the disk's size limit gets the disk's words.
 * Without info every output is as in stage 10. */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "kernel/fs/fs_errno.h"
#include "kernel/shell/shell.h"
#include "tests/unit/unit.h"

static char out[8192];
static int errors;
static struct memory_stats mem;
static struct cpu_info cpu;
static struct shell_context ctx;
static struct shell_extras extras;
static struct shell_state st;
static struct shell_session ss;
static struct shell_files files;
static struct shell_storage storage;
static struct shell_disk disk;
static struct history hist;
static struct shell_disk_info info;

static void capture(void *c, const char *text, enum shell_text kind)
{
    (void)c;
    if (kind == SHELL_ERROR)
        errors++;
    strncat(out, text, sizeof(out) - strlen(out) - 1);
}

static const struct shell_output sink = { capture, NULL };

static enum shell_result run(const char *cmd)
{
    static char line[512];
    strncpy(line, cmd, sizeof(line) - 1);
    line[sizeof(line) - 1] = '\0';
    out[0] = '\0';
    errors = 0;
    return shell_execute(line, &ctx, &sink);
}

/* ---- fakes ---- */

static char chdir_seen[64];
static int chdir_user_answer;       /* the answer for /home/user */
static bool fake_removed(void *c) { (void)c; return false; }

static int fake_chdir(void *c, const char *p)
{
    (void)c;
    snprintf(chdir_seen + strlen(chdir_seen), sizeof(chdir_seen) - strlen(chdir_seen), "%s;", p);
    return strcmp(p, "/home/user") == 0 ? chdir_user_answer : 0;
}

static int mkdir_answer;
static int fake_mkdir(void *c, const char *p) { (void)c; (void)p; return mkdir_answer; }

static int rename_answer;
static int fake_rename(void *c, const char *a, const char *b) { (void)c; (void)a; (void)b; return rename_answer; }

static void fake_df(void *c, struct shell_dfinfo *d)
{
    (void)c;
    d->nodes = 5;
    d->max_nodes = 1024;
    d->pages = 2;
    d->budget = 8192;
}

static struct shell_du du_give;
static int fake_du_open(void *c, const char *p, struct shell_du *t)
{
    (void)c; (void)p;
    *t = du_give;
    return 0;
}
static int fake_du_next(void *c, struct shell_du *t) { (void)c; (void)t; return 0; }
static void fake_du_close(void *c) { (void)c; }

static int64_t write_answer;
static int fake_out_open(void *c, const char *p, bool append) { (void)c; (void)p; (void)append; return 0; }
static int64_t fake_out_write(void *c, const uint8_t *b, uint32_t n) { (void)c; (void)b; (void)n; return write_answer; }
static void fake_out_close(void *c) { (void)c; }
static bool fake_out_is(void *c, const char *p) { (void)c; (void)p; return false; }

static enum shell_sync_end fake_sync(void *c, uint64_t *blocks)
{
    (void)c;
    *blocks = 0;
    return SHELL_SYNC_SAVED;
}

static void fake_info(void *c, struct shell_disk_info *o)
{
    (void)c;
    *o = info;
}

static bool on_disk_answer;
static bool fake_on_disk(void *c, const char *p) { (void)c; (void)p; return on_disk_answer; }

static void reset(bool with_info)
{
    memset(&mem, 0, sizeof(mem));
    memset(&cpu, 0, sizeof(cpu));
    memset(&ctx, 0, sizeof(ctx));
    memset(&extras, 0, sizeof(extras));
    memset(&files, 0, sizeof(files));
    memset(&storage, 0, sizeof(storage));
    memset(&disk, 0, sizeof(disk));
    memset(&info, 0, sizeof(info));
    history_init(&hist);
    ctx.version = "1.4";
    ctx.mem = &mem;
    ctx.cpu = &cpu;
    ctx.page_size = 4096;
    extras.hostname = "madire";
    extras.history = &hist;
    ctx.extras = &extras;
    shell_state_init(&st);
    ctx.state = &st;
    shell_session_init(&ss);
    ctx.session = &ss;
    files.cwd_removed = fake_removed;
    files.chdir = fake_chdir;
    files.mkdir = fake_mkdir;
    ctx.files = &files;
    storage.rename = fake_rename;
    storage.df = fake_df;
    storage.du_open = fake_du_open;
    storage.du_next = fake_du_next;
    storage.du_close = fake_du_close;
    storage.out_open = fake_out_open;
    storage.out_write = fake_out_write;
    storage.out_close = fake_out_close;
    storage.out_is = fake_out_is;
    ctx.storage = &storage;
    disk.sync = fake_sync;
    disk.info = with_info ? fake_info : NULL;
    disk.on_disk = with_info ? fake_on_disk : NULL;
    ctx.disk = &disk;
    chdir_seen[0] = 0;
    chdir_user_answer = 0;
    mkdir_answer = 0;
    rename_answer = 0;
    on_disk_answer = false;
    write_answer = 0;
    memset(&du_give, 0, sizeof(du_give));
}

static void disk_mounted(enum shell_disk_mode m)
{
    info.mode = m;
    snprintf(info.label, sizeof(info.label), "madire-test");
    info.disk = 0;
    info.partition = 1;
    info.block_size = 1024;
    info.blocks = 4096;
    info.blocks_free = 3990;
    info.inodes = 256;
    info.inodes_free = 233;
    info.controller = true;
    info.has_disk = true;
    info.where = "00:05.0";
    info.model = "QEMU HARDDISK";
    info.size = "5 MiB";
    info.table = "MBR, 1 partition";
    info.guard = "ALLOWED on partition 1";
    info.use = "ext2 'madire-test' mounted read-write on /home";
    info.port = 0;
}

/* ---- cases ---- */

static void test_help_lists(void)
{
    static char without[8192];
    reset(false);
    CHECK_EQ(run("help"), SHELL_OK);
    snprintf(without, sizeof(without), "%s", out);
    CHECK(strstr(without, "sync") == NULL);
    reset(true);
    CHECK_EQ(run("help"), SHELL_OK);
    CHECK(strstr(out, "\n  pwd  cd  ls  mkdir  rmdir  touch  stat  cat  rm  cp  mv  df  du  sync  disk\n") != NULL);
    CHECK_EQ(run("help files"), SHELL_OK);
    CHECK(strstr(out, "  du       Show the pages under a folder: du [path]\n"
                      "  sync     Save changed files to the disk\n"
                      "  disk     Show disks and how they are used\n") != NULL);
    CHECK_EQ(run("help disk"), SHELL_OK);
    CHECK(strcmp(out, "disk: Show disks and how they are used\nUsage: disk\n") == 0);
    CHECK_EQ(run("disk now"), SHELL_BAD_ARGS);          /* no argument */
    reset(false);
    CHECK_EQ(run("disk"), SHELL_UNKNOWN);               /* without info: unknown, as before */
}

static void test_version(void)
{
    reset(false);
    CHECK_EQ(run("version"), SHELL_OK);
    CHECK(strstr(out, "\nLive mode (files in memory only)\n") != NULL);
    reset(true);
    CHECK_EQ(run("version"), SHELL_OK);
    CHECK(strstr(out, "\nLive mode (files in memory only)\n") != NULL);   /* info: Live */
    disk_mounted(SHELL_DISK_RW);
    CHECK_EQ(run("version"), SHELL_OK);
    CHECK(strstr(out, "\nDisk mode (/home is saved on the disk)\n") != NULL);
    disk_mounted(SHELL_DISK_RO);
    CHECK_EQ(run("version"), SHELL_OK);
    CHECK(strstr(out, "\nDisk mode (/home is read-only)\n") != NULL);
}

static void test_cd_home(void)
{
    reset(true);
    CHECK_EQ(run("cd"), SHELL_OK);
    CHECK(strcmp(chdir_seen, "/home;") == 0);           /* Live mode: /home, as before */
    reset(true);
    disk_mounted(SHELL_DISK_RW);
    CHECK_EQ(run("cd"), SHELL_OK);
    CHECK(strcmp(chdir_seen, "/home/user;") == 0);
    reset(true);
    disk_mounted(SHELL_DISK_RO);
    chdir_user_answer = -FS_ENOENT;                     /* a volume without /home/user */
    CHECK_EQ(run("cd"), SHELL_OK);
    CHECK(strcmp(chdir_seen, "/home/user;/home;") == 0);
    CHECK_EQ(errors, 0);
    reset(true);
    disk_mounted(SHELL_DISK_RW);
    CHECK_EQ(run("cd /tmp"), SHELL_OK);
    CHECK(strcmp(chdir_seen, "/tmp;") == 0);
}

static void test_df(void)
{
    static char live[2048];
    reset(false);
    CHECK_EQ(run("df"), SHELL_OK);
    snprintf(live, sizeof(live), "%s", out);
    reset(true);
    CHECK_EQ(run("df"), SHELL_OK);
    CHECK(strcmp(out, live) == 0);                      /* Live mode: word for word */
    disk_mounted(SHELL_DISK_RW);
    CHECK_EQ(run("df"), SHELL_OK);
    CHECK(strncmp(out, live, strlen(live)) == 0);       /* the RAM block first, unchanged */
    CHECK(strcmp(out + strlen(live),
                 "Filesystem: ext2 'madire-test' (disk 0, partition 1), mounted on /home\n"
                 "Blocks:     106 used of 4096 (3990 free), 1 KiB each\n"
                 "Inodes:     23 used of 256 (233 free)\n") == 0);
    disk_mounted(SHELL_DISK_RO);
    info.block_size = 4096;
    CHECK_EQ(run("df"), SHELL_OK);
    CHECK(strstr(out, "Filesystem: ext2 'madire-test' (disk 0, partition 1), mounted on /home, read-only\n"
                      "Blocks:     106 used of 4096 (3990 free), 4 KiB each\n") != NULL);
}

static void test_du(void)
{
    reset(true);
    du_give.pages = 3;
    du_give.files = 2;
    CHECK_EQ(run("du"), SHELL_OK);
    CHECK(strcmp(out, "3 pages (12 KiB) in 2 files and 0 folders\n") == 0);     /* as before */
    du_give.pages = 0;
    du_give.kib = 7;
    du_give.files = 2;
    du_give.folders = 7;
    du_give.disk = true;
    CHECK_EQ(run("du"), SHELL_OK);
    CHECK(strcmp(out, "7 KiB on the disk in 2 files and 7 folders\n") == 0);
    du_give.kib = 1;
    du_give.files = 1;
    du_give.folders = 0;
    CHECK_EQ(run("du note.txt"), SHELL_OK);
    CHECK(strcmp(out, "1 KiB on the disk in 1 file and 0 folders\n") == 0);
    du_give.pages = 1;
    du_give.kib = 5;
    du_give.files = 3;
    du_give.folders = 2;
    CHECK_EQ(run("du /"), SHELL_OK);
    CHECK(strcmp(out, "1 page (4 KiB) in memory and 5 KiB on the disk, in 3 files and 2 folders\n") == 0);
}

static void test_disk(void)
{
    reset(true);
    CHECK_EQ(run("disk"), SHELL_OK);
    CHECK(strcmp(out, "disk: no AHCI controller; no disk in use (Live mode).\n") == 0);
    info.controller = true;
    info.summary = "AHCI 00:05.0, no disk (Live mode)";
    CHECK_EQ(run("disk"), SHELL_OK);
    CHECK(strcmp(out, "disk: AHCI 00:05.0, no disk (Live mode).\n") == 0);
    disk_mounted(SHELL_DISK_RW);
    CHECK_EQ(run("disk"), SHELL_OK);
    CHECK(strcmp(out, "Disk on port 0: QEMU HARDDISK, 5 MiB (AHCI 00:05.0)\n"
                      "  Partitions:  MBR, 1 partition\n"
                      "  Write guard: ALLOWED on partition 1\n"
                      "  Use:         ext2 'madire-test' mounted read-write on /home (Disk mode)\n") == 0);
    disk_mounted(SHELL_DISK_RO);
    info.more_disks = true;
    CHECK_EQ(run("disk"), SHELL_OK);
    CHECK(strstr(out, "  Use:         ext2 'madire-test' mounted read-only on /home (Disk mode, read-only)\n"
                      "  More disks:  found, not used (one disk in 1.4)\n") != NULL);
    disk_mounted(SHELL_DISK_LIVE);                      /* refused by the guard: not mounted */
    info.guard = "REFUSED: (b) no valid test marker";
    info.use = "read only, not mounted";
    CHECK_EQ(run("disk"), SHELL_OK);
    CHECK(strstr(out, "  Write guard: REFUSED: (b) no valid test marker\n"
                      "  Use:         read only, not mounted (Live mode)\n") != NULL);
    CHECK_EQ(errors, 0);
}

static void test_mv_exdev(void)
{
    reset(true);
    rename_answer = -FS_EXDEV;
    CHECK_EQ(run("mv -y note.txt /tmp/note.txt"), SHELL_BAD_ARGS);
    CHECK(strcmp(out, "mv: cannot move between filesystems (copy, then remove).\n") == 0);
    CHECK_EQ(errors, 1);
    CHECK(strstr(out, "error 18") == NULL);
}

static void test_disk_words(void)
{
    reset(true);
    mkdir_answer = -FS_ENOSPC;
    CHECK_EQ(run("mkdir a"), SHELL_BAD_ARGS);
    CHECK(strcmp(out, "mkdir: 'a': no room (1024 at most).\n") == 0);   /* RAM: as before */
    disk_mounted(SHELL_DISK_RW);
    on_disk_answer = true;
    CHECK_EQ(run("mkdir a"), SHELL_BAD_ARGS);
    CHECK(strcmp(out, "mkdir: 'a': no room left on the disk.\n") == 0);
    write_answer = -FS_EFBIG;
    CHECK_EQ(run("echo hi > big.txt"), SHELL_OK);
    CHECK(strstr(out, "Output to 'big.txt': the file is at the disk's size limit.\n") != NULL);
    on_disk_answer = false;                             /* in /tmp: the RAM budget's words */
    CHECK_EQ(run("echo hi > big.txt"), SHELL_OK);
    CHECK(strstr(out, "Output to 'big.txt': the file is at its size limit (2 MiB).\n") != NULL);
}

int main(void)
{
    printf("shell_disk12 (Disk mode: help lists, version, cd, df, du, disk, mv, the disk's words)\n");
    RUN(test_help_lists);
    RUN(test_version);
    RUN(test_cd_home);
    RUN(test_df);
    RUN(test_du);
    RUN(test_disk);
    RUN(test_mv_exdev);
    RUN(test_disk_words);
    return unit_report("shell_disk12");
}
