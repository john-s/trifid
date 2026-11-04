/* SPDX-License-Identifier: MIT
 * Copyright (C) 2026 john-s */
/* The output: the hard limiter that comes after everything and is never bypassed. Flat
 * otherwise (the Speaker voicing was cut on 2026-11-04: it was not useful). MASTER (the hardware pot) comes after this, in the platform, as a gain of at most 1. */
#pragma once
#include "dsp_util.h"

#define OUT_CEIL 0.8913f                /* -1 dBFS: no sample ever leaves past it */


typedef struct {
    float lim_g, lim_rel;               /* the limiter's gain, its release a sample */
    float gain;                         /* the level into it */
    float peak;                         /* the block's peak after the limiter (the meter) */
} out_t;

void out_init(out_t *o);
void out_sanitize(out_t *o);

/* the limiter: instant attack (the gain drops at once to what keeps this frame at the ceiling), smooth release, then
 * a clamp to the ceiling that also catches a NaN. Nothing above OUT_CEIL can come out. */
static inline void out_limit(out_t *o, float *l, float *r)
{
    float a = fm_maxf(fm_fabsf(*l), fm_fabsf(*r)), g;
    o->lim_g += o->lim_rel * (1.0f - o->lim_g);
    if (a * o->lim_g > OUT_CEIL)
        o->lim_g = OUT_CEIL / a;
    g = o->lim_g;
    *l = tf_finite(*l) ? fm_clampf(*l * g, -OUT_CEIL, OUT_CEIL) : 0.0f;
    *r = tf_finite(*r) ? fm_clampf(*r * g, -OUT_CEIL, OUT_CEIL) : 0.0f;
}

static inline void out_tick(out_t *o, float *l, float *r)
{
    float x = tf_finite(*l) ? *l * o->gain : 0.0f, y = tf_finite(*r) ? *r * o->gain : 0.0f;
    out_limit(o, &x, &y);
    if (fm_fabsf(x) > o->peak)
        o->peak = fm_fabsf(x);
    if (fm_fabsf(y) > o->peak)
        o->peak = fm_fabsf(y);
    *l = x;
    *r = y;
}
