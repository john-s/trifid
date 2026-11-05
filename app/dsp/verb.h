/* SPDX-License-Identifier: MIT
 * Copyright (C) 2026 john-s */
/* The reverb in the loop: a chorused plate, crossfaded into the loop by Mix (0 bypasses it). Written fresh from the
 * published topology (J. Dattorro, "Effect Design, Part 1: Reverberator and Other Filters", JAES 1997), not from
 * DaisySP.
 *
 *   mid in -> bandwidth (one-pole) -> 4 input diffusers -> + -> modulated allpass -> delay -> damping -> x decay
 *                                                          ^      -> allpass -> delay -> (to the other half) ...
 *   two halves cross-fed in a figure of eight; the outputs are 7 taps a side (stereo wet)
 *
 * The modulation: the tank's two allpasses sweep their delay with a quadrature LFO (Depth, Rate), the chorus that
 * keeps the plate from ringing metallic and lets the loop drift. Every delay is 0.55 of Dattorro's sample counts at
 * 29.8 kHz (a smaller plate: the whole tank in 12,400 int16 samples, 24,800 bytes of the pool), stored at full rate
 * (no decimation). The wet path is scaled by its measured peak gain for the decay (verb.c), so it never adds gain to
 * the plant's loop and the ignition meter stays a guarantee. */
#pragma once
#include "dsp_util.h"

#define VERB_LEN 12400u                 /* int16 */
#define VERB_FS 2.0f                    /* the lines' full scale (the loop runs within +-1 after the drive) */

typedef struct {
    float mix;                          /* 0..1: 0 bypasses (the dry loop alone) */
    float decay;                        /* 0..1 */
    float depth;                        /* 0..1: the allpasses' sweep, 0..12 samples */
    float rate;                         /* 0..1: 0.05..5 Hz */
} verb_params_t;

typedef struct { uint16_t at, len; uint16_t w; } verb_line_t;
#ifdef VERB_FLOAT_TEST                  /* tests/dsp/verb_gain.c: the same plate in float, exact (no storage, no dither) */
typedef float verb_s_t;
#else
typedef int16_t verb_s_t;
#endif

typedef struct {
    verb_s_t *buf;
    verb_line_t in[4], apm[2], dl1[2], ap2[2], dl2[2];
    float bw_y, damp_y[2], back[2];     /* the bandwidth filter, the dampings, each half's last output */
    float mix, decay, wet_g, exc;
    float lfo_c, lfo_s, lfo_cw, lfo_sw; /* the quadrature LFO, a phasor turned each sample */
    uint32_t rng;
    int active;
} verb_t;

void verb_init(verb_t *v, verb_s_t *buf);
void verb_reset(verb_t *v);
void verb_set(verb_t *v, const verb_params_t *k);   /* once a block */
void verb_sanitize(verb_t *v);
float verb_wet_gain(float decay);       /* the wet path's scale: 1 / its peak gain at this decay */

/* stored rounded (unbiased); the tank's recirculating writes add TPDF dither of +-1 LSB (verb_wr_d), which keeps
 * them from settling into a limit cycle of a few LSBs (plain rounding made the tail hang at about -66 dBFS at any
 * decay; truncation instead cut the tails short: its bias towards zero outweighs the decay at low levels) */
#ifdef VERB_FLOAT_TEST
TF_INL float verb_q(float x) { return x; }
TF_INL float verb_f(float q) { return q; }
#else
TF_INL int16_t verb_q(float x)
{
    return (int16_t)tf_round(fm_clampf(x * (32767.0f / VERB_FS), -32767.0f, 32767.0f));
}
TF_INL float verb_f(int32_t q) { return (float)q * (VERB_FS / 32767.0f); }
#endif

/* the sample written d samples ago on line l (d 1 .. len) */
/* the raw stored value d samples ago (the output taps sum these, and convert once) */
#ifdef VERB_FLOAT_TEST
TF_INL float verb_raw(const verb_t *v, const verb_line_t *l, uint32_t d)
#else
TF_INL int32_t verb_raw(const verb_t *v, const verb_line_t *l, uint32_t d)
#endif
{
    int32_t i = (int32_t)l->w - (int32_t)d;
    if (i < 0)
        i += l->len;
    return v->buf[l->at + (uint32_t)i];
}
TF_INL float verb_rd(const verb_t *v, const verb_line_t *l, uint32_t d)
{
    int32_t i = (int32_t)l->w - (int32_t)d;
    if (i < 0)
        i += l->len;
    return verb_f(v->buf[l->at + (uint32_t)i]);
}
TF_INL void verb_wr(verb_t *v, verb_line_t *l, float x)
{
    v->buf[l->at + l->w] = verb_q(x);
    l->w = (uint16_t)(l->w + 1u == l->len ? 0u : l->w + 1u);
}
TF_INL void verb_wr_d(verb_t *v, verb_line_t *l, float x)   /* dithered: the tank's recirculation */
{
#ifdef VERB_FLOAT_TEST
    verb_wr(v, l, x);
#else
    uint32_t n;                         /* TPDF from one xorshift: its two halves, each uniform */
    float d;
    n = v->rng;
    n ^= n << 13;
    n ^= n >> 17;
    n ^= n << 5;
    v->rng = n;
    d = ((float)(int32_t)(n & 0xFFFFu) + (float)(int32_t)(n >> 16) - 65535.0f) * (VERB_FS / 32767.0f / 65536.0f);
    verb_wr(v, l, x + d);
#endif
}
/* a delay line: x in, its full length ago out */
TF_INL float verb_delay(verb_t *v, verb_line_t *l, float x)
{
    float y = verb_rd(v, l, l->len);
    verb_wr(v, l, x);
    return y;
}
/* a Schroeder allpass of the line's length: |H| = 1 */
TF_INL float verb_ap(verb_t *v, verb_line_t *l, float g, float x)
{
    float d = verb_rd(v, l, l->len), w = x - g * d;
    verb_wr(v, l, w);
    return d + g * w;
}
/* the same, its delay swept: d = len - 14 + 7 + exc * lfo (fractional, linear) */
TF_INL float verb_apm(verb_t *v, verb_line_t *l, float g, float x, float mod)
{
    float pos = (float)(l->len - 7u) + mod, f, a, b, d, w;
    uint32_t i = (uint32_t)pos;
    f = pos - (float)i;
    a = verb_rd(v, l, i);
    b = verb_rd(v, l, i + 1u);
    d = a + f * (b - a);
    w = x - g * d;
    verb_wr(v, l, w);
    return d + g * w;
}

/* every frame: the loop's two sides in; out the crossfaded pair (dry .. wet by Mix) */
TF_INL void verb_tick(verb_t *v, float *l, float *r)
{
    float x, a, t, wl, wr, c, s;
    int h;
    if (!v->active)
        return;
    c = v->lfo_c * v->lfo_cw - v->lfo_s * v->lfo_sw;            /* the LFO turned one sample */
    s = v->lfo_c * v->lfo_sw + v->lfo_s * v->lfo_cw;
    v->lfo_c = c;
    v->lfo_s = s;
    v->bw_y += 0.9995f * (0.5f * (*l + *r) - v->bw_y);           /* bandwidth */
    x = verb_ap(v, &v->in[0], 0.75f, v->bw_y);
    x = verb_ap(v, &v->in[1], 0.75f, x);
    x = verb_ap(v, &v->in[2], 0.625f, x);
    x = verb_ap(v, &v->in[3], 0.625f, x);
    for (h = 0; h < 2; h++) {           /* the two halves, each fed by the other's last output */
        a = verb_apm(v, &v->apm[h], -0.7f, x + v->decay * v->back[h ^ 1], v->exc * (h ? s : c));
        {                               /* (the first delay of each half, written with dither) */
            float y = verb_rd(v, &v->dl1[h], v->dl1[h].len);
            verb_wr_d(v, &v->dl1[h], a);
            a = y;
        }
        v->damp_y[h] += 0.65f * (a - v->damp_y[h]);              /* damping 0.35 */
        t = verb_ap(v, &v->ap2[h], 0.5f, v->damp_y[h] * v->decay);
        v->back[h] = verb_delay(v, &v->dl2[h], t);
    }
    /* the taps (Dattorro's, scaled): each side mostly from the other half */
    /* (summed as stored, converted once a side) */
    wl = verb_f(verb_raw(v, &v->dl1[1], 146) + verb_raw(v, &v->dl1[1], 1636) - verb_raw(v, &v->ap2[1], 1052)
              + verb_raw(v, &v->dl2[1], 1098) - verb_raw(v, &v->dl1[0], 1095) - verb_raw(v, &v->ap2[0], 103)
              - verb_raw(v, &v->dl2[0], 586));
    wr = verb_f(verb_raw(v, &v->dl1[0], 194) + verb_raw(v, &v->dl1[0], 1995) - verb_raw(v, &v->ap2[0], 675)
              + verb_raw(v, &v->dl2[0], 1470) - verb_raw(v, &v->dl1[1], 1161) - verb_raw(v, &v->ap2[1], 184)
              - verb_raw(v, &v->dl2[1], 67));
    *l += v->mix * (wl * v->wet_g - *l);
    *r += v->mix * (wr * v->wet_g - *r);
}
