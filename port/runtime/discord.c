/*
 * Discord status ("Playing MS Saga - A New Dawn": at the title screen, exploring, in battle).
 *
 * Talks to the Discord app on this computer over its local IPC (a unix socket discord-ipc-N, on Windows the named
 * pipe \\.\pipe\discord-ipc-N): frames of { u32 opcode, u32 length, JSON }. Opcode 0 is the handshake with the
 * application's ID, 1 a command (SET_ACTIVITY), 2 close, 3 ping, 4 pong. No Discord library; nothing goes over the
 * network from here (the Discord app shows the status to whom its settings say). What the app answers is read and
 * dropped (it names the user: never logged). When the program ends, the connection closes and the status goes.
 *
 * Where the player is comes from the game's code modules as they are loaded (port/hle/dll.c): title, bmap / fmap /
 * tmap (the maps), sento (battle); the others leave the status as it was.
 *
 * On in the game's window; off with --discord off, MSSAGA_DISCORD=0 or a "0" in the settings folder's "discord"
 * file. Headless test runs (--shot) stay silent unless MSSAGA_DISCORD=1.
 */
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "r5900.h"
#include "runtime.h"
#include "platform.h"
#include "discord.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif
#endif

#define APP_ID "1575286990482575430"   /* the Discord application "MS Saga - A New Dawn" (public identifier) */

/* art asset names uploaded to that application */
static const struct { const char *details, *image; } states[] = {
    {"At the title screen", "title"},
    {"Exploring", "field"},
    {"In battle", "battle"},
};
enum { AT_TITLE, EXPLORING, IN_BATTLE };

static volatile int state = AT_TITLE;

/* whether the battle DLL is the one loaded (gpu_vk.c: the battle HUD stays in the middle 4:3 for now) */
int game_in_battle(void) { return state == IN_BATTLE; }

void discord_note_dll(const char *name) {
    if (!strcmp(name, "title")) state = AT_TITLE;
    else if (!strcmp(name, "bmap") || !strcmp(name, "fmap") || !strcmp(name, "tmap")) state = EXPLORING;
    else if (!strcmp(name, "sento")) state = IN_BATTLE;
}

/* ------------------------------------------------------------------------------------------- the pipe */

#ifdef _WIN32
static HANDLE pipe_h = INVALID_HANDLE_VALUE;

static int ipc_open(void) {
    for (int i = 0; i < 10; i++) {
        char path[64];
        snprintf(path, sizeof path, "\\\\.\\pipe\\discord-ipc-%d", i);
        pipe_h = CreateFileA(path, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
        if (pipe_h != INVALID_HANDLE_VALUE) return 1;
    }
    return 0;
}
static void ipc_close(void) {
    if (pipe_h != INVALID_HANDLE_VALUE) CloseHandle(pipe_h);
    pipe_h = INVALID_HANDLE_VALUE;
}
static int ipc_write(const void *p, size_t n) {
    DWORD done;
    return WriteFile(pipe_h, p, (DWORD)n, &done, NULL) && done == n;
}
/* bytes waiting; -1: the connection is gone. Waits up to ms for the first. */
static int ipc_waiting(int ms) {
    for (;;) {
        DWORD have = 0;
        if (!PeekNamedPipe(pipe_h, NULL, 0, NULL, &have, NULL)) return -1;
        if (have || ms <= 0) return (int)have;
        Sleep(20);
        ms -= 20;
    }
}
static int ipc_read(void *p, size_t n) {
    u8 *out = p;
    while (n) {
        if (ipc_waiting(2000) <= 0) return 0;
        DWORD done = 0;
        if (!ReadFile(pipe_h, out, (DWORD)n, &done, NULL) || !done) return 0;
        out += done;
        n -= done;
    }
    return 1;
}
static unsigned own_pid(void) { return (unsigned)GetCurrentProcessId(); }
#else
static int sock = -1;

static int ipc_open(void) {
    const char *base = NULL;
    static const char *vars[] = {"XDG_RUNTIME_DIR", "TMPDIR", "TMP", "TEMP"};
    for (int i = 0; i < 4 && !(base && *base); i++) base = getenv(vars[i]);
    if (!(base && *base)) base = "/tmp";
    /* the app's own place, then the Flatpak's and the Snap's */
    static const char *dirs[] = {"", "app/com.discordapp.Discord/", "snap.discord/"};
    for (int d = 0; d < 3; d++)
        for (int i = 0; i < 10; i++) {
            struct sockaddr_un a = {.sun_family = AF_UNIX};
            if (snprintf(a.sun_path, sizeof a.sun_path, "%s/%sdiscord-ipc-%d", base, dirs[d], i) >= (int)sizeof a.sun_path)
                continue;
            int s = socket(AF_UNIX, SOCK_STREAM, 0);
            if (s < 0) return 0;
            if (!connect(s, (struct sockaddr *)&a, sizeof a)) { sock = s; return 1; }
            close(s);
        }
    return 0;
}
static void ipc_close(void) {
    if (sock >= 0) close(sock);
    sock = -1;
}
static int ipc_write(const void *p, size_t n) { return send(sock, p, n, MSG_NOSIGNAL) == (ssize_t)n; }
static int ipc_waiting(int ms) {
    struct pollfd f = {sock, POLLIN, 0};
    int r = poll(&f, 1, ms);
    if (r <= 0) return r < 0 ? -1 : 0;
    return f.revents & POLLIN ? 1 : -1;
}
static int ipc_read(void *p, size_t n) {
    u8 *out = p;
    while (n) {
        if (ipc_waiting(2000) <= 0) return 0;
        ssize_t got = recv(sock, out, n, 0);
        if (got <= 0) return 0;
        out += got;
        n -= (size_t)got;
    }
    return 1;
}
static unsigned own_pid(void) { return (unsigned)getpid(); }
#endif

/* ------------------------------------------------------------------------------------------- frames */

static int send_frame(u32 op, const char *json) {
    u8 buf[8 + 1024];
    u32 n = (u32)strlen(json);
    if (n > sizeof buf - 8) return 0;
    memcpy(buf, &op, 4);
    memcpy(buf + 4, &n, 4);
    memcpy(buf + 8, json, n);
    return ipc_write(buf, 8 + n);
}

/* One frame from the app, its body dropped (a ping's is sent back); the opcode, or -1: the connection is gone. */
static int read_frame(void) {
    u32 head[2];
    if (!ipc_read(head, 8) || head[1] > (1u << 20)) return -1;
    char *body = malloc((size_t)head[1] + 1);
    if (!body || (head[1] && !ipc_read(body, head[1]))) { free(body); return -1; }
    body[head[1]] = 0;
    if (head[0] == 3) send_frame(4, body);
    free(body);
    return (int)head[0];
}

static int connect_app(void) {
    if (!ipc_open()) return 0;
    if (send_frame(0, "{\"v\":1,\"client_id\":\"" APP_ID "\"}") && read_frame() == 1) return 1;
    ipc_close();
    return 0;
}

static int set_activity(int s, time_t since, unsigned nonce) {
    char json[1024];
    snprintf(json, sizeof json,
             "{\"cmd\":\"SET_ACTIVITY\",\"args\":{\"pid\":%u,\"activity\":{\"details\":\"%s\","
             "\"timestamps\":{\"start\":%lld},\"assets\":{\"large_image\":\"%s\","
             "\"large_text\":\"MS Saga: A New Dawn\",\"small_image\":\"small\",\"small_text\":\"Native PC port\"}}},"
             "\"nonce\":\"%u\"}",
             own_pid(), states[s].details, (long long)since, states[s].image, nonce);
    return send_frame(1, json);
}

static void *discord_main(void *arg) {
    (void)arg;
    time_t since = time(NULL), sent_at = 0, tried_at = 0;
    int connected = 0, shown = -1;
    unsigned nonce = 0;
    for (;;) {
        time_t now = time(NULL);
        if (!connected && now - tried_at >= 15) {    /* the app may be started later */
            tried_at = now;
            connected = connect_app();
            if (connected) { rt_log("discord", "connected: the status follows the game"); shown = -1; sent_at = 0; }
        }
        if (connected) {
            int s = state, r;
            /* Discord takes a few updates in 20 s at most: no more than one in 5 */
            if (s != shown && now - sent_at >= 5) {
                if (set_activity(s, since, ++nonce)) { shown = s; sent_at = now; }
                else connected = 0;
            }
            while (connected && (r = ipc_waiting(0)) != 0)
                if (r < 0 || read_frame() < 0 ) connected = 0;
            if (!connected) { rt_log("discord", "connection lost"); ipc_close(); tried_at = now; }
        }
        usleep(500 * 1000);
    }
    return NULL;
}

void discord_start(int headless) {
    const char *e = getenv("MSSAGA_DISCORD");
    int on = 1;
    if (e && *e) on = atoi(e) != 0;
    else {
        if (headless) return;
        char path[1100], dir[1024], buf[8] = "";
        platform_data_dir(dir, sizeof dir);
        snprintf(path, sizeof path, "%s/discord", dir);
        FILE *f = fopen(path, "r");
        if (f) { if (fgets(buf, sizeof buf, f)) on = atoi(buf) != 0; fclose(f); }
    }
    if (!on) return;
    pthread_t t;
    if (pthread_create(&t, NULL, discord_main, NULL)) return;
    platform_thread_name(t, "discord");
    pthread_detach(t);
}
