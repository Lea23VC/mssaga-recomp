/*
 * MS Saga: cheat codes, as a PS2 cheat device would apply them: at each vertical blank (kernel.c, on the game's side,
 * after the pads are read), each code that is on writes its values into the game's memory.
 *
 * The list: the MS Saga section of the CheatDevicePS2 database (cheats_db.c, tools/cheats2c.py), then the player's own
 * `cheats.txt` in the settings folder, same format: a line with a name, the code lines under it ("aaaaaaaa vvvvvvvv",
 * raw / decrypted); a name with no code lines starts a group. Which are on: `cheats_on` in the settings folder, one name
 * per line ("group/name"). None of MS Saga's codes patch the game's instructions (they would do nothing here: those are
 * compiled); all write its data.
 *
 * Code types (the raw CodeBreaker / Action Replay ones the list uses):
 *   0aaaaaaa 000000vv   8-bit write          1aaaaaaa 0000vvvv   16-bit write       2aaaaaaa vvvvvvvv   32-bit write
 *   4aaaaaaa nnnnssss + vvvvvvvv iiiiiiii   n 32-bit writes, s words apart, the value growing by i
 *   Daaaaaaa 0n0tvvvv   if the 16 bits at a compare (t: 0 equal, 1 not equal, 2 less, 3 greater) with v, the next n
 *                       lines (0: one) run, else they are skipped (the button tests: the pad's buttons, active low)
 *   9aaaaaaa ........   a cheat device's hook: nothing to do here
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "r5900.h"
#include "runtime.h"

extern const char cheats_db[];                                      /* cheats_db.c */
void display_config_path(char *p, size_t n, const char *name);     /* display.c */

typedef struct { u32 a, v; } Code;
typedef struct { char name[96], group[96]; Code *codes; int n; int on; } Cheat;

static Cheat *cheats;
static int ncheats, loaded;
static volatile int any_on;

static void parse(const char *text) {
    char group[96] = "General";
    const char *p = text;
    Cheat *cur = NULL;
    while (*p) {
        const char *e = strchr(p, '\n');
        size_t len = e ? (size_t)(e - p) : strlen(p);
        char line[160];
        if (len >= sizeof line) len = sizeof line - 1;
        memcpy(line, p, len);
        line[len] = 0;
        while (len && (line[len - 1] == '\r' || line[len - 1] == ' ')) line[--len] = 0;
        p = e ? e + 1 : p + strlen(p);
        if (!len || line[0] == '"' || line[0] == '/') continue;     /* blank, a game title, a comment */
        unsigned a, v;
        char rest;
        if (sscanf(line, "%8x %8x%c", &a, &v, &rest) == 2 && strlen(line) == 17) {
            if (!cur) continue;
            cur->codes = realloc(cur->codes, (size_t)(cur->n + 1) * sizeof(Code));
            cur->codes[cur->n++] = (Code){a, v};
            continue;
        }
        /* a name: the one before it without codes was a group's */
        if (cur && cur->n == 0) {
            snprintf(group, sizeof group, "%s", cur->name);
            size_t gl = strlen(group);
            if (gl > 6 && !strcmp(group + gl - 6, " Codes")) group[gl - 6] = 0;
            ncheats--;
        }
        cheats = realloc(cheats, (size_t)(ncheats + 1) * sizeof(Cheat));
        cur = &cheats[ncheats++];
        memset(cur, 0, sizeof *cur);
        snprintf(cur->name, sizeof cur->name, "%s", line);
        snprintf(cur->group, sizeof cur->group, "%s", group);
    }
    if (cur && cur->n == 0) ncheats--;                              /* a group with nothing under it, last */
}

static void save_on(void) {
    char path[1100];
    display_config_path(path, sizeof path, "cheats_on");
    FILE *f = fopen(path, "w");
    if (!f) return;
    for (int i = 0; i < ncheats; i++)
        if (cheats[i].on) fprintf(f, "%s/%s\n", cheats[i].group, cheats[i].name);
    fclose(f);
}

static void load(void) {
    if (loaded) return;
    loaded = 1;
    parse(cheats_db);
    /* The database's "Max EXP" writes 99,999,999, twenty times the game's maximum: the EXP table (0x3B7438, 49 steps,
       the one "Gain 1 Exp For Level 50" rewrites) ends at 5,000,000 for level 50, and with more the party screen loses
       the character's portrait. Here they write that maximum. */
    for (int i = 0; i < ncheats; i++)
        if (!strcmp(cheats[i].name, "Max EXP"))
            for (int k = 0; k < cheats[i].n; k++)
                if (cheats[i].codes[k].a >> 28 == 2 && cheats[i].codes[k].v == 99999999u) cheats[i].codes[k].v = 5000000u;
    char path[1100];
    display_config_path(path, sizeof path, "cheats.txt");             /* the player's own codes */
    FILE *f = fopen(path, "rb");
    if (f) {
        fseek(f, 0, SEEK_END);
        long n = ftell(f);
        fseek(f, 0, SEEK_SET);
        char *t = n > 0 ? malloc((size_t)n + 1) : NULL;
        if (t) {
            size_t got = fread(t, 1, (size_t)n, f);
            t[got] = 0;
            parse(t);
            free(t);
        }
        fclose(f);
    }
    display_config_path(path, sizeof path, "cheats_on");
    f = fopen(path, "r");
    if (f) {
        char line[200];
        while (fgets(line, sizeof line, f)) {
            line[strcspn(line, "\r\n")] = 0;
            for (int i = 0; i < ncheats; i++) {
                char full[200];
                snprintf(full, sizeof full, "%s/%s", cheats[i].group, cheats[i].name);
                if (!strcmp(full, line)) cheats[i].on = 1;
            }
        }
        fclose(f);
    }
    int on = 0;
    for (int i = 0; i < ncheats; i++) on += cheats[i].on;
    any_on = on;
    rt_log("cheats", "%d cheats, %d on", ncheats, on);
}

/* the settings menu (menu.cpp) */
int cheats_count(void) { load(); return ncheats; }
const char *cheats_name(int i) { return i >= 0 && i < ncheats ? cheats[i].name : ""; }
const char *cheats_group(int i) { return i >= 0 && i < ncheats ? cheats[i].group : ""; }
int cheats_on(int i) { return i >= 0 && i < ncheats && cheats[i].on; }

void cheats_set(int i, int on) {
    if (i < 0 || i >= ncheats) return;
    cheats[i].on = on != 0;
    int any = 0;
    for (int k = 0; k < ncheats; k++) any |= cheats[k].on;
    any_on = any;
    save_on();
    rt_log("cheats", "%s/%s %s", cheats[i].group, cheats[i].name, on ? "on" : "off");
}

static u16 peek16(u32 a) { u16 v; memcpy(&v, rt_ram + (a & 0x1FFFFFE), 2); return v; }

static void run(const Code *c, int n) {
    for (int i = 0; i < n; i++) {
        u32 t = c[i].a >> 28, a = c[i].a & 0x1FFFFFF, v = c[i].v;
        switch (t) {
        case 0: rt_ram[a] = (u8)v; break;
        case 1: { u16 w = (u16)v; memcpy(rt_ram + (a & ~1u), &w, 2); break; }
        case 2: memcpy(rt_ram + (a & ~3u), &v, 4); break;
        case 4:
            if (i + 1 < n) {
                u32 count = v >> 16, step = (v & 0xFFFF) * 4, val = c[i + 1].a, inc = c[i + 1].v;
                for (u32 k = 0; k < count && a + k * step + 4 <= 0x2000000; k++, val += inc)
                    memcpy(rt_ram + ((a + k * step) & ~3u), &val, 4);
                i++;
            }
            break;
        case 0xD: {
            u32 lines = (v >> 24) & 0xFF, test = (v >> 16) & 0xF;
            u16 have = peek16(a), want = (u16)v;
            int ok = test == 0 ? have == want : test == 1 ? have != want : test == 2 ? have < want : have > want;
            if (!ok) i += lines ? (int)lines : 1;
            break;
        }
        default: break;                                             /* 9: a device's hook; others unused */
        }
    }
}

/* kernel.c, each vertical blank (the game's thread, between its instructions) */
void cheats_apply(void) {
    if (!loaded || !any_on) return;
    for (int i = 0; i < ncheats; i++)
        if (cheats[i].on) run(cheats[i].codes, cheats[i].n);
}
