// SPDX-License-Identifier: Apache-2.0
// Pure helpers of the offline voice layer (stage 3e): 48 kHz -> 16 kHz decimation with a real low-pass, microphone slot
// selection, the navigation command table (Japanese + English), the voice status machine and the speaker mask.
// No ESP-IDF, no hardware, no radio: everything here is host-tested (tests/dev_voice_core_test.c).
//
// The speech engine itself (ESP-SR: AFE + WakeNet + MultiNet, all offline, all on-chip) lives in dev_voice.c and is only
// built with EXP_VOICE=1. It follows the owner's "Full Demo" project (korvo1_yokai_demo/main/voice_service.c): MultiNet7
// has an English model only, so Japanese commands are registered as phonetic English words ("HOH MOO" for ホーム) and
// carry the command id + 100. NOTHING here has run on the board: the phonetic spellings are a first guess to be tuned
// against the "heard" log of the real board.
#pragma once

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* ---- 48 kHz stereo (codec) -> 16 kHz mono (speech) -------------------------------------------------------------------
 * The 3-tap box filter used for the level meter (dev_pcm_decimate) has a null at 16 kHz and almost no rejection above
 * 8 kHz, so everything between 8 and 24 kHz would fold into the speech band. The speech path uses a 63-tap windowed-sinc
 * low-pass instead (Hamming window, -6 dB at 8 kHz, stop band from ~9.6 kHz, about -50 dB), then keeps every 3rd sample. */
#define DEV_VOICE_RATE 16000
#define DEV_VDEC_FACTOR 3
#define DEV_VDEC_TAPS 63

typedef struct {
    int32_t coef[DEV_VDEC_TAPS];   /* Q15, sum = 32768                            */
    int16_t hist[DEV_VDEC_TAPS];   /* newest sample at hist[pos]                  */
    int pos;
    int phase;                     /* input samples since the last output         */
} dev_vdec_t;

static inline void dev_vdec_init(dev_vdec_t *d)
{
    const double pi = 3.14159265358979323846;
    const double fc = 8000.0 / 48000.0;                 /* cutoff (-6 dB point) as a fraction of the input rate */
    const int m = DEV_VDEC_TAPS - 1;
    double c[DEV_VDEC_TAPS], sum = 0.0;
    for (int i = 0; i < DEV_VDEC_TAPS; ++i) {
        const double x = (double)i - (double)m / 2.0;
        const double sinc = (x == 0.0) ? 2.0 * fc : sin(2.0 * pi * fc * x) / (pi * x);
        const double w = 0.54 - 0.46 * cos(2.0 * pi * (double)i / (double)m);
        c[i] = sinc * w;
        sum += c[i];
    }
    memset(d, 0, sizeof(*d));
    int32_t total = 0;
    for (int i = 0; i < DEV_VDEC_TAPS; ++i) { d->coef[i] = (int32_t)lround(c[i] / sum * 32768.0); total += d->coef[i]; }
    d->coef[DEV_VDEC_TAPS / 2] += 32768 - total;        /* unity DC gain exactly */
}

/* st: interleaved L,R int16 frames; slot 0/1 picks the channel. Writes up to frames/3 + 1 samples, returns the count.
 * The filter state carries over between calls, so chunks of any size give one continuous stream. */
static inline int dev_vdec_push(dev_vdec_t *d, const int16_t *st, int frames, int slot, int16_t *out)
{
    int n = 0;
    for (int i = 0; i < frames; ++i) {
        d->pos = (d->pos + 1) % DEV_VDEC_TAPS;
        d->hist[d->pos] = st[2 * i + (slot ? 1 : 0)];
        if (++d->phase < DEV_VDEC_FACTOR) continue;
        d->phase = 0;
        int64_t acc = 0;
        int p = d->pos;
        for (int k = 0; k < DEV_VDEC_TAPS; ++k) {
            acc += (int64_t)d->coef[k] * d->hist[p];
            if (--p < 0) p = DEV_VDEC_TAPS - 1;
        }
        acc = (acc + 16384) >> 15;
        out[n++] = (int16_t)(acc > 32767 ? 32767 : (acc < -32768 ? -32768 : acc));
    }
    return n;
}

/* ---- Which I2S slot carries the ES8311 signal ----------------------------------------------------------------------------
 * Unknown until the first on-device log (see dev_audio.c). The level meter takes the louder slot per chunk; speech needs a
 * stable choice, or the filter would splice two different signals. The slot is changed only when the other one has been
 * clearly louder (4x energy and above an absolute floor) for DEV_VSLOT_HOLD chunks in a row. */
#define DEV_VSLOT_HOLD 8
#define DEV_VSLOT_FLOOR 100   /* mean square of the other slot below which it is treated as silence */

typedef struct {
    uint8_t slot;
    uint8_t votes;
} dev_vslot_t;

static inline int dev_vslot_choose(dev_vslot_t *s, const int16_t *st, int frames)
{
    int64_t e[2] = {0, 0};
    for (int i = 0; i < frames; ++i)
        for (int c = 0; c < 2; ++c) e[c] += (int64_t)st[2 * i + c] * st[2 * i + c];
    const int64_t mine = e[s->slot], other = e[1 - s->slot];
    const int64_t floor_e = (int64_t)DEV_VSLOT_FLOOR * (frames > 0 ? frames : 1);
    if (other > 4 * mine && other > floor_e) {
        if (++s->votes >= DEV_VSLOT_HOLD) { s->slot = (uint8_t)(1 - s->slot); s->votes = 0; }
    } else {
        s->votes = 0;
    }
    return s->slot;
}

/* ---- Sample ring between the microphone task (producer) and the speech feed task (consumer) -------------------------------
 * Single producer / single consumer, lock free (acquire/release on the indices). Capacity is a power of two. A full ring
 * drops the NEWEST samples and counts them: the speech engine then sees a gap, never stale audio. */
typedef struct {
    int16_t *buf;
    uint32_t mask;            /* capacity - 1 */
    uint32_t head;            /* total samples written (producer)  */
    uint32_t tail;            /* total samples read (consumer)     */
    uint32_t dropped;         /* samples refused because the ring was full */
} dev_vring_t;

static inline void dev_vring_init(dev_vring_t *r, int16_t *buf, uint32_t capacity_pow2)
{
    r->buf = buf;
    r->mask = capacity_pow2 - 1U;
    r->head = r->tail = r->dropped = 0;
}

static inline uint32_t dev_vring_avail(const dev_vring_t *r)
{
    return __atomic_load_n(&r->head, __ATOMIC_ACQUIRE) - __atomic_load_n(&r->tail, __ATOMIC_ACQUIRE);
}

/* in == NULL writes silence. Returns the number of samples stored. */
static inline uint32_t dev_vring_write(dev_vring_t *r, const int16_t *in, uint32_t n)
{
    const uint32_t cap = r->mask + 1U;
    const uint32_t used = dev_vring_avail(r);
    const uint32_t room = cap - used;
    const uint32_t take = n < room ? n : room;
    const uint32_t h = __atomic_load_n(&r->head, __ATOMIC_RELAXED);
    for (uint32_t i = 0; i < take; ++i) r->buf[(h + i) & r->mask] = in ? in[i] : 0;
    __atomic_store_n(&r->head, h + take, __ATOMIC_RELEASE);
    r->dropped += n - take;
    return take;
}

/* All-or-nothing: returns n when n samples were available, otherwise 0 and nothing is consumed. */
static inline uint32_t dev_vring_read(dev_vring_t *r, int16_t *out, uint32_t n)
{
    if (dev_vring_avail(r) < n) return 0;
    const uint32_t t = __atomic_load_n(&r->tail, __ATOMIC_RELAXED);
    for (uint32_t i = 0; i < n; ++i) out[i] = r->buf[(t + i) & r->mask];
    __atomic_store_n(&r->tail, t + n, __ATOMIC_RELEASE);
    return n;
}

/* ---- Speaker mask ----------------------------------------------------------------------------------------------------------
 * No echo cancellation is used (the experience only plays short UI cues), so the microphone is fed silence while a cue is
 * playing and for DEV_VHOLD_TAIL_MS afterwards: the device must not wake itself up with its own beep. */
#define DEV_VHOLD_TAIL_MS 250U

typedef struct {
    bool active;
    uint32_t until_ms;
} dev_vhold_t;

static inline void dev_vhold_set(dev_vhold_t *h, bool active, uint32_t now_ms)
{
    h->active = active;
    if (!active) h->until_ms = now_ms + DEV_VHOLD_TAIL_MS;
}

static inline bool dev_vhold_masked(const dev_vhold_t *h, uint32_t now_ms)
{
    return h->active || (int32_t)(h->until_ms - now_ms) > 0;
}

/* ---- Commands ----------------------------------------------------------------------------------------------------------------
 * The numbers are the values of exp_voice_cmd_t (exp_types.h; the host test checks that). */
typedef enum {
    DEV_VCMD_NONE = 0,
    DEV_VCMD_HOME,
    DEV_VCMD_MOTION,
    DEV_VCMD_AUDIO,
    DEV_VCMD_GAME,
    DEV_VCMD_DEVICE,
    DEV_VCMD_COUNT
} dev_vcmd_t;

#define DEV_VOICE_JA_BASE 100   /* Japanese phrases are registered with id = 100 + command (as in the Full Demo) */

typedef struct {
    uint8_t cmd;
    uint8_t ja;           /* 1 = Japanese phonetic spelling (id + 100), 0 = English */
    const char *phrase;   /* MultiNet7 English input: capital letters and single spaces only */
} dev_vphrase_t;

/* Phonetic spellings in capitals (what the English G2P turns into phonemes). Comments give the intended Japanese.
 * Commands are deliberately short and different from each other; every spelling is a candidate to be tuned on the board. */
static const dev_vphrase_t DEV_VOICE_PHRASES[] = {
    /* ホーム */
    {DEV_VCMD_HOME, 1, "HOH MOO"},               /* ホーム                 */
    {DEV_VCMD_HOME, 1, "HOH MOO GAH MEN"},       /* ホーム画面             */
    {DEV_VCMD_HOME, 1, "TOP PAY JEE"},           /* トップページ           */
    {DEV_VCMD_HOME, 1, "MOH DOH RU"},            /* もどる                 */
    {DEV_VCMD_HOME, 0, "HOME"},
    {DEV_VCMD_HOME, 0, "GO HOME"},
    {DEV_VCMD_HOME, 0, "GO BACK HOME"},
    {DEV_VCMD_HOME, 0, "GO TO HOME"},
    /* モーション */
    {DEV_VCMD_MOTION, 1, "MOH SHOHN"},           /* モーション             */
    {DEV_VCMD_MOTION, 1, "MOH SHUN"},
    {DEV_VCMD_MOTION, 1, "OO GO KEY"},           /* 動き                   */
    {DEV_VCMD_MOTION, 1, "SEN SAH"},             /* センサー               */
    {DEV_VCMD_MOTION, 0, "MOTION"},
    {DEV_VCMD_MOTION, 0, "OPEN MOTION"},
    {DEV_VCMD_MOTION, 0, "GO TO MOTION"},
    {DEV_VCMD_MOTION, 0, "SENSOR"},
    /* オーディオ・AI */
    {DEV_VCMD_AUDIO, 1, "AW DEE OH"},            /* オーディオ             */
    {DEV_VCMD_AUDIO, 1, "OH DEE OH"},
    {DEV_VCMD_AUDIO, 1, "ON SAY"},               /* 音声 (Full Demo と同じ綴り) */
    {DEV_VCMD_AUDIO, 1, "OHN SAY"},
    {DEV_VCMD_AUDIO, 1, "AY AI"},                /* エーアイ               */
    {DEV_VCMD_AUDIO, 0, "AUDIO"},
    {DEV_VCMD_AUDIO, 0, "OPEN AUDIO"},
    {DEV_VCMD_AUDIO, 0, "GO TO AUDIO"},
    {DEV_VCMD_AUDIO, 0, "VOICE"},
    /* ゲーム */
    {DEV_VCMD_GAME, 1, "GAY MOO"},               /* ゲーム                 */
    {DEV_VCMD_GAME, 1, "GEH MOO"},
    {DEV_VCMD_GAME, 1, "MAY ROW"},               /* 迷路                   */
    {DEV_VCMD_GAME, 1, "GAY MOO OH HAH JEE MEH"},/* ゲームをはじめる       */
    {DEV_VCMD_GAME, 0, "GAME"},
    {DEV_VCMD_GAME, 0, "OPEN GAME"},
    {DEV_VCMD_GAME, 0, "GO TO GAME"},
    {DEV_VCMD_GAME, 0, "PLAY GAME"},
    {DEV_VCMD_GAME, 0, "MAZE"},
    /* デバイス */
    {DEV_VCMD_DEVICE, 1, "DEH BYE SOO"},         /* デバイス               */
    {DEV_VCMD_DEVICE, 1, "DEE VYE SOO"},
    {DEV_VCMD_DEVICE, 1, "DEH BAH EE SOO"},      /* 2026-10-10 on the board: the first two spellings were not recognised;   */
    {DEV_VCMD_DEVICE, 1, "DEE BAH EE SOO"},      /* more variants of デバイス / ディバイス (to be tuned from [HEARD] logs)   */
    {DEV_VCMD_DEVICE, 1, "DAY BYE SOO"},
    {DEV_VCMD_DEVICE, 1, "DEV EYE SOO"},
    {DEV_VCMD_DEVICE, 1, "DEH BUY SOO"},
    {DEV_VCMD_DEVICE, 1, "KEY KEY"},             /* 機器                   */
    {DEV_VCMD_DEVICE, 1, "JOH HOH"},             /* 情報                   */
    {DEV_VCMD_DEVICE, 0, "DEVICE"},
    {DEV_VCMD_DEVICE, 0, "OPEN DEVICE"},
    {DEV_VCMD_DEVICE, 0, "GO TO DEVICE"},
    {DEV_VCMD_DEVICE, 0, "DEVICE INFO"},
    {DEV_VCMD_DEVICE, 0, "SHOW DEVICE"},
    {DEV_VCMD_DEVICE, 0, "DEE VICE"},
    {DEV_VCMD_DEVICE, 0, "DIH VICE"},
    {DEV_VCMD_DEVICE, 0, "DEV ICE"},
    {DEV_VCMD_DEVICE, 0, "SYSTEM"},
    {DEV_VCMD_DEVICE, 0, "OPEN SYSTEM"},
    {DEV_VCMD_DEVICE, 0, "GO TO SYSTEM"},
    {DEV_VCMD_DEVICE, 0, "SYSTEM INFO"},
    {DEV_VCMD_DEVICE, 0, "SETTINGS"},
    {DEV_VCMD_DEVICE, 0, "HARDWARE"},
    {DEV_VCMD_DEVICE, 0, "ABOUT"},
};
#define DEV_VOICE_PHRASE_N ((int)(sizeof(DEV_VOICE_PHRASES) / sizeof(DEV_VOICE_PHRASES[0])))

static inline int dev_voice_phrase_id(const dev_vphrase_t *p) { return (int)p->cmd + (p->ja ? DEV_VOICE_JA_BASE : 0); }

/* MultiNet id -> command. Returns DEV_VCMD_NONE for ids that are not ours. *ja = 1 when the id was a Japanese one. */
static inline dev_vcmd_t dev_voice_normalize_id(int id, int *ja)
{
    int is_ja = 0;
    if (id >= DEV_VOICE_JA_BASE) { is_ja = 1; id -= DEV_VOICE_JA_BASE; }
    if (ja) *ja = is_ja;
    return (id > DEV_VCMD_NONE && id < DEV_VCMD_COUNT) ? (dev_vcmd_t)id : DEV_VCMD_NONE;
}

static inline bool dev_voice_phrase_valid(const char *s)
{
    const size_t n = strlen(s);
    if (n == 0 || n > 48 || s[0] == ' ' || s[n - 1] == ' ') return false;
    for (size_t i = 0; i < n; ++i) {
        const char c = s[i];
        if (c == ' ') { if (s[i + 1] == ' ') return false; continue; }
        if (c < 'A' || c > 'Z') return false;
    }
    return true;
}

/* Detection thresholds of the Full Demo (MultiNet "det_threshold"): 0.23 while the model is created, 0.26 once the
 * commands are registered. Higher = fewer false triggers, more misses. Wake word and VAD parameters likewise. */
#define DEV_VOICE_MN_THRESH_CREATE 0.23f
#define DEV_VOICE_MN_THRESH_ACTIVE 0.26f
#define DEV_VOICE_MN_TIMEOUT_MS 5000
#define DEV_VOICE_VAD_MIN_SPEECH_MS 80
#define DEV_VOICE_VAD_MIN_NOISE_MS 300
#define DEV_VOICE_LISTEN_MAX_MS 7000U   /* safety net: the listening state ends even if the engine reports nothing */

static inline int dev_voice_prob_pct(float p)
{
    if (!(p > 0.0f)) return 0;
    if (p >= 1.0f) return 100;
    return (int)lroundf(p * 100.0f);
}

/* ---- Voice status machine --------------------------------------------------------------------------------------------------
 * Only real engine events change it. "ready" is set by the loader after the models and commands really loaded. */
typedef enum { DEV_VPH_OFF = 0, DEV_VPH_LOADING, DEV_VPH_READY, DEV_VPH_LISTENING, DEV_VPH_ERROR, DEV_VPH_SKIPPED } dev_vphase_t;
typedef enum { DEV_VEV_NONE = 0, DEV_VEV_WAKE, DEV_VEV_COMMAND, DEV_VEV_TIMEOUT } dev_vevent_t;

typedef struct {
    dev_vphase_t phase;
    dev_vevent_t event;      /* last event (DEV_VEV_NONE until one happened) */
    dev_vcmd_t cmd;          /* last recognised command                       */
    uint8_t ja;              /* 1 = the id of that command was a Japanese one */
    uint8_t wake_idx;        /* 1 or 2: which wake word fired (0 = none yet)  */
    uint8_t prob_pct;        /* confidence of the last command                */
    uint16_t seq;            /* +1 at every event                             */
    uint32_t listen_since_ms;
    uint32_t n_wake, n_cmd, n_timeout, n_reject;
} dev_vstate_t;

/* Phase -> the application's exp_ai_state_t numbers (0 not integrated, 1 loading, 2 ready, 3 error). "Ready" is reported
 * only for READY / LISTENING, i.e. after the models and the command list really loaded. */
static inline int dev_vs_ai_state(dev_vphase_t p)
{
    switch (p) {
    case DEV_VPH_LOADING: return 1;
    case DEV_VPH_READY:
    case DEV_VPH_LISTENING: return 2;
    case DEV_VPH_ERROR:
    case DEV_VPH_SKIPPED: return 3;
    default: return 0;
    }
}

static inline void dev_vs_set_phase(dev_vstate_t *v, dev_vphase_t p) { v->phase = p; }

static inline void dev_vs_wake(dev_vstate_t *v, uint32_t now_ms, int wake_idx)
{
    if (v->phase != DEV_VPH_READY && v->phase != DEV_VPH_LISTENING) return;   /* a wake word means nothing before "ready" */
    v->phase = DEV_VPH_LISTENING;
    v->event = DEV_VEV_WAKE;
    v->wake_idx = (uint8_t)(wake_idx == 2 ? 2 : 1);
    v->listen_since_ms = now_ms;
    ++v->n_wake;
    ++v->seq;
}

/* Returns true when the id was one of ours and an event was recorded. */
static inline bool dev_vs_command(dev_vstate_t *v, int id, float prob)
{
    int ja = 0;
    const dev_vcmd_t c = dev_voice_normalize_id(id, &ja);
    if (v->phase != DEV_VPH_READY && v->phase != DEV_VPH_LISTENING) return false;
    if (c == DEV_VCMD_NONE) { ++v->n_reject; return false; }
    v->phase = DEV_VPH_READY;
    v->event = DEV_VEV_COMMAND;
    v->cmd = c;
    v->ja = (uint8_t)ja;
    v->prob_pct = (uint8_t)dev_voice_prob_pct(prob);
    ++v->n_cmd;
    ++v->seq;
    return true;
}

static inline void dev_vs_timeout(dev_vstate_t *v)
{
    if (v->phase != DEV_VPH_LISTENING) return;
    v->phase = DEV_VPH_READY;
    v->event = DEV_VEV_TIMEOUT;
    ++v->n_timeout;
    ++v->seq;
}

/* Call regularly: ends a listening window the engine never closed. */
static inline void dev_vs_tick(dev_vstate_t *v, uint32_t now_ms)
{
    if (v->phase == DEV_VPH_LISTENING && (uint32_t)(now_ms - v->listen_since_ms) >= DEV_VOICE_LISTEN_MAX_MS) dev_vs_timeout(v);
}
