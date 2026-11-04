/* SPDX-License-Identifier: MIT
 * Copyright (C) 2026 john-s */
/* The plant: Trifid's feedback loop, in stereo (two strings, one pitch; the right body delay 4 samples shorter).
 *
 *   feed mixer -> string -> drive -> LPF -> HPF -> [reverb, crossfaded: verb.h] -> feedback gain -> body delay -+
 *        ^                                                                                                  |
 *        +--------------------------------------------------------------------------------------------------+
 *
 * The feed mixer sums the noise seed (always on), the pressure feed, the velocity impulse, the echo's return and an
 * external hook (v1.x: metal impulses, FM bursts) into the string's input. The string is a Karplus-Strong line with
 * its own feedback (a Hermite read, a DC block, a one-pole damping), so it rings at its pitch; the outer loop through
 * the feedback gain sets whether the whole thing self-excites.
 *
 * Every stage in the dry loop has a slope of at most 1 (the drive is normalised: drive changes the clip point, not
 * the small-signal gain), so the loop's small-signal gain bounds it: while plant_margin_db() < 0 the dry loop cannot
 * ignite (the small-gain theorem), however long it runs. The meter shows that number. The reverb (verb.h), crossfaded
 * in, moves the real point (its wet is scaled to unity energy, not to a peak of 1: docs/DECISIONS.md).
 *
 * Per sample: plant_tick_a (feed to filters), then the reverb (the engine), then plant_tick_b (gain, body). Per block:
 * plant_set. No platform calls: compiles natively for the tests. */
#pragma once
#include "dsp_util.h"

#define PLANT_STRING_LEN 2304u          /* MIDI 16 (20.6 Hz) is 2141 samples */
#define PLANT_BODY_LEN 4608u            /* 104 ms */
#define PLANT_NOTE_LO 16.0f
#define PLANT_NOTE_HI 72.0f
#define PLANT_FB_LO_DB (-60.0f)         /* the feedback gain's range; the floor is Starve's */
#define PLANT_FB_HI_DB 12.0f
#define PLANT_STR_FB 0.9f               /* the string's own feedback (its ring) */
#define PLANT_DAMP_X 10.0f              /* the string's one-pole damping, at this many times its pitch (the fundamental
                                         * keeps the most loop gain, so the loop plays the note) */
#define PLANT_DC_HZ 3.0f                /* its DC block */
#define PLANT_BODY_R_SHORTER 4.0f       /* samples */

typedef struct {
    float note;                         /* MIDI, fractional; clamped to 16..72 */
    float body;                         /* 0..1: the body delay, 1..100 ms (exponential) */
    float drive;                        /* 0..1 (0.4 the default) */
    float lpf_hz, hpf_hz;               /* 100..18000, 10..4000 */
    float fb_db;                        /* -60..+12 */
    float seed_db;                      /* the noise seed, dBFS (-90 the default) */
    float press;                        /* the pressure feed, 0..1 (linear level of noise into the string) */
    float ret;                          /* the echo's return into the feed, 0..1 */
    int starve;                         /* 1: the feedback gain cut to the floor */
} plant_params_t;

typedef struct {
    float *buf;                         /* PLANT_STRING_LEN floats (the app's pool) */
    uint32_t w;
    float d;                            /* the read delay now, samples (moves to d_target over a block) */
    float dc_x1, dc_y1, damp_y;
} tf_string_t;

typedef struct {
    tf_string_t s[2];
    float *body[2];                     /* PLANT_BODY_LEN floats each */
    uint32_t bw;
    float body_d, body_target, body_a;  /* the body delay, samples, smoothed per sample (tape-like) */
    tf_svf_coef_t lpc, hpc;
    tf_svf_t lp[2], hp[2];
    float drive_g, drive_ig;
    float g, g_target, g_a;             /* the feedback gain (linear), smoothed per sample */
    float seed, press, press_target, ret;
    float imp_env, imp_decay;           /* the velocity impulse: a noise burst's envelope */
    float d_target, d_step;             /* the string's delay: its target, and the step a sample this block */
    float dc_r, damp_a;
    float note;                         /* the note it is tuned to (after the clamp) */
    uint32_t rng;
} plant_t;

void plant_init(plant_t *p, float *str_l, float *str_r, float *body_l, float *body_r, uint32_t seed);
void plant_reset(plant_t *p);           /* the lines and states to 0 (boot: quiet) */
void plant_set(plant_t *p, const plant_params_t *k);   /* once a block, before the samples */
void plant_impulse(plant_t *p, float amp);              /* a noise burst into the feed, amp 0..1 */
/* the loop's small-signal peak gain without the feedback gain, dB (the string's peaks through the filters, at its
 * harmonics); the ignition threshold is its negative */
float plant_loop_peak_db(const plant_t *p);
/* how far the loop is above (+) or below (-) ignition with feedback gain fb_db; the gain now, if now */
float plant_margin_db(const plant_t *p, float fb_db);
float plant_margin_now_db(const plant_t *p);
float plant_string_delay(float note, float damp_a, float dc_r);   /* the tuned line delay for a note, samples */
float plant_damp_a(float note);          /* the damping's coefficient for a note */
float plant_body_delay(const plant_t *p, float want);    /* the body delay locked to the pitch, nearest to want samples */
void plant_sanitize(plant_t *p);        /* end of a block: denormals flushed, a blown-up state reset */

/* ------------------------------------------------------------- per sample --- */
static inline float plant_string_tick(tf_string_t *s, const plant_t *p, float in)
{
    const float *b = s->buf;
    float d = s->d, f, xm1, x0, x1, x2, c1, c2, c3, v;
    int32_t di = (int32_t)d, i0, im1, i1, i2;
    f = d - (float)di;
    i0 = (int32_t)s->w - di;
    if (i0 < 0)
        i0 += (int32_t)PLANT_STRING_LEN;
    im1 = i0 + 1 == (int32_t)PLANT_STRING_LEN ? 0 : i0 + 1;
    i1 = i0 == 0 ? (int32_t)PLANT_STRING_LEN - 1 : i0 - 1;
    i2 = i1 == 0 ? (int32_t)PLANT_STRING_LEN - 1 : i1 - 1;
    xm1 = b[im1];                       /* newer */
    x0 = b[i0];
    x1 = b[i1];
    x2 = b[i2];                         /* older */
    c1 = 0.5f * (x1 - xm1);             /* 4-point cubic Hermite (Catmull-Rom), f towards the older */
    c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
    c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
    v = ((c3 * f + c2) * f + c1) * f + x0 + in;
    v = fm_clampf(v, -24.0f, 24.0f);
    {                                   /* DC block */
        float y = v - s->dc_x1 + p->dc_r * s->dc_y1;
        s->dc_x1 = v;
        s->dc_y1 = y;
        v = y;
    }
    s->damp_y += p->damp_a * (PLANT_STR_FB * v - s->damp_y);
    s->buf[s->w] = s->damp_y;
    s->w = s->w + 1 == PLANT_STRING_LEN ? 0 : s->w + 1;
    s->d = d + p->d_step;
    return s->damp_y;
}

static inline float plant_body_read(const plant_t *p, int ch, float d)
{
    const float *b = p->body[ch];
    int32_t di = (int32_t)d, i0, i1;
    float f = d - (float)di;
    i0 = (int32_t)p->bw - di;
    if (i0 < 0)
        i0 += (int32_t)PLANT_BODY_LEN;
    i1 = i0 == 0 ? (int32_t)PLANT_BODY_LEN - 1 : i0 - 1;
    return b[i0] + f * (b[i1] - b[i0]);
}

static inline float plant_drive(const plant_t *p, float x)
{
    float u = x * p->drive_g;           /* the clip point at 1 / drive_g: small-signal gain 1 */
    float y = tf_softclip(u);
    return y * p->drive_ig;
}

/* the feed, the strings, the drive and the filters: fl / fr go to the reverb's crossfade. ext: the v1.x feeds (0) */
static inline void plant_tick_a(plant_t *p, float ret_l, float ret_r, float ext, float *fl, float *fr)
{
    float n = tf_noise(&p->rng), feed, il, ir, sl, sr;
    p->press += 0.0015f * (p->press_target - p->press);          /* ~15 ms: pressure moves every 5 ms */
    feed = n * (p->seed + p->press + p->imp_env) + ext;
    p->imp_env *= p->imp_decay;
    p->body_d += p->body_a * (p->body_target - p->body_d);
    il = feed + plant_body_read(p, 0, p->body_d) + p->ret * ret_l;
    ir = feed + plant_body_read(p, 1, p->body_d - PLANT_BODY_R_SHORTER) + p->ret * ret_r;
    sl = plant_string_tick(&p->s[0], p, il);
    sr = plant_string_tick(&p->s[1], p, ir);
    sl = plant_drive(p, sl);
    sr = plant_drive(p, sr);
    *fl = tf_svf_hp(&p->hp[0], &p->hpc, tf_svf_lp(&p->lp[0], &p->lpc, sl));
    *fr = tf_svf_hp(&p->hp[1], &p->hpc, tf_svf_lp(&p->lp[1], &p->lpc, sr));
}

/* after the reverb: the feedback gain, into the body delay */
static inline void plant_tick_b(plant_t *p, float yl, float yr)
{
    p->g += p->g_a * (p->g_target - p->g);
    p->body[0][p->bw] = yl * p->g;
    p->body[1][p->bw] = yr * p->g;
    p->bw = p->bw + 1 == PLANT_BODY_LEN ? 0 : p->bw + 1;
}
