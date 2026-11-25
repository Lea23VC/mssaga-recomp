/*
 * The memory card, as a folder: ~/.local/share/mssaga/memcard1 ($XDG_DATA_HOME/mssaga/memcard1;
 * MSSAGA_MEMCARD=dir chooses another). Only port 0 (memory card slot 1) has a card.
 *
 * MS Saga uses the classic libmc (SDK 2.8): every sceMc* call starts a command and returns 0 at once; the game
 * then polls sceMcSync(mode, &cmd, &result), which says 1 ("finished") once, with the command's number and
 * result, and -1 ("nothing running") after that. The public functions are replaced here (port/hle.txt, the
 * executable's own names) by native ones that finish at once and leave their result for sceMcSync.
 *
 * Command numbers (cmd) and results follow libmc.h: 1 GetInfo, 2 Open, 3 Close, 4 Seek, 5 Read, 6 Write,
 * 0xA Flush, 0xB Mkdir, 0xC Chdir, 0xD GetDir, 0xE SetFileInfo, 0xF Delete, 0x10 Format, 0x11 Unformat,
 * 0x12 GetEntSpace, 0x13 Rename. Results: >= 0 done, -1 a new card, -2 not formatted, -3 card full,
 * -4 no such entry (or exists already, for Mkdir), -5 not allowed, -6 directory not empty, -7 too many files
 * open, -10 no card.
 * GetDir entries (sceMcTblGetDir, 64 bytes): created +0, modified +8 ({0, s, m, h, day, month, u16 year}),
 * size +16, attributes (u16) +20, name[32] +32.
 */
#define _GNU_SOURCE
#include <dirent.h>
#include <fnmatch.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "r5900.h"
#include "runtime.h"
#include "platform.h"

enum { NOCARD = -10, NOENT = -4, TOOMANY = -7 };
enum { CMD_INFO = 1, CMD_OPEN, CMD_CLOSE, CMD_SEEK, CMD_READ, CMD_WRITE, CMD_FLUSH = 0xA, CMD_MKDIR, CMD_CHDIR,
       CMD_GETDIR, CMD_SETINFO, CMD_DELETE, CMD_FORMAT, CMD_UNFORMAT, CMD_ENTSPACE, CMD_RENAME };
#define FREE_CLUSTERS 8000          /* KB free on an 8 MB card */
#define MAX_FILES 8

static char root[PATH_MAX];
static char cwd[PATH_MAX] = "/";    /* on the card */
static FILE *files[MAX_FILES];
static int pending, pending_cmd, pending_result;
static int first_info = 1;          /* the first GetInfo reports a card that was changed (-1), like a real one */

static void setup(void) {
    if (root[0]) return;
    const char *e = getenv("MSSAGA_MEMCARD");
    if (e && *e) snprintf(root, sizeof root, "%s", e);
    else {
        char p[PATH_MAX - 16];
        platform_data_dir(p, sizeof p);
        snprintf(root, sizeof root, "%s/memcard1", p);
    }
    mkdir(root, 0755);
    rt_log("mc", "memory card in %s", root);
}

/* the command's result, for sceMcSync; the call itself returns 0 (started) */
static void done(Ctx *c, int cmd, int result) {
    pending = 1;
    pending_cmd = cmd;
    pending_result = result;
    if (rt_verbose > 1) rt_log("mc", "cmd %X -> %d", cmd, result);
    SET64(2, 0);
}

static void guest_str(Ctx *c, u32 a, char *out, size_t n) {
    size_t i = 0;
    for (; i + 1 < n; i++) { char ch = (char)rd8(c, a + (u32)i); out[i] = ch; if (!ch) break; }
    out[n - 1] = 0;
}

/* A card path (absolute, or relative to the current directory) to a host path, "." and ".." resolved. */
static void host_path(const char *card, char *out, size_t n) {
    char full[2 * PATH_MAX];
    if (card[0] == '/') snprintf(full, sizeof full, "%s", card);
    else snprintf(full, sizeof full, "%s%s%s", cwd, cwd[strlen(cwd) - 1] == '/' ? "" : "/", card);
    char parts[64][64];
    int np = 0;
    char *save = NULL;
    for (char *t = strtok_r(full, "/", &save); t; t = strtok_r(NULL, "/", &save)) {
        if (!strcmp(t, ".")) continue;
        if (!strcmp(t, "..")) { if (np) np--; continue; }
        if (np < 64) snprintf(parts[np++], 64, "%s", t);
    }
    size_t len = (size_t)snprintf(out, n, "%s", root);
    for (int i = 0; i < np && len < n; i++) len += (size_t)snprintf(out + len, n - len, "/%s", parts[i]);
}

static void card_time(Ctx *c, u32 a, time_t t) {
    struct tm tm;
    localtime_r(&t, &tm);
    wr8(c, a, 0);
    wr8(c, a + 1, (u8)tm.tm_sec);
    wr8(c, a + 2, (u8)tm.tm_min);
    wr8(c, a + 3, (u8)tm.tm_hour);
    wr8(c, a + 4, (u8)tm.tm_mday);
    wr8(c, a + 5, (u8)(tm.tm_mon + 1));
    wr16(c, a + 6, (u16)(tm.tm_year + 1900));
}

/* ------------------------------------------------------------------------------- libmc */

void hle_sceMcInit(Ctx *c) { setup(); SET64(2, 0); }
void hle_sceMcEnd(Ctx *c) { SET64(2, 0); }
void hle_sceMcZero(Ctx *c) { SET64(2, 0); }           /* ChangeThreadPriority, UdCheckNewCard */
void hle_sceMcGetSlotMax(Ctx *c) { SET64(2, 1); }

/* int sceMcSync(int mode, int *cmd, int *result): 1 finished, -1 nothing running */
void hle_sceMcSync(Ctx *c) {
    u32 cmd = RW(5), result = RW(6);
    if (!pending) { SET64(2, -1); return; }
    pending = 0;
    if (cmd) wr32(c, cmd, (u32)pending_cmd);
    if (result) wr32(c, result, (u32)pending_result);
    SET64(2, 1);
}

/* sceMcGetInfo(port, slot, int *type, int *free, int *format) */
void hle_sceMcGetInfo(Ctx *c) {
    setup();
    u32 port = RW(4), type = RW(6), free_ = RW(7), format = RW(8);
    static int told[2];
    if (port < 2 && !told[port]++) rt_log("mc", "GetInfo port %u: %s", port, port ? "no card" : "a card");
    if (port != 0) {
        if (type) wr32(c, type, 0);
        done(c, CMD_INFO, NOCARD);
        return;
    }
    if (type) wr32(c, type, 2);                       /* PS2 memory card */
    if (free_) wr32(c, free_, FREE_CLUSTERS);
    if (format) wr32(c, format, 1);
    done(c, CMD_INFO, first_info ? -1 : 0);
    first_info = 0;
}

/* sceMcOpen(port, slot, name, mode): mode 1 read, 2 write, 3 both, 0x200 create, 0x400 truncate */
void hle_sceMcOpen(Ctx *c) {
    setup();
    char name[256], path[PATH_MAX];
    u32 mode = RW(7);
    guest_str(c, RW(6), name, sizeof name);
    if (RW(4) != 0) { done(c, CMD_OPEN, NOCARD); return; }
    host_path(name, path, sizeof path);
    int fd = 0;
    while (fd < MAX_FILES && files[fd]) fd++;
    if (fd == MAX_FILES) { done(c, CMD_OPEN, TOOMANY); return; }
    struct stat st;
    int exists = stat(path, &st) == 0;
    if (exists && S_ISDIR(st.st_mode)) { done(c, CMD_OPEN, -5); return; }
    if (!exists && !(mode & 0x200)) { done(c, CMD_OPEN, NOENT); return; }
    FILE *f = fopen(path, !exists || (mode & 0x400) ? "w+b" : (mode & 2) ? "r+b" : "rb");
    rt_log("mc", "open %s (mode %X) -> %s", name, mode, f ? "ok" : "failed");
    if (!f) { done(c, CMD_OPEN, NOENT); return; }
    files[fd] = f;
    done(c, CMD_OPEN, fd);
}

static FILE *file_of(u32 fd) { return fd < MAX_FILES ? files[fd] : NULL; }

void hle_sceMcClose(Ctx *c) {
    FILE *f = file_of(RW(4));
    if (f) { fclose(f); files[RW(4)] = NULL; }
    done(c, CMD_CLOSE, f ? 0 : NOENT);
}

void hle_sceMcFlush(Ctx *c) {
    FILE *f = file_of(RW(4));
    if (f) fflush(f);
    done(c, CMD_FLUSH, f ? 0 : NOENT);
}

/* sceMcSeek(fd, offset, origin 0 start / 1 current / 2 end) -> new position */
void hle_sceMcSeek(Ctx *c) {
    FILE *f = file_of(RW(4));
    if (!f) { done(c, CMD_SEEK, NOENT); return; }
    fseek(f, (long)RSW(5), RW(6) == 1 ? SEEK_CUR : RW(6) == 2 ? SEEK_END : SEEK_SET);
    done(c, CMD_SEEK, (int)ftell(f));
}

static void file_io(Ctx *c, int write) {        /* fd, buffer, size -> bytes moved */
    FILE *f = file_of(RW(4));
    u32 buf = RW(5), size = RW(6);
    int cmd = write ? CMD_WRITE : CMD_READ;
    if (!f) { done(c, cmd, NOENT); return; }
    u8 *tmp = malloc(size ? size : 1);
    size_t n;
    if (write) {
        for (u32 i = 0; i < size; i++) tmp[i] = rd8(c, buf + i);
        n = fwrite(tmp, 1, size, f);
    } else {
        n = fread(tmp, 1, size, f);
        for (u32 i = 0; i < n; i++) wr8(c, buf + i, tmp[i]);
    }
    free(tmp);
    done(c, cmd, (int)n);
}

void hle_sceMcRead(Ctx *c) { file_io(c, 0); }
void hle_sceMcWrite(Ctx *c) { file_io(c, 1); }

/* sceMcMkdir(port, slot, name) */
void hle_sceMcMkdir(Ctx *c) {
    setup();
    char name[256], path[PATH_MAX];
    guest_str(c, RW(6), name, sizeof name);
    if (RW(4) != 0) { done(c, CMD_MKDIR, NOCARD); return; }
    host_path(name, path, sizeof path);
    int r = mkdir(path, 0755) == 0 ? 0 : NOENT;
    rt_log("mc", "mkdir %s -> %d", name, r);
    done(c, CMD_MKDIR, r);
}

/* sceMcChdir(port, slot, new directory, buffer for the old one) */
void hle_sceMcChdir(Ctx *c) {
    setup();
    char name[256], path[PATH_MAX];
    guest_str(c, RW(6), name, sizeof name);
    if (RW(4) != 0) { done(c, CMD_CHDIR, NOCARD); return; }
    if (RW(7)) for (u32 i = 0; i <= strlen(cwd); i++) wr8(c, RW(7) + i, (u8)cwd[i]);
    host_path(name, path, sizeof path);
    struct stat st;
    if (stat(path, &st) || !S_ISDIR(st.st_mode)) { done(c, CMD_CHDIR, NOENT); return; }
    snprintf(cwd, sizeof cwd, "%s", path + strlen(root));
    if (!cwd[0]) snprintf(cwd, sizeof cwd, "/");
    done(c, CMD_CHDIR, 0);
}

static void put_entry(Ctx *c, u32 e, const char *name, const char *path) {
    struct stat st;
    if (stat(path, &st)) memset(&st, 0, sizeof st);
    for (u32 i = 0; i < 64; i++) wr8(c, e + i, 0);
    card_time(c, e, st.st_ctime);
    card_time(c, e + 8, st.st_mtime);
    wr32(c, e + 16, S_ISDIR(st.st_mode) ? 0 : (u32)st.st_size);
    wr16(c, e + 20, S_ISDIR(st.st_mode) ? 0x8427 : 0x8497);
    for (int i = 0; i < 31 && name[i]; i++) wr8(c, e + 32 + i, (u8)name[i]);
}

/* sceMcGetDir(port, slot, pattern ("/DIR/ *"), mode (0 first, 1 next), max entries, table) -> count */
void hle_sceMcGetDir(Ctx *c) {
    setup();
    char pattern[256], dir[PATH_MAX];
    guest_str(c, RW(6), pattern, sizeof pattern);
    u32 mode = RW(7), max = RW(8), table = RW(9);
    static int skip;
    if (RW(4) != 0) { done(c, CMD_GETDIR, NOCARD); return; }
    if (mode == 0) skip = 0;
    char *slash = strrchr(pattern, '/'), leaf[256];
    if (slash) { snprintf(leaf, sizeof leaf, "%s", slash + 1); *slash = 0; }
    else { snprintf(leaf, sizeof leaf, "%s", pattern); pattern[0] = 0; }
    host_path(pattern[0] ? pattern : (slash ? "/" : "."), dir, sizeof dir);
    DIR *d = opendir(dir);
    int n = 0, seen = 0;
    if (d) {
        for (struct dirent *de; (de = readdir(d)) && (u32)n < max;) {
            if (fnmatch(leaf, de->d_name, 0)) continue;
            if (seen++ < skip) continue;
            char path[2 * PATH_MAX];
            snprintf(path, sizeof path, "%s/%s", dir, de->d_name);
            put_entry(c, table + 64 * (u32)n, de->d_name, path);
            n++;
        }
        closedir(d);
    }
    skip += n;
    rt_log("mc", "getdir %s/%s -> %d", pattern, leaf, n);
    done(c, CMD_GETDIR, n);
}

/* sceMcDelete(port, slot, name) */
void hle_sceMcDelete(Ctx *c) {
    setup();
    char name[256], path[PATH_MAX];
    guest_str(c, RW(6), name, sizeof name);
    if (RW(4) != 0) { done(c, CMD_DELETE, NOCARD); return; }
    host_path(name, path, sizeof path);
    int r = remove(path) == 0 ? 0 : NOENT;
    rt_log("mc", "delete %s -> %d", name, r);
    done(c, CMD_DELETE, r);
}

/* sceMcRename(port, slot, old name, new name (in the same directory)) */
void hle_sceMcRename(Ctx *c) {
    setup();
    char from[256], to[256], a[PATH_MAX], b[PATH_MAX];
    guest_str(c, RW(6), from, sizeof from);
    guest_str(c, RW(7), to, sizeof to);
    if (RW(4) != 0) { done(c, CMD_RENAME, NOCARD); return; }
    host_path(from, a, sizeof a);
    snprintf(b, sizeof b, "%s", a);
    char *s = strrchr(b, '/');
    if (s) snprintf(s + 1, sizeof b - (size_t)(s + 1 - b), "%s", to);
    done(c, CMD_RENAME, rename(a, b) == 0 ? 0 : NOENT);
}

void hle_sceMcSetFileInfo(Ctx *c) { done(c, CMD_SETINFO, RW(4) ? NOCARD : 0); }
void hle_sceMcFormat(Ctx *c) { setup(); done(c, CMD_FORMAT, RW(4) ? NOCARD : 0); }
void hle_sceMcUnformat(Ctx *c) { done(c, CMD_UNFORMAT, RW(4) ? NOCARD : 0); }
void hle_sceMcGetEntSpace(Ctx *c) { done(c, CMD_ENTSPACE, RW(4) ? NOCARD : 32); }
