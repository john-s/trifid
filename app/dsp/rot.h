/* SPDX-License-Identifier: MIT
 * Copyright (C) 2026 john-s */
/* Rot: the in-loop reverb, mono, kept at fs/4 in one int16 line of 8,192 samples (743 ms). Written fresh (not
 * DaisySP's ReverbSc).
 *
 *   in -> decimate (Grit: 4th-order AA .. one-pole .. nothing) -> + -> [line] -> 4 taps at prime offsets, each
 *                                                                ^              jittering 1-3 samples, slowly
 *                                                                |                 |  mean
 *                                                   allpass (20-80) <- x Decay <---+--> x (1 - Decay) -> up to fs
 *
 * The wet path's gain is at most 1 at every frequency (the taps' mean is at most 1, the allpass is 1, the loop
 * 1 / (1 - Decay) at its peak, then scaled by 1 - Decay), so Rot never adds gain to the plant's loop. The line is
 * stored rounded with TPDF dither; Grit makes the steps coarser (16 bits at 0, 6 at 1).
 * rot_tick runs every frame (the decimation filter and the reconstruction); the reverb itself every 4th. */
#pragma once
#include "dsp_util.h"

#define ROT_LEN 8192u                   /* int16: 16,384 bytes (the app's pool) */
#define ROT_TAPS 4
#define ROT_AP_MAX 80u
#define ROT_FS 2.0f                     /* the line's full scale (the loop runs at about +-1 after the drive) */

typedef struct {
    float mix;                          /* 0..1: 0 bypasses (the dry loop alone) */
    float decay;                        /* 0..1 */
    float smear;                        /* 0..1: the allpass, 20..80 samples, its coefficient 0.5..0.75 */
    float jitter;                       /* 0..1: 0 none, then 1..3 samples of slow tap jitter */
    float grit;                         /* 0..1: Grit's share (decimation, quantization) */
} rot_params_t;

typedef struct {
    int16_t *buf;
    uint32_t w, ph;                     /* the write index; the frame of 4 */
    float ap[ROT_AP_MAX];
    uint32_t ap_w, ap_len;
    float ap_g;
    float fb, wet_g, mix;
    float jit_amt, jit[ROT_TAPS], jit_to[ROT_TAPS], jit_a;
    uint32_t jit_t;
    int32_t qstep;                      /* quantization step, LSBs */
    float prev, cur;                    /* the last two fs/4 outputs */
    tf_dec_coef_t dc;
    tf_dec_t dec;
    uint32_t rng;
    int active;                         /* mix > 0 (or fading out) */
} rot_t;

extern const uint16_t rot_tap_at[ROT_TAPS];

void rot_init(rot_t *r, int16_t *buf, uint32_t seed);
void rot_reset(rot_t *r);
void rot_set(rot_t *r, const rot_params_t *k);    /* once a block */
void rot_sanitize(rot_t *r);

/* the reverb at fs/4: one decimated sample in, one out */
static inline float rot_core(rot_t *r, float x)
{
    float sum = 0.0f, y, v, d;
    int i;
    for (i = 0; i < ROT_TAPS; i++) {
        float at = (float)rot_tap_at[i] + r->jit[i];
        int32_t ai = (int32_t)at, i0 = (int32_t)r->w - ai, i1;
        float f = at - (float)ai, a, b;
        i0 &= (int32_t)(ROT_LEN - 1u);
        i1 = (i0 - 1) & (int32_t)(ROT_LEN - 1u);
        a = (float)r->buf[i0];
        b = (float)r->buf[i1];
        sum += a + f * (b - a);
    }
    y = sum * (ROT_FS / 32767.0f / (float)ROT_TAPS);
    /* the allpass in the loop (Schroeder): |H| = 1 */
    d = r->ap[r->ap_w >= r->ap_len ? 0 : r->ap_w];
    v = r->fb * y - r->ap_g * d;
    r->ap[r->ap_w] = v;
    r->ap_w = r->ap_w + 1 >= r->ap_len ? 0 : r->ap_w + 1;
    v = d + r->ap_g * v;
    r->buf[r->w] = tf_store16(x + v, 1.0f / ROT_FS, r->qstep, &r->rng);
    r->w = (r->w + 1u) & (ROT_LEN - 1u);
    return y * r->wet_g;
}

/* every frame: x the loop's signal; returns the crossfade (dry .. wet by Mix) */
static inline float rot_tick(rot_t *r, float x)
{
    float in, wet;
    if (!r->active)
        return x;
    in = tf_dec_in(&r->dec, &r->dc, x);
    if (++r->ph == 4u) {
        r->ph = 0;
        r->prev = r->cur;
        r->cur = rot_core(r, in);
    }
    wet = tf_dec_out(&r->dec, &r->dc, r->prev, r->cur, r->ph);
    return x + r->mix * (wet - x);
}
