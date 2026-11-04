/* SPDX-License-Identifier: MIT
 * Copyright (C) 2026 john-s */
/* Trifid: an industrial feedback-noise instrument for the FM-1, on fm1-base. A Karplus-Strong string inside a
 * feedback loop, excited by noise (the idea is Synthux Academy's Audrey II; the code is Trifid's own: docs/PRD.md).
 *
 * This file: the engine's buffers (the pool), the render, the state (what is saved: the autosave and the one
 * snapshot), boot. The UI is in ui/ (keys and pressure, pages and encoders, the screen); the DSP in dsp/ (o2 units,
 * no platform calls). Part of fm1-base's unity build (device/unity.c, host/plat_host.c), after the platform. */
#include "fastmath.h"
#include "engine.h"

/* ------------------------------------------------------------------ pool --- */
/* the engine's lines, in the pool (docs/ram.md has the budget): 18,432 + 36,864 + 16,384 + 176,472 bytes */
static float str_buf[2][PLANT_STRING_LEN] PLAT_POOL;
static float body_buf[2][PLANT_BODY_LEN] PLAT_POOL;
static int16_t rot_buf[ROT_LEN] PLAT_POOL;
static int16_t echo_buf[2][ECHO_LEN] PLAT_POOL;

static tf_engine_t eng;
static tf_params_t prm;                  /* the main loop writes, the render reads once a block */
static volatile uint8_t eng_ready;

/* ----------------------------------------------------------------- state --- */
#define TF_ST_MAGIC 0x31465254u             /* "TRF1" */
#define TF_ST_VERSION 1u
enum { PG_HOME, PG_FX, PG_SEQ, PG_SEL, PG_GLO, NPAGES };
enum { FEED_PRESSURE, FEED_VELOCITY };
/* everything a player sets; the floats glide on a recall (EDIT+SAVE), the small ones jump */
#define TF_ST_NF 20
typedef struct {
    uint32_t magic;
    uint16_t version, size;
    union {
        struct {
            float fb_db, drone, fine, glide, body, drive, lpf, hpf, rot_mix, rot_decay, rot_smear, rot_jitter,
                echo_send, echo_ret, echo_time, echo_fb, echo_wear, grit, seed_db, press_depth;
        } v;
        float f[TF_ST_NF];
    } u;
    int8_t scale, root, octave, out_mode, vel_curve, press_sense, feed_mode, page;
} tf_state_t;
static tf_state_t st;
_Static_assert(sizeof(((tf_state_t *)0)->u.v) == TF_ST_NF * sizeof(float), "the floats a recall glides");

static void state_defaults(tf_state_t *s)
{
    tf_params_t d;
    tf_params_default(&d);
    s->magic = TF_ST_MAGIC;
    s->version = TF_ST_VERSION;
    s->size = (uint16_t)sizeof *s;
    s->u.v.fb_db = -30.0f;
    s->u.v.drone = 40.0f;               /* E2 */
    s->u.v.fine = 0.0f;
    s->u.v.glide = d.glide;
    s->u.v.body = d.body;
    s->u.v.drive = d.drive;
    s->u.v.lpf = d.lpf;
    s->u.v.hpf = d.hpf;
    s->u.v.rot_mix = d.rot_mix;
    s->u.v.rot_decay = d.rot_decay;
    s->u.v.rot_smear = d.rot_smear;
    s->u.v.rot_jitter = d.rot_jitter;
    s->u.v.echo_send = d.echo_send;
    s->u.v.echo_ret = d.echo_ret;
    s->u.v.echo_time = d.echo_time;
    s->u.v.echo_fb = d.echo_fb;
    s->u.v.echo_wear = d.echo_wear;
    s->u.v.grit = 0.0f;
    s->u.v.seed_db = -90.0f;
    s->u.v.press_depth = 0.7f;
    s->scale = 1;                       /* natural minor */
    s->root = 4;                        /* E */
    s->octave = 0;
    s->out_mode = OUT_SPEAKER;
    s->vel_curve = 0;
    s->press_sense = 0;
    s->feed_mode = FEED_PRESSURE;
    s->page = PG_HOME;
}

/* a loaded state checked field by field: a bad value is the default's */
static void state_fix(tf_state_t *s)
{
    tf_state_t d;
    int i;
    static const float lo[TF_ST_NF] = {-60, 16, -100, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -120, 0};
    static const float hi[TF_ST_NF] = {12, 72, 100, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, -60, 1};
    state_defaults(&d);
    for (i = 0; i < TF_ST_NF; i++)
        if (!(s->u.f[i] >= lo[i] && s->u.f[i] <= hi[i]))     /* (a NaN fails both) */
            s->u.f[i] = d.u.f[i];
    if (s->scale < 0 || s->scale > 11) s->scale = d.scale;
    if (s->root < 0 || s->root > 11) s->root = d.root;
    if (s->octave < -2 || s->octave > 2) s->octave = d.octave;
    if (s->out_mode != OUT_SPEAKER && s->out_mode != OUT_LINE) s->out_mode = d.out_mode;
    if (s->vel_curve < -5 || s->vel_curve > 5) s->vel_curve = d.vel_curve;
    if (s->press_sense < -5 || s->press_sense > 5) s->press_sense = d.press_sense;
    if (s->feed_mode != FEED_PRESSURE && s->feed_mode != FEED_VELOCITY) s->feed_mode = d.feed_mode;
    if (s->page < 0 || s->page >= NPAGES) s->page = d.page;
}

static int state_load(uint32_t obj, tf_state_t *s)
{
    tf_state_t t;
    if (plat_store_load(obj, &t, sizeof t) != (int)sizeof t || t.magic != TF_ST_MAGIC || t.version != TF_ST_VERSION ||
        t.size != sizeof t)
        return 0;
    state_fix(&t);
    *s = t;
    return 1;
}

/* --------------------------------------------------------- live (not saved) --- */
static struct {
    float note;                         /* the string's target: the key's note, or the drone */
    float feed;                         /* the key's feed level, 0..1 */
    uint8_t starve, starve_latch, freeze, latch;
    float time_mul;
    uint32_t imp_seq;
    float imp_amp;
    /* the recall's glide (EDIT+SAVE): from, to, when it started */
    float from[TF_ST_NF], to[TF_ST_NF];
    uint32_t glide_t0;
    uint8_t gliding;
    /* the autosave */
    uint32_t changed_at, saved_at;
    uint8_t dirty;
} live;

static void mark(void)                  /* the state changed: autosave it once it settles */
{
    live.dirty = 1;
    live.changed_at = plat_ms();
}

/* the state and the live controls into the engine's parameters (main loop) */
static void params_push(void)
{
    union { tf_params_t p; uint32_t w[sizeof(tf_params_t) / 4u]; } u;   /* (a union: no aliasing trap) */
    tf_params_t p = prm;
    p.fb_db = st.u.v.fb_db;
    p.note = live.note;
    p.fine = st.u.v.fine;
    p.glide = st.u.v.glide;
    p.body = st.u.v.body;
    p.drive = st.u.v.drive;
    p.lpf = st.u.v.lpf;
    p.hpf = st.u.v.hpf;
    p.rot_mix = st.u.v.rot_mix;
    p.rot_decay = st.u.v.rot_decay;
    p.rot_smear = st.u.v.rot_smear;
    p.rot_jitter = st.u.v.rot_jitter;
    p.echo_send = st.u.v.echo_send;
    p.echo_ret = st.u.v.echo_ret;
    p.echo_time = st.u.v.echo_time;
    p.echo_fb = st.u.v.echo_fb;
    p.echo_wear = st.u.v.echo_wear;
    p.time_mul = live.time_mul;
    p.grit = st.u.v.grit;
    p.seed_db = st.u.v.seed_db;
    p.feed = live.feed;
    p.out_mode = st.out_mode;
    p.starve = live.starve;
    p.freeze = live.freeze;
    p.imp_amp = live.imp_amp;
    p.imp_seq = live.imp_seq;
    {   /* word by word, in order (each field one 32-bit word): the render may read it any time, and never sees a
         * field half written */
        volatile uint32_t *dst = (volatile uint32_t *)&prm;
        uint32_t i;
        u.p = p;
        for (i = 0; i < sizeof prm / 4u; i++)
            dst[i] = u.w[i];
    }
}
_Static_assert(sizeof(tf_params_t) % 4u == 0, "the parameters are whole words");

/* the ignition threshold for the state now (dB of feedback gain) */
static float threshold_db(void)
{
    tf_params_t p = prm;
    p.note = live.note;
    p.lpf = st.u.v.lpf;
    p.hpf = st.u.v.hpf;
    p.fine = st.u.v.fine;
    return tf_engine_threshold_db(&eng, &p);
}

/* ---------------------------------------------------------------- render --- */
/* the longest render, in plat_ticks24 ticks (Setup shows it against the budget: an audio half, APP_BLOCK frames) */
static volatile uint32_t render_max;

void app_render(int32_t *lr, uint32_t n, uint32_t master_q12)
{
    static float l[APP_BLOCK], r[APP_BLOCK];
    float g = (float)master_q12 * (8388607.0f / 4096.0f);     /* MASTER, after the limiter: at most 1 */
    uint32_t i, done = 0;
    if (!eng_ready) {
        for (i = 0; i < 2u * n; i++)
            lr[i] = 0;
        return;
    }
    uint32_t t0 = plat_ticks24(), dt;
    while (done < n) {                  /* (n is APP_BLOCK on the FM-1; the host may ask for any) */
        uint32_t k = n - done < APP_BLOCK ? n - done : APP_BLOCK;
        tf_engine_render(&eng, &prm, l, r, k, 0);
        for (i = 0; i < k; i++) {
            lr[2u * (done + i)] = (int32_t)(l[i] * g);
            lr[2u * (done + i) + 1u] = (int32_t)(r[i] * g);
        }
        done += k;
    }
    dt = plat_ticks24() - t0;           /* the render's time, ticks (interrupts that cut in included) */
    if (dt > render_max)
        render_max = dt;
}

void app_panic(void)
{
    live.starve = 1;
    prm.starve = 1;
    eng_ready = 0;                      /* silence at once; app_init brings it back */
}

/* ------------------------------------------------------------------ boot --- */
static void engine_start(void)
{
    tf_buffers_t b;
    b.str[0] = str_buf[0];
    b.str[1] = str_buf[1];
    b.body[0] = body_buf[0];
    b.body[1] = body_buf[1];
    b.rot = rot_buf;
    b.echo[0] = echo_buf[0];
    b.echo[1] = echo_buf[1];
    eng_ready = 0;
    tf_engine_init(&eng, &b);
    tf_params_default(&prm);
}

static void ui_boot(void);               /* ui/keys.c, ui/pages.c */
static int log_boot(tf_state_t *s);      /* the autosave log, below */

void app_init(void)
{
    float th;
    engine_start();
    state_defaults(&st);
    if (!log_boot(&st))                 /* the last state: the autosave log's newest, else (before the log) the store's */
        state_load(OBJ_AUTO, &st);
    live.note = st.u.v.drone;
    live.time_mul = 1.0f;
    live.starve = live.starve_latch = live.freeze = live.latch = 0;
    live.feed = 0.0f;
    params_push();
    /* boot is quiet and safe: the lines are empty and the feedback starts at least 6 dB below ignition (the last
     * setting, if it was lower) */
    th = threshold_db();
    if (st.u.v.fb_db > th - 6.0f)
        st.u.v.fb_db = fm_floorf((th - 6.0f) * 10.0f) * 0.1f;
    params_push();
    live.dirty = 0;
    ui_boot();
    eng_ready = 1;
}

/* ------------------------------------------------------- save and recall --- */
/* a save erases a flash sector, and fm1-base silences the audio meanwhile (tens of ms): the autosave waits until the
 * controls have been still 4 s, and while the loop is loud it saves at most once a minute */
/* a save erases a flash sector with the interrupts off, longer than an audio half: the render after it is late, an
 * xrun. They are counted apart (Setup shows them), so the ones a save did not cause stand out */
static struct {
    uint32_t before, until;             /* the xruns before a save; when its window closes */
    uint32_t xruns;                     /* the xruns saves caused */
    uint8_t open;
} saves;
static int store_save(uint32_t obj)
{
    int rc;
    if (!saves.open)
        saves.before = plat_xruns();
    rc = plat_store_save(obj, &st, sizeof st);
    saves.open = 1;
    saves.until = plat_ms() + 100u;     /* (the late render comes as soon as the interrupts are back) */
    return rc;
}
static void saves_poll(void)
{
    if (saves.open && (int32_t)(plat_ms() - saves.until) >= 0) {
        saves.open = 0;
        saves.xruns += plat_xruns() - saves.before;
    }
}

/* ------------------------------------------------------------ the autosave log --- */
/* The autosave never erases while it plays. fm1-base's store erases a 4 KiB flash sector for every save, with the
 * interrupts off for tens of ms: an xrun each time (the device showed it). A flash write holds them off for one
 * 256-byte page only (well under a ms), inside the render's slack (its worst is 0.85 of 2.9 ms). So the last state
 * goes into a log: two sectors of the data area, 32 slots of 128 bytes each; a save writes the next erased slot
 * (one page), and the newest valid record wins at boot. The boot (silent anyway) erases the sector that does not
 * hold the newest record: a session has 32 saves before it needs an erase, and the erase then waits for quiet. */
#define LOG_SEC0 0x97000u                /* the data area's first two sectors (fm1-base plat.h: 0x97000..0xDFFFF) */
#define LOG_SECTORS 2u
#define LOG_SLOT 128u
#define LOG_SLOTS (0x1000u / LOG_SLOT)
#define LOG_MAGIC 0x4C465254u            /* "TRFL" */
typedef struct {
    uint32_t magic, seq, sum;
    tf_state_t st;
    uint8_t pad[LOG_SLOT - 12u - sizeof(tf_state_t)];
} log_rec_t;
_Static_assert(sizeof(log_rec_t) == LOG_SLOT, "a log record is one slot (inside one flash page)");
static log_rec_t log_buf;                /* the record being written (the flash driver takes RAM) */
static struct {
    uint32_t sec, slot, seq;             /* where the next record goes, and its number */
    uint8_t ready;
} lg;

static uint32_t log_sum(const tf_state_t *s)    /* FNV-1a over the state */
{
    const uint8_t *b = (const uint8_t *)s;
    uint32_t h = 2166136261u, i;
    for (i = 0; i < sizeof *s; i++)
        h = (h ^ b[i]) * 16777619u;
    return h;
}
static uint32_t log_at(uint32_t sec, uint32_t slot) { return LOG_SEC0 + sec * 0x1000u + slot * LOG_SLOT; }
static int log_blank(const uint8_t *p, uint32_t n)
{
    while (n--)
        if (*p++ != 0xFFu)
            return 0;
    return 1;
}

/* boot: the newest valid record into s (1), or none (0); the log made ready for this session's saves */
static int log_boot(tf_state_t *s)
{
    uint32_t sec, slot, best_seq = 0, best_sec = 0, found = 0;
    tf_state_t t;
    for (sec = 0; sec < LOG_SECTORS; sec++)
        for (slot = 0; slot < LOG_SLOTS; slot++) {
            const log_rec_t *r = (const log_rec_t *)plat_data(log_at(sec, slot));
            if (!r || r->magic != LOG_MAGIC || r->sum != log_sum(&r->st) || (found && r->seq <= best_seq))
                continue;
            t = r->st;
            if (t.magic != TF_ST_MAGIC || t.version != TF_ST_VERSION || t.size != sizeof t)
                continue;
            found = 1;
            best_seq = r->seq;
            best_sec = sec;
            *s = t;
        }
    if (found)
        state_fix(s);
    /* this session writes the other sector, from its first slot; the newest record stays where it is until then */
    lg.sec = found ? (best_sec + 1u) % LOG_SECTORS : 0u;
    lg.slot = 0;
    lg.seq = best_seq + 1u;
    {
        const uint8_t *p = plat_data(log_at(lg.sec, 0));
        lg.ready = p != 0;
        if (p && !log_blank(p, 0x1000u))
            lg.ready = plat_data_erase(log_at(lg.sec, 0)) == 0;
    }
    return (int)found;
}

/* a record of the state now; may_erase: the next sector may be erased first if this one is full (quiet only) */
static int log_save(int may_erase)
{
    const log_rec_t *back;
    int rc;
    if (!lg.ready)
        return -1;
    if (lg.slot >= LOG_SLOTS) {          /* full: the other sector (its records all older) erased, when it may */
        uint32_t next = (lg.sec + 1u) % LOG_SECTORS;
        if (!may_erase)
            return 1;                    /* (later, when it is quiet) */
        if (plat_data_erase(log_at(next, 0)) != 0)
            return -1;
        lg.sec = next;
        lg.slot = 0;
    }
    log_buf.magic = LOG_MAGIC;
    log_buf.seq = lg.seq;
    log_buf.st = st;
    log_buf.sum = log_sum(&log_buf.st);
    rc = plat_data_write(log_at(lg.sec, lg.slot), &log_buf, sizeof log_buf);
    back = (const log_rec_t *)plat_data(log_at(lg.sec, lg.slot));
    lg.slot++;                           /* (a failed slot is passed over: it is not erased any more) */
    if (rc != 0 || !back || back->seq != lg.seq || back->sum != log_buf.sum)
        return -1;
    lg.seq++;
    return 0;
}

static void autosave(void)
{
    uint32_t now = plat_ms();
    int quiet = eng.peak < 0.001f, rc;   /* below -60 dBFS: an erase's silence (and its xrun) goes unheard */
    if (!live.dirty || now - live.changed_at < 4000u || live.gliding)
        return;
    if (!saves.open)
        saves.before = plat_xruns();     /* (any xrun a log write causes is counted with the saves') */
    rc = log_save(quiet);
    saves.open = 1;
    saves.until = plat_ms() + 100u;
    if (rc == 0) {
        live.dirty = 0;
        live.saved_at = now ? now : 1u;
    } else if (rc < 0) {                 /* the log failed: fm1-base's store, as before (an erase) */
        if (store_save(OBJ_AUTO) == 0)
            live.dirty = 0;
    }
}

static void snapshot_save(void)
{
    if (store_save(OBJ_SNAP) == 0)
        app_say("SAVED", "SNAPSHOT");
    else
        app_say("SAVE", "FAILED");
}

/* EDIT+SAVE: the snapshot back, every value gliding to it over a second (the small ones at once) */
static void snapshot_recall(void)
{
    tf_state_t s;
    int i;
    if (!state_load(OBJ_SNAP, &s)) {
        app_say("NO SNAPSHOT", "SAVE FIRST");
        return;
    }
    for (i = 0; i < TF_ST_NF; i++) {
        live.from[i] = st.u.f[i];
        live.to[i] = s.u.f[i];
    }
    st.scale = s.scale;
    st.root = s.root;
    st.octave = s.octave;
    st.out_mode = s.out_mode;
    st.vel_curve = s.vel_curve;
    st.press_sense = s.press_sense;
    st.feed_mode = s.feed_mode;
    live.glide_t0 = plat_ms();
    live.gliding = 1;
    mark();
    app_say("RECALL", "SNAPSHOT");
}

static void recall_glide(void)
{
    uint32_t t = plat_ms() - live.glide_t0;
    float a;
    int i;
    if (!live.gliding)
        return;
    a = t >= 1000u ? 1.0f : (float)t / 1000.0f;
    a = a * a * (3.0f - 2.0f * a);      /* smooth at both ends */
    for (i = 0; i < TF_ST_NF; i++)
        st.u.f[i] = live.from[i] + (live.to[i] - live.from[i]) * a;
    if (t >= 1000u)
        live.gliding = 0;
}

/* text, without a C library */
static void text_cat(char *d, const char *s)
{
    while (*d)
        d++;
    while (*s)
        *d++ = *s++;
    *d = 0;
}
static void int_text(int32_t v, char *b)
{
    char t[12];
    int n = 0;
    uint32_t u = (uint32_t)(v < 0 ? -v : v);
    do
        t[n++] = (char)('0' + u % 10u);
    while ((u /= 10u) && n < 11);
    if (v < 0)
        *b++ = '-';
    while (n)
        *b++ = t[--n];
    *b = 0;
}
/* v with one decimal (or none), signed if asked */
static void dec_text(float v, int decimals, int sign, char *b)
{
    int32_t s = 1, i;
    float a = v;
    char t[12];
    if (decimals > 0)
        s = decimals == 1 ? 10 : 100;
    i = (int32_t)(fm_fabsf(a) * (float)s + 0.5f);
    b[0] = 0;
    if (v < 0.0f && i)
        text_cat(b, "-");
    else if (sign)
        text_cat(b, "+");
    int_text(i / s, t);
    text_cat(b, t);
    if (decimals > 0) {
        char f[4];
        int32_t r = i % s;
        text_cat(b, ".");
        if (decimals == 2 && r < 10)
            text_cat(b, "0");
        int_text(r, f);
        text_cat(b, f);
    }
}

#include "ui/keys.c"
#include "ui/pages.c"
#include "ui/screen.c"
#ifdef PLAT_HOST
#include "host.c"
#endif
