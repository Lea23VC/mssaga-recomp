/* The player (FFmpeg) is from SaintSeiyaRecomp port/hle/movie.c (MIT, Copyright (c) 2026 Matheus Nunes Pereira;
 * licenses/SaintSeiyaRecomp-MIT.txt). How it is started and stopped is MS Saga's. */
/*
 * The game's movies: CRI Sofdec files in AFSLIST0.AFS, entry 25753 gto_op.sfd (the opening, ~100 MB) and 25754
 * conp.sfd. MPEG-2 512x448 at 29.97 frames a second, ADX stereo sound at 48 kHz.
 *
 * Sofdec decodes the pictures with the PS2's IPU, which the port has not got. The game's movie layer is replaced
 * where it starts, polls and stops a movie:
 *   MOVIE_PlayStart(entry, no_draw, loop, params) (0x1283F8) takes the first of the 4 slots (D_004EF680, 0x28 bytes
 *     each, state in byte 0) that is free (0) or ended (3), as the game's does, and starts FFmpeg on that AFS entry
 *     on a thread of its own. The slot says 1 (playing); byte 1 (the game's "has a picture") stays 0, so MOVIE_Main
 *     leaves it alone.
 *   MOVIE_GetStat(slot) (0x128E78), which callers poll until it says 3 or 4: the slot says 3 once the movie ended
 *     (or START was pressed, as Sofdec movies are skipped on the PS2).
 *   MOVIE_Stop(slot) (0x128DC8) / MOVIE_StopAll (0x128E10): stop it.
 * The pictures are shown in place of the game's (port/gs/display.c), the sound is mixed with the game's
 * (port/iop/cri_adxi.c). Built without FFmpeg, a movie ends at once (the game goes on as if it had played).
 */
#define _GNU_SOURCE
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "r5900.h"
#include "runtime.h"
#include "platform.h"
#include "pad.h"

#define MOVIE_SLOTS 0x004EF680u    /* D_004EF680: 4 slots of 0x28 bytes, byte 0 the state */
#define SLOT(n) (MOVIE_SLOTS + 0x28u * (u32)(n))

int cdvd_find(const char *path, u32 *lsn, u32 *size);
int cdvd_read(u32 lsn, u32 count, u8 *out);
void cri_audio_start(void);

/* An AFS entry's place on the disc: AFSLIST0.AFS's table is { "AFS\0", count, (offset, size) * count }. */
static int afs_entry(u32 index, u32 *lsn, u32 *size) {
    u32 base, afs_size;
    if (!cdvd_find("\\AFSLIST0.AFS;1", &base, &afs_size)) return 0;
    u8 sec[2048];
    if (cdvd_read(base, 1, sec)) return 0;
    u32 count;
    memcpy(&count, sec + 4, 4);
    if (index >= count) return 0;
    u32 at = 8 + index * 8, off, sz;
    if (cdvd_read(base + at / 2048, 1, sec)) return 0;
    memcpy(&off, sec + at % 2048, 4);
    if (at % 2048 + 8 <= 2048) memcpy(&sz, sec + at % 2048 + 4, 4);
    else {
        if (cdvd_read(base + at / 2048 + 1, 1, sec)) return 0;
        memcpy(&sz, sec, 4);
    }
    if (off % 2048) return 0;
    *lsn = base + off / 2048;
    *size = sz;
    return 1;
}

#ifdef MSSAGA_FFMPEG
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>

#define OUT_RATE 48000
#define RING (OUT_RATE * 2 * 4)  /* 4 s of the sound, stereo */
#define BLOCK 32                 /* sectors read at a time */

static pthread_t thread;
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static volatile int running, done, stop;
static u32 lsn, size;           /* the file on the disc */
static u64 start_ns;

/* the newest picture (RGBA), published by the thread */
static u32 *picture;
static int pic_w, pic_h;
static u32 pic_gen;

/* the sound, decoded ahead of the mixer */
static float ring[RING];
static u32 ring_head, ring_n;

static u64 now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (u64)ts.tv_sec * 1000000000ull + (u64)ts.tv_nsec;
}

/* the file, for libavformat: read through a block of sectors */
typedef struct { s64 pos; u32 block; u8 buf[BLOCK * 2048]; int have; } File;

static int io_read(void *opaque, uint8_t *out, int n) {
    File *f = opaque;
    int got = 0;
    while (got < n && f->pos < size) {
        u32 sector = (u32)(f->pos / 2048), first = sector / BLOCK * BLOCK;
        if (!f->have || f->block != first) {
            u32 count = (size + 2047) / 2048 - first;
            if (count > BLOCK) count = BLOCK;
            if (cdvd_read(lsn + first, count, f->buf)) return got ? got : AVERROR(EIO);
            f->block = first;
            f->have = 1;
        }
        s64 at = f->pos - (s64)first * 2048, left = (s64)BLOCK * 2048 - at, rest = size - f->pos;
        int k = (int)(left < rest ? left : rest);
        if (k > n - got) k = n - got;
        memcpy(out + got, f->buf + at, (size_t)k);
        got += k;
        f->pos += k;
    }
    return got ? got : AVERROR_EOF;
}

static int64_t io_seek(void *opaque, int64_t off, int whence) {
    File *f = opaque;
    if (whence & AVSEEK_SIZE) return size;
    whence &= ~AVSEEK_FORCE;
    s64 to = whence == SEEK_SET ? off : whence == SEEK_CUR ? f->pos + off : whence == SEEK_END ? size + off : -1;
    if (to < 0 || to > size) return -1;
    f->pos = to;
    return to;
}

/* Waits until the video's time t (seconds), or stop. */
static void wait_until(double t) {
    while (!stop) {
        s64 left = (s64)(t * 1e9) - (s64)(now_ns() - start_ns);
        if (left <= 0) return;
        struct timespec ts = {0, left > 5000000 ? 5000000 : left};
        nanosleep(&ts, NULL);
    }
}

static void put_sound(const float *s, int frames) {
    pthread_mutex_lock(&lock);
    for (int i = 0; i < frames * 2 && ring_n < RING; i++, ring_n++) ring[(ring_head + ring_n) % RING] = s[i];
    pthread_mutex_unlock(&lock);
}

static void *play(void *arg) {
    (void)arg;
    File *f = calloc(1, sizeof *f);
    AVFormatContext *fmt = avformat_alloc_context();
    u8 *iobuf = av_malloc(65536);
    fmt->pb = avio_alloc_context(iobuf, 65536, 0, f, io_read, NULL, io_seek);
    AVCodecContext *dec[2] = {NULL, NULL};          /* video, sound */
    int idx[2] = {-1, -1};
    struct SwsContext *sws = NULL;
    SwrContext *swr = NULL;
    AVPacket *pkt = av_packet_alloc();
    AVFrame *fr = av_frame_alloc();
    float *pcm = NULL;
    int pcm_cap = 0;
    if (avformat_open_input(&fmt, NULL, NULL, NULL) < 0 || avformat_find_stream_info(fmt, NULL) < 0) {
        rt_log("movie", "not a video FFmpeg can read");
        goto out;
    }
    for (int k = 0; k < 2; k++) {
        idx[k] = av_find_best_stream(fmt, k ? AVMEDIA_TYPE_AUDIO : AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
        if (idx[k] < 0) continue;
        AVStream *st = fmt->streams[idx[k]];
        const AVCodec *codec = avcodec_find_decoder(st->codecpar->codec_id);
        if (!codec || !(dec[k] = avcodec_alloc_context3(codec)) || avcodec_parameters_to_context(dec[k], st->codecpar) < 0
            || avcodec_open2(dec[k], codec, NULL) < 0) {
            rt_log("movie", "no decoder for stream %d", idx[k]);
            avcodec_free_context(&dec[k]);
            idx[k] = -1;
        }
    }
    if (idx[0] < 0) goto out;
    rt_log("movie", "%dx%d %s%s", dec[0]->width, dec[0]->height, avcodec_get_name(dec[0]->codec_id),
           idx[1] >= 0 ? ", with sound" : "");
    start_ns = now_ns();
    double last = 0;
    for (int eof = 0; !stop;) {
        int k = -1;
        if (!eof) {
            if (av_read_frame(fmt, pkt) < 0) {
                eof = 1;
                for (int j = 0; j < 2; j++) if (dec[j]) avcodec_send_packet(dec[j], NULL);
            } else {
                k = pkt->stream_index == idx[0] ? 0 : pkt->stream_index == idx[1] ? 1 : -1;
                if (k >= 0) avcodec_send_packet(dec[k], pkt);
                av_packet_unref(pkt);
                if (k < 0) continue;
            }
        }
        int any = 0;
        for (int j = 0; j < 2; j++) {
            if (!dec[j] || (k >= 0 && j != k)) continue;
            while (!stop && avcodec_receive_frame(dec[j], fr) == 0) {
                any = 1;
                if (j == 1) {
                    if (!swr) {
                        AVChannelLayout stereo = AV_CHANNEL_LAYOUT_STEREO;
                        swr_alloc_set_opts2(&swr, &stereo, AV_SAMPLE_FMT_FLT, OUT_RATE, &fr->ch_layout, fr->format,
                                            fr->sample_rate, 0, NULL);
                        if (swr_init(swr) < 0) swr_free(&swr);
                    }
                    int cap = swr ? swr_get_out_samples(swr, fr->nb_samples) : 0;
                    if (cap > pcm_cap) { pcm_cap = cap; pcm = realloc(pcm, (size_t)cap * 2 * sizeof *pcm); }
                    int n = swr ? swr_convert(swr, (uint8_t **)&pcm, cap, (const uint8_t **)fr->extended_data, fr->nb_samples) : 0;
                    if (n > 0) put_sound(pcm, n);
                } else {
                    AVRational tb = fmt->streams[idx[0]]->time_base;
                    double t = fr->best_effort_timestamp == AV_NOPTS_VALUE ? last : fr->best_effort_timestamp * av_q2d(tb);
                    last = t;
                    sws = sws_getCachedContext(sws, fr->width, fr->height, fr->format, fr->width, fr->height,
                                               AV_PIX_FMT_RGBA, SWS_BILINEAR, NULL, NULL, NULL);
                    static u32 *back;
                    static int back_size;
                    if (back_size < fr->width * fr->height) { back_size = fr->width * fr->height; back = realloc(back, (size_t)back_size * 4); }
                    uint8_t *dst[1] = {(uint8_t *)back};
                    int stride[1] = {fr->width * 4};
                    sws_scale(sws, (const uint8_t *const *)fr->data, fr->linesize, 0, fr->height, dst, stride);
                    wait_until(t);
                    pthread_mutex_lock(&lock);
                    if (pic_w * pic_h < fr->width * fr->height) picture = realloc(picture, (size_t)fr->width * fr->height * 4);
                    memcpy(picture, back, (size_t)fr->width * fr->height * 4);
                    pic_w = fr->width;
                    pic_h = fr->height;
                    pic_gen++;
                    pthread_mutex_unlock(&lock);
                }
                av_frame_unref(fr);
            }
        }
        if (eof && !any) break;
    }
    if (!stop) {                                    /* the last picture's time, then what is left of the sound */
        wait_until(last + 1.0 / 30);
        for (int k = 0; k < 100 && !stop && ring_n; k++) wait_until((double)(now_ns() - start_ns) / 1e9 + 0.01);
        rt_log("movie", "ended at %.1f s", last);
    }
out:
    av_frame_free(&fr);
    av_packet_free(&pkt);
    for (int k = 0; k < 2; k++) avcodec_free_context(&dec[k]);
    sws_freeContext(sws);
    swr_free(&swr);
    free(pcm);
    if (fmt->pb) { av_freep(&fmt->pb->buffer); avio_context_free(&fmt->pb); }
    avformat_close_input(&fmt);
    free(f);
    done = 1;
    return NULL;
}

static int movie_start(u32 entry) {
    if (!afs_entry(entry, &lsn, &size)) { rt_log("movie", "AFS entry %u not found", entry); return 0; }
    rt_log("movie", "playing AFS entry %u (%u bytes)", entry, size);
    cri_audio_start();
    stop = done = 0;
    pic_gen = 0;
    ring_n = 0;
    running = 1;
    if (pthread_create(&thread, NULL, play, NULL)) { running = 0; return 0; }
    platform_thread_name(thread, "movie");
    return 1;
}

static void movie_stop(void) {
    if (!running) return;
    stop = 1;
    pthread_join(thread, NULL);
    pthread_mutex_lock(&lock);
    running = 0;
    ring_n = 0;
    pthread_mutex_unlock(&lock);
}

static int movie_playing(void) { return running && !done; }
int movie_active(void) { return movie_playing(); }

/* For the display: the newest picture into out (if there is a video and *gen is not its picture's
 * number); 0 when no video is playing. */
int movie_take(u32 *out, int max, int *w, int *h, u32 *gen) {
    if (!running) return 0;
    pthread_mutex_lock(&lock);
    int on = running && pic_gen;
    if (on && pic_gen != *gen && pic_w * pic_h <= max) {
        memcpy(out, picture, (size_t)pic_w * pic_h * 4);
        *w = pic_w;
        *h = pic_h;
        *gen = pic_gen;
    }
    pthread_mutex_unlock(&lock);
    return on;
}

/* For the mixer: adds the video's sound to out (stereo, OUT_RATE). */
void movie_audio(float *out, int frames) {
    if (!running) return;
    pthread_mutex_lock(&lock);
    for (int i = 0; i < frames * 2 && ring_n; i++, ring_n--) {
        out[i] += ring[ring_head];
        ring_head = (ring_head + 1) % RING;
    }
    pthread_mutex_unlock(&lock);
}
#else
static int movie_start(u32 entry) { rt_log("movie", "AFS entry %u skipped: built without FFmpeg", entry); return 0; }
static void movie_stop(void) {}
static int movie_playing(void) { return 0; }
int movie_active(void) { return movie_playing(); }
int movie_take(u32 *out, int max, int *w, int *h, u32 *gen) { (void)out; (void)max; (void)w; (void)h; (void)gen; return 0; }
void movie_audio(float *out, int frames) { (void)out; (void)frames; }
#endif

static int our_slot = -1;           /* the slot our player plays in */
static u16 start_was;               /* START held when the movie started (no skip until let go) */

static void end_slot(Ctx *c) {
    movie_stop();
    if (our_slot >= 0) wr8(c, SLOT(our_slot), 3);  /* ended */
    our_slot = -1;
}

void hle_MOVIE_PlayStart(Ctx *c) {
    u32 entry = RW(4);
    if (our_slot >= 0) end_slot(c);                /* one movie at a time */
    int slot = 0;
    while (slot < 4) {
        u8 st = rd8(c, SLOT(slot));
        if (st == 0 || st == 3) break;
        slot++;
    }
    if (slot == 4) slot = 0;
    rt_log("movie", "MOVIE_PlayStart(%u, %u, %u, %08X), slot %d", entry, RW(5), RW(6), RW(7), slot);
    wr8(c, SLOT(slot) + 1, 0);                      /* no picture of the game's: MOVIE_Main leaves it alone */
    if (movie_start(entry)) {
        wr8(c, SLOT(slot), 1);                      /* playing */
        our_slot = slot;
        start_was = pad_buttons[0] & PAD_START;
    } else
        wr8(c, SLOT(slot), 3);                      /* ended at once */
    SET64(2, slot);
}

void hle_MOVIE_GetStat(Ctx *c) {
    int slot = (int)(RW(4) & 0xFF);
    if (slot == our_slot) {
        u16 start = pad_buttons[0] & PAD_START;
        if (start && !start_was) { rt_log("movie", "skipped (START)"); end_slot(c); }
        else if (!movie_playing()) end_slot(c);
        start_was = start;
    }
    SET64(2, rd8(c, SLOT(slot & 3)));
}

void hle_MOVIE_Stop(Ctx *c) {
    if ((int)(RW(4) & 0xFF) == our_slot) end_slot(c);
}

void hle_MOVIE_StopAll(Ctx *c) {
    if (our_slot >= 0) end_slot(c);
}
