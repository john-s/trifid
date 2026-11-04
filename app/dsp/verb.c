/* SPDX-License-Identifier: MIT
 * Copyright (C) 2026 john-s */
/* The chorused plate's block-rate side (verb.h) */
#include "verb.h"

/* the lines, 0.55 of Dattorro's lengths at 29.8 kHz; the modulated allpasses 14 longer (their sweep, +-7) */
static const uint16_t LEN_IN[4] = {78, 59, 208, 152};
static const uint16_t LEN_APM[2] = {370 + 14, 499 + 14}, LEN_DL1[2] = {2449, 2319}, LEN_AP2[2] = {990, 1461},
                      LEN_DL2[2] = {2046, 1740};

/* the wet path's energy gain (the impulse response's RMS sum: its average |H|), measured on the exact float plate at
 * decay 0, 0.1 .. 1 (tests/dsp/verb_gain.c). Scaled by it, the reverb neither adds nor takes loop gain on average; at
 * its resonances it adds some (verb_ring_db), which the meter counts */
#ifndef VERB_GAIN_TABLE
#define VERB_GAIN_TABLE 1.756f, 1.781f, 1.816f, 1.862f, 1.921f, 1.999f, 2.102f, 2.244f, 2.450f, 2.802f, 3.782f
#endif
static const float ENERGY[11] = {VERB_GAIN_TABLE};

static float table(const float *t, float decay)
{
    float x = fm_clampf(decay, 0.0f, 1.0f) * 10.0f;
    int i = (int)x;
    if (i >= 10)
        i = 9;
    return t[i] + (x - (float)i) * (t[i + 1] - t[i]);
}
float verb_wet_gain(float decay) { return 1.0f / table(ENERGY, decay); }

static void line(verb_line_t *l, uint32_t *at, uint32_t len)
{
    l->at = (uint16_t)*at;
    l->len = (uint16_t)len;
    l->w = 0;
    *at += len;
}

void verb_init(verb_t *v, verb_s_t *buf)
{
    uint32_t at = 0, i;
    v->buf = buf;
    v->rng = 0x6A09E667u;
    for (i = 0; i < 4u; i++)
        line(&v->in[i], &at, LEN_IN[i]);
    for (i = 0; i < 2u; i++) {
        line(&v->apm[i], &at, LEN_APM[i]);
        line(&v->dl1[i], &at, LEN_DL1[i]);
        line(&v->ap2[i], &at, LEN_AP2[i]);
        line(&v->dl2[i], &at, LEN_DL2[i]);
    }
    (void)sizeof(char[at <= VERB_LEN ? 1 : -1]);
    verb_reset(v);
}

void verb_reset(verb_t *v)
{
    uint32_t i;
    for (i = 0; i < VERB_LEN; i++)
        v->buf[i] = 0;
    v->bw_y = v->damp_y[0] = v->damp_y[1] = v->back[0] = v->back[1] = 0.0f;
    v->mix = 0.0f;
    v->decay = 0.5f;
    v->wet_g = verb_wet_gain(0.5f);
    v->exc = 0.0f;
    v->lfo_c = 1.0f;
    v->lfo_s = 0.0f;
    v->lfo_cw = 1.0f;
    v->lfo_sw = 0.0f;
    v->active = 0;
}

void verb_set(verb_t *v, const verb_params_t *k)
{
    float d = fm_clampf(k->decay, 0.0f, 1.0f), hz, w, n;
    v->mix = fm_clampf(k->mix, 0.0f, 1.0f);
    v->decay = 0.2f + 0.77f * d;                                /* 0.2 .. 0.97 */
    v->wet_g = verb_wet_gain(d);
    v->exc = 6.0f * fm_clampf(k->depth, 0.0f, 1.0f);
    hz = 0.05f * fm_exp2f(6.643856f * fm_clampf(k->rate, 0.0f, 1.0f));   /* 0.05 .. 5 Hz */
    w = FM_TWO_PI * hz / TF_FS;
    v->lfo_cw = fm_cosf(w);
    v->lfo_sw = fm_sinf(w);
    n = fm_rsqrtf(v->lfo_c * v->lfo_c + v->lfo_s * v->lfo_s);   /* the phasor kept on the unit circle */
    if (!tf_finite(n) || n > 2.0f || n < 0.5f) {
        v->lfo_c = 1.0f;
        v->lfo_s = 0.0f;
    } else {
        v->lfo_c *= n;
        v->lfo_s *= n;
    }
    if (v->mix > 0.0f && !v->active) {  /* waking: what was in the plate is from long ago */
        uint32_t i;
        for (i = 0; i < VERB_LEN; i++)
            v->buf[i] = 0;
        v->bw_y = v->damp_y[0] = v->damp_y[1] = v->back[0] = v->back[1] = 0.0f;
    }
    v->active = v->mix > 0.0f;
}

void verb_sanitize(verb_t *v)
{
    int bad = !tf_finite(v->bw_y + v->damp_y[0] + v->damp_y[1] + v->back[0] + v->back[1]);
    v->bw_y = tf_clean(v->bw_y);
    v->damp_y[0] = tf_clean(v->damp_y[0]);
    v->damp_y[1] = tf_clean(v->damp_y[1]);
    v->back[0] = tf_clean(v->back[0]);
    v->back[1] = tf_clean(v->back[1]);
    if (bad) {
        uint32_t i;
        for (i = 0; i < VERB_LEN; i++)
            v->buf[i] = 0;
    }
}
