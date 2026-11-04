/* SPDX-License-Identifier: MIT
 * Copyright (c) 2016 airwindows (Chris Johnson): BitGlitter's algorithm, MIT (see glitter.c)
 * Copyright (C) 2026 john-s: this port */
/* Glitter: Airwindows' BitGlitter, an old-sampler lo-fi, after the loop and before the echo (so the echo repeats the
 * crushed sound and the loop's ignition is untouched). In single precision for the FM-1's FPU.
 *
 *   in x Input -> sine saturation -> two sample-rate reducers, one 1.618x the other's rate, each held sample's edge
 *   softened and crushed to a fixed step (about 9-10 bits) -> averaged -> darkened by up to half -> x Output -> Dry/Wet
 *
 * Glitter sets the reduction: the slower reducer holds a sample for (1 + Glitter)^7 + 2 frames, 3 at 0, 130 at 1.
 * Dry/Wet 0 bypasses it entirely (the clean core). */
#pragma once
#include "dsp_util.h"

typedef struct {
    float input;                        /* 0..1: -18..+18 dB */
    float glitter;                      /* 0..1 */
    float output;                       /* 0..1: -18..+18 dB */
    float wet;                          /* 0..1: 0 bypasses */
} glitter_params_t;

typedef struct {
    float ingain, outgain, wet, rate_a, rate_b;
    float half[2], last_in[2], held_a[2], held_b[2], pos_a[2], pos_b[2], last_out[2];
} glitter_t;

void glitter_init(glitter_t *g);
void glitter_set(glitter_t *g, const glitter_params_t *k);   /* once a block */
void glitter_sanitize(glitter_t *g);

#define GLITTER_REZ_A 0.0016666666666667f   /* BitGlitter's fixed steps */
#define GLITTER_REZ_B 0.0026666666666667f

/* round away from zero to a multiple of rez (BitGlitter's loop of subtractions, as arithmetic), then its scale */
static inline float glitter_crush(float x, float rez)
{
    float m = fm_fabsf(x), q = -fm_floorf(-m / rez) * rez;      /* ceil */
    q *= 1.0f - rez;
    return x < 0.0f ? -q : q;
}

/* one reducer: hold a sample every 1/rate frames, its edge softened (the held sample interpolated towards BitGlitter's
 * lastSample, which it never updates: towards 0), crushed */
static inline float glitter_reduce(float *pos, float *held, float rate, float in, float rez)
{
    float out = *held;
    *pos += rate;
    if (*pos > 1.0f) {
        *pos -= 1.0f;
        *held = in * (1.0f - *pos);
        out = 0.5f * out + 0.5f * *held;
    }
    return glitter_crush(out, rez);
}

static inline float glitter_ch(glitter_t *g, int c, float x)
{
    float dry = x, a, b, m, half, o;
    if (g->wet <= 0.0f)
        return x;
    x = fm_clampf(x * g->ingain, -1.0f, 1.0f) * 1.2533141373155f;   /* the sine saturation */
    m = fm_fabsf(x);
    x = m > 0.0f ? fm_sinf(x * m) / m : 0.0f;
    half = 0.5f * (x + g->last_in[c]);  /* (BitGlitter's crude oversampling: the halfway sample) */
    g->last_in[c] = x;
    a = glitter_reduce(&g->pos_a[c], &g->held_a[c], g->rate_a, x, GLITTER_REZ_A);
    b = glitter_reduce(&g->pos_b[c], &g->held_b[c], g->rate_b, half, GLITTER_REZ_B);
    x = 0.5f * (a + b);
    o = x * (1.0f - 0.5f * g->wet) + g->last_out[c] * (0.5f * g->wet);   /* darker with more wet, at most half */
    g->last_out[c] = x;
    o *= g->outgain;
    return g->wet < 1.0f ? dry + g->wet * (o - dry) : o;   /* (as BitGlitter: no blend at full wet) */
}

static inline void glitter_tick(glitter_t *g, float *l, float *r)
{
    *l = glitter_ch(g, 0, *l);
    *r = glitter_ch(g, 1, *r);
}
