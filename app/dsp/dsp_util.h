/* SPDX-License-Identifier: MIT
 * Copyright (C) 2026 john-s */
/* Trifid's DSP helpers: everything here is a static inline on floats, with no platform calls, so the DSP modules
 * compile natively (tests/dsp) as well as for the FM-1 (an o2 unit). fastmath.h is fm1-base's (MPL-2.0), a header of
 * pure functions. Written fresh for Trifid: nothing here comes from DaisySP. */
#pragma once
#include <stdint.h>
#include "fastmath.h"

#define TF_FS 44118.0f                  /* the FM-1's measured audio rate (fm1-base's nominal FS is 44100) */
#define TF_FS4 (TF_FS * 0.25f)          /* Rot's and the echo's storage rate */
#define TF_BLOCK 128u                   /* frames a render; the controls move once a block */

/* ------------------------------------------------------------------ noise --- */
/* xorshift32: white, uniform in [-1, 1) */
static inline float tf_noise(uint32_t *s)
{
    uint32_t x = *s;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *s = x;
    return (float)(int32_t)x * (1.0f / 2147483648.0f);
}

/* ------------------------------------------------------------- numerics --- */
static inline int tf_finite(float x)    /* not NaN, not infinite (no libm) */
{
    fm_bits_t b;
    b.f = x;
    return (b.u & 0x7F800000u) != 0x7F800000u;
}
/* a state that has drifted into denormals, or blown up, back to 0 */
static inline float tf_clean(float x) { return tf_finite(x) && fm_fabsf(x) > 1e-20f ? x : 0.0f; }
static inline float tf_lerp(float a, float b, float t) { return a + (b - a) * t; }

/* one-pole coefficient for a cutoff (Hz) at rate fs: y += a (x - y) */
static inline float tf_onepole_a(float fc, float fs) { return 1.0f - fm_expf(-FM_TWO_PI * fc / fs); }
/* the same for a time constant (s) */
static inline float tf_tau_a(float tau, float fs) { return tau <= 0.0f ? 1.0f : 1.0f - fm_expf(-1.0f / (tau * fs)); }

/* atan(x), |error| < 1e-5 rad: for the block-rate tuning sums (phase of a filter at the string's pitch) */
static inline float tf_atanf(float x)
{
    float ax = fm_fabsf(x), r, z, z2;
    int inv = ax > 1.0f;
    z = inv ? 1.0f / ax : ax;
    z2 = z * z;
    r = z * (0.99997726f + z2 * (-0.33262347f + z2 * (0.19354346f + z2 * (-0.11643287f + z2 * (0.05265332f - z2 * 0.01172120f)))));
    if (inv)
        r = 1.5707963f - r;
    return x < 0.0f ? -r : r;
}
static inline float tf_atan2f(float y, float x)
{
    if (x > 0.0f)
        return tf_atanf(y / x);
    if (x < 0.0f)
        return tf_atanf(y / x) + (y >= 0.0f ? FM_PI : -FM_PI);
    return y > 0.0f ? 1.5707963f : y < 0.0f ? -1.5707963f : 0.0f;
}

/* -------------------------------------------------------- shapers (slope <= 1) --- */
/* cubic soft clip, unity slope at 0, flat (and 1) from |x| = 1.5: slope never above 1, so a loop through it is
 * never louder than the linear loop */
static inline float tf_softclip(float x)
{
    x = fm_clampf(x, -1.5f, 1.5f);
    return x - (4.0f / 27.0f) * x * x * x;
}
/* triangle wavefold into [-1, 1]: slope +-1 everywhere */
static inline float tf_fold(float u)
{
    float v = u + 1.0f;
    v -= 4.0f * fm_floorf(v * 0.25f);
    return 1.0f - fm_fabsf(v - 2.0f);
}

/* ------------------------------------------------------------- the SVF --- */
/* Zavalishin's topology-preserving state-variable filter (the trapezoidal integrators): stable while it moves */
typedef struct { float g, k, a1, a2, a3; } tf_svf_coef_t;
typedef struct { float ic1, ic2; } tf_svf_t;

static inline void tf_svf_set(tf_svf_coef_t *c, float fc, float q, float fs)
{
    c->g = fm_tanf(FM_PI * fm_clampf(fc, 1.0f, 0.49f * fs) / fs);
    c->k = 1.0f / q;
    c->a1 = 1.0f / (1.0f + c->g * (c->g + c->k));
    c->a2 = c->g * c->a1;
    c->a3 = c->g * c->a2;
}
/* one step: the low-, band- and high-pass outputs */
static inline void tf_svf_tick(tf_svf_t *s, const tf_svf_coef_t *c, float x, float *lp, float *bp, float *hp)
{
    float v3 = x - s->ic2;
    float v1 = c->a1 * s->ic1 + c->a2 * v3;
    float v2 = s->ic2 + c->a2 * s->ic1 + c->a3 * v3;
    s->ic1 = 2.0f * v1 - s->ic1;
    s->ic2 = 2.0f * v2 - s->ic2;
    *lp = v2;
    *bp = v1;
    *hp = x - c->k * v1 - v2;
}
static inline float tf_svf_lp(tf_svf_t *s, const tf_svf_coef_t *c, float x)
{
    float v3 = x - s->ic2;
    float v1 = c->a1 * s->ic1 + c->a2 * v3;
    float v2 = s->ic2 + c->a2 * s->ic1 + c->a3 * v3;
    s->ic1 = 2.0f * v1 - s->ic1;
    s->ic2 = 2.0f * v2 - s->ic2;
    return v2;
}
static inline float tf_svf_hp(tf_svf_t *s, const tf_svf_coef_t *c, float x)
{
    float v3 = x - s->ic2;
    float v1 = c->a1 * s->ic1 + c->a2 * v3;
    float v2 = s->ic2 + c->a2 * s->ic1 + c->a3 * v3;
    s->ic1 = 2.0f * v1 - s->ic1;
    s->ic2 = 2.0f * v2 - s->ic2;
    return x - c->k * v1 - v2;
}
static inline void tf_svf_clean(tf_svf_t *s)
{
    s->ic1 = tf_clean(s->ic1);
    s->ic2 = tf_clean(s->ic2);
}
/* |H| and arg H of the SVF's low-pass (hp = 0) or high-pass (hp = 1) at w (radians a sample): the trapezoidal SVF is
 * the bilinear transform of the analog prototype, so H(e^jw) = H_a(j tan(w/2) / g) */
static inline void tf_svf_resp(const tf_svf_coef_t *c, int hp, float w, float *mag, float *ph)
{
    float W = fm_tanf(0.5f * fm_minf(w, 3.1f)) / c->g;          /* s = jW */
    float re = 1.0f - W * W, im = c->k * W;                     /* the denominator 1 + k s + s^2 */
    float d2 = re * re + im * im;
    float num = hp ? W * W : 1.0f;                              /* |s^2| or 1 */
    *mag = num / fm_sqrtf(d2);
    *ph = (hp ? FM_PI : 0.0f) - tf_atan2f(im, re);              /* s^2 = -W^2: a half turn */
}

/* ------------------------------------------------------------ int16 storage --- */
/* a float in [-fs, fs] stored as int16, rounded, with light TPDF dither: +-1 LSB of 16 bits, whatever the step.
 * step is in LSBs (1: 16 bits, 2^k: 16 - k bits). At 16 bits the dither linearises the rounding (clean); at coarse
 * steps it is far smaller than a step, so the rounding stays a distortion that follows the signal (crunch), and a
 * signal under half a step stores as silence, not as hiss */
static inline int16_t tf_store16(float x, float inv_fs, int32_t step, uint32_t *rng)
{
    float d = (tf_noise(rng) + tf_noise(rng)) * 0.5f;           /* triangular, +-1 LSB */
    float v = x * inv_fs * 32767.0f + d;
    int32_t q;
    if (!tf_finite(v))
        return 0;
    v = fm_clampf(v, -32767.0f, 32767.0f);
    if (step > 1) {
        float u = v / (float)step;
        q = (int32_t)(u + (u >= 0.0f ? 0.5f : -0.5f)) * step;
    } else {
        q = (int32_t)(v + (v >= 0.0f ? 0.5f : -0.5f));
    }
    return (int16_t)(q < -32767 ? -32767 : q > 32767 ? 32767 : q);
}

/* --------------------------------------------- decimate by 4, and back up --- */
/* Rot and the echo store at fs/4 (Nyquist 5.5 kHz). Grit sets how clean that is: 0, a 6th-order Butterworth at
 * 3.8 kHz before the decimation (what would fold into the passband is 40 dB down or more) and linear interpolation
 * with a 2-pole smoother after it (the images); towards 0.5 the pre-filter becomes a one-pole whose cutoff rises; at
 * 1 nothing at all: the sample taken every 4th frame and held (zero-order hold) */
typedef struct {
    tf_svf_coef_t aa1, aa2, aa3, post;
    float op_a;                         /* the one-pole's coefficient */
    float w_op, w_raw;                  /* the blend: aa4 -> one-pole (w_op), -> raw (w_raw) */
    float w_hold;                       /* the reconstruction: smooth (0) -> zero-order hold (1) */
} tf_dec_coef_t;
typedef struct {
    tf_svf_t a1, a2, a3, post;
    float op;
} tf_dec_t;

static inline void tf_dec_set(tf_dec_coef_t *c, float grit)
{
    float g = fm_clampf(grit, 0.0f, 1.0f);
    tf_svf_set(&c->aa1, 3800.0f, 0.5176f, TF_FS);
    tf_svf_set(&c->aa2, 3800.0f, 0.7071f, TF_FS);
    tf_svf_set(&c->aa3, 3800.0f, 1.9319f, TF_FS);
    tf_svf_set(&c->post, 4000.0f, 0.7071f, TF_FS);
    c->op_a = tf_onepole_a(3000.0f + 14000.0f * g, TF_FS);
    c->w_op = fm_minf(1.0f, g * 2.0f);
    c->w_raw = fm_clampf(g * 2.0f - 1.0f, 0.0f, 1.0f);
    c->w_hold = g;
}
/* the pre-filter, every frame (its output is kept every 4th) */
static inline float tf_dec_in(tf_dec_t *d, const tf_dec_coef_t *c, float x)
{
    float a = tf_svf_lp(&d->a3, &c->aa3, tf_svf_lp(&d->a2, &c->aa2, tf_svf_lp(&d->a1, &c->aa1, x)));
    float o;
    d->op += c->op_a * (x - d->op);
    o = tf_lerp(a, d->op, c->w_op);
    return tf_lerp(o, x, c->w_raw);
}
/* the reconstruction, every frame: prev and cur the last two fs/4 samples, ph 0..3 the frame since cur */
static inline float tf_dec_out(tf_dec_t *d, const tf_dec_coef_t *c, float prev, float cur, uint32_t ph)
{
    float lin = tf_lerp(prev, cur, (float)ph * 0.25f);
    float sm = tf_svf_lp(&d->post, &c->post, lin);
    return tf_lerp(sm, cur, c->w_hold);
}
static inline void tf_dec_clean(tf_dec_t *d)
{
    tf_svf_clean(&d->a1);
    tf_svf_clean(&d->a2);
    tf_svf_clean(&d->a3);
    tf_svf_clean(&d->post);
    d->op = tf_clean(d->op);
}
