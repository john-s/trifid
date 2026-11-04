/* SPDX-License-Identifier: MIT
 * Copyright (C) 2026 john-s */
/* The plant's block-rate side: coefficients, tuning, the gain margin (plant.h). Concept after Audrey II by Synthux
 * Academy (Nick Donaldson / Infrasonic Audio, Roey Tsemah; MIT): no code of it is used. */
#include "plant.h"

void plant_init(plant_t *p, float *str_l, float *str_r, float *body_l, float *body_r, uint32_t seed)
{
    p->s[0].buf = str_l;
    p->s[1].buf = str_r;
    p->body[0] = body_l;
    p->body[1] = body_r;
    p->rng = seed ? seed : 0x7F4A7C15u;
    p->dc_r = 1.0f - FM_TWO_PI * PLANT_DC_HZ / TF_FS;
    p->body_a = tf_tau_a(0.2f, TF_FS);
    p->imp_decay = fm_expf(-1.0f / (0.004f * TF_FS));             /* 4 ms */
    plant_reset(p);
}

void plant_reset(plant_t *p)
{
    uint32_t i, c;
    for (c = 0; c < 2u; c++) {
        for (i = 0; i < PLANT_STRING_LEN; i++)
            p->s[c].buf[i] = 0.0f;
        for (i = 0; i < PLANT_BODY_LEN; i++)
            p->body[c][i] = 0.0f;
        p->s[c].w = 0;
        p->s[c].dc_x1 = p->s[c].dc_y1 = p->s[c].damp_y = 0.0f;
        p->lp[c].ic1 = p->lp[c].ic2 = p->hp[c].ic1 = p->hp[c].ic2 = 0.0f;
    }
    p->bw = 0;
    p->g = p->g_target = 0.001f;                                   /* the floor: quiet */
    p->press = p->press_target = 0.0f;
    p->imp_env = 0.0f;
    p->note = 40.0f;
    p->damp_a = plant_damp_a(p->note);
    p->d_target = plant_string_delay(p->note, p->damp_a, p->dc_r);
    p->s[0].d = p->s[1].d = p->d_target;
    p->d_step = 0.0f;
    p->body_d = p->body_target = 0.01f * TF_FS;
    tf_svf_set(&p->lpc, 18000.0f, 0.7071f, TF_FS);
    tf_svf_set(&p->hpc, 10.0f, 0.7071f, TF_FS);
    p->drive_g = 1.0f;
    p->drive_ig = 1.0f;
    p->fold = 0.0f;
    p->fold_k = p->fold_ik = 1.0f;
    p->seed = 0.0f;
    p->ret = 0.0f;
    p->g_a = tf_tau_a(0.02f, TF_FS);
}

float plant_damp_a(float note)
{
    float f0 = 440.0f * fm_exp2f((fm_clampf(note, PLANT_NOTE_LO, PLANT_NOTE_HI) - 69.0f) * (1.0f / 12.0f));
    return tf_onepole_a(fm_clampf(f0 * PLANT_DAMP_X, 80.0f, 12000.0f), TF_FS);
}

/* the line delay that puts the string's resonance on the note: one period, less the phase the DC block and the
 * damping add at the fundamental (D = (2 pi + arg Hdamp + arg Hdc) / w0) */
float plant_string_delay(float note, float damp_a, float dc_r)
{
    float f0 = 440.0f * fm_exp2f((fm_clampf(note, PLANT_NOTE_LO, PLANT_NOTE_HI) - 69.0f) * (1.0f / 12.0f));
    float w = FM_TWO_PI * f0 / TF_FS, cw = fm_cosf(w), sw = fm_sinf(w);
    float b = 1.0f - damp_a;
    float ph_damp = -tf_atan2f(b * sw, 1.0f - b * cw);            /* a / (1 - b z^-1) */
    /* (1 - z^-1) / (1 - r z^-1): numerator's phase minus the denominator's */
    float ph_dc = tf_atan2f(sw, 1.0f - cw) - tf_atan2f(dc_r * sw, 1.0f - dc_r * cw);
    float d = (FM_TWO_PI + ph_damp + ph_dc) / w;
    return fm_clampf(d, 2.0f, (float)PLANT_STRING_LEN - 3.0f);
}

/* the phase the string (at its peak: its damping and DC block), the LPF and the HPF add at w */
static float loop_phase(const plant_t *p, float w)
{
    float cw = fm_cosf(w), sw = fm_sinf(w), b = 1.0f - p->damp_a, m, ph_lp, ph_hp;
    float ph = -tf_atan2f(b * sw, 1.0f - b * cw) + tf_atan2f(sw, 1.0f - cw) - tf_atan2f(p->dc_r * sw, 1.0f - p->dc_r * cw);
    tf_svf_resp(&p->lpc, 0, w, &m, &ph_lp);
    tf_svf_resp(&p->hpc, 1, w, &m, &ph_hp);
    return ph + ph_lp + ph_hp;
}

/* the body delay, locked to the pitch: the whole outer loop (the string at its peak, the filters, the body) made a
 * whole number of the string's periods, the nearest to the knob's time, so the outer loop's modes fall on the string's
 * harmonics and the loop plays the note (a free body delay makes the loop's own period the pitch, which is Audrey II's
 * "rough" pitch). B = (2 pi m + phase) / w0, m >= 1 */
float plant_body_delay(const plant_t *p, float want)
{
    float f0 = 440.0f * fm_exp2f((p->note - 69.0f) * (1.0f / 12.0f)), w = FM_TWO_PI * f0 / TF_FS;
    float ph = loop_phase(p, w), per = FM_TWO_PI / w, lo = 2.0f + PLANT_BODY_R_SHORTER, hi = (float)PLANT_BODY_LEN - 3.0f;
    float m = fm_floorf(want / per + 0.5f), b;
    if (m < 1.0f)
        m = 1.0f;
    b = (FM_TWO_PI * m + ph) / w;
    while (b > hi && m > 1.0f) {
        m -= 1.0f;
        b = (FM_TWO_PI * m + ph) / w;
    }
    while (b < lo) {
        m += 1.0f;
        b = (FM_TWO_PI * m + ph) / w;
    }
    return fm_clampf(b, lo, hi);
}

void plant_set(plant_t *p, const plant_params_t *k)
{
    float ms, d, drv;
    p->note = fm_clampf(k->note, PLANT_NOTE_LO, PLANT_NOTE_HI);
    p->damp_a = plant_damp_a(p->note);
    d = plant_string_delay(p->note, p->damp_a, p->dc_r);
    p->d_target = d;
    p->s[1].d = p->s[0].d;                                         /* (one pitch: kept in step) */
    p->d_step = (d - p->s[0].d) * (1.0f / (float)TF_BLOCK);         /* a straight line to it over the block */
    ms = 1.0f * fm_exp2f(fm_clampf(k->body, 0.0f, 1.0f) * 6.643856f);   /* 1 .. 100 ms */
    tf_svf_set(&p->lpc, fm_clampf(k->lpf_hz, 100.0f, 18000.0f), 0.7071f, TF_FS);
    tf_svf_set(&p->hpc, fm_clampf(k->hpf_hz, 10.0f, 4000.0f), 0.7071f, TF_FS);
    p->body_target = plant_body_delay(p, ms * 0.001f * TF_FS);
    drv = fm_clampf(k->drive, 0.0f, 1.0f);
    p->drive_g = 1.0f + 9.0f * drv * drv * 1.5625f;                 /* 0.4: x 3.25; 1: x 15 */
    p->drive_ig = 1.0f / p->drive_g;
    p->fold = fm_clampf(k->fold, 0.0f, 1.0f);
    p->fold_k = 1.0f + 4.0f * p->fold;
    p->fold_ik = 1.0f / p->fold_k;
    p->seed = fm_db2lin(fm_clampf(k->seed_db, -140.0f, -40.0f));
    p->press_target = fm_clampf(k->press, 0.0f, 1.0f);
    p->ret = fm_clampf(k->ret, 0.0f, 1.0f);
    if (k->starve) {                                               /* Starve: fast, still smoothed (no click) */
        p->g_target = fm_db2lin(PLANT_FB_LO_DB);
        p->g_a = tf_tau_a(0.004f, TF_FS);
    } else {
        p->g_target = fm_db2lin(fm_clampf(k->fb_db, PLANT_FB_LO_DB, PLANT_FB_HI_DB));
        p->g_a = tf_tau_a(0.03f, TF_FS);
    }
}

void plant_impulse(plant_t *p, float amp)
{
    amp = fm_clampf(amp, 0.0f, 1.0f);
    if (amp > p->imp_env)
        p->imp_env = amp;
}

/* the string's peak gain at harmonic f (Hz) through the loop filters: r |Hd Hdc| / (1 - r |Hd Hdc|) times |LP HP| */
static float loop_gain_at(const plant_t *p, float f)
{
    float w = FM_TWO_PI * f / TF_FS, cw = fm_cosf(w), sw = fm_sinf(w);
    float b = 1.0f - p->damp_a, m_lp, m_hp, ph, m_d, m_dc, l;
    m_d = p->damp_a / fm_sqrtf((1.0f - b * cw) * (1.0f - b * cw) + b * b * sw * sw);
    m_dc = fm_sqrtf(((1.0f - cw) * (1.0f - cw) + sw * sw) /
                    ((1.0f - p->dc_r * cw) * (1.0f - p->dc_r * cw) + p->dc_r * p->dc_r * sw * sw));
    tf_svf_resp(&p->lpc, 0, w, &m_lp, &ph);
    tf_svf_resp(&p->hpc, 1, w, &m_hp, &ph);
    l = PLANT_STR_FB * m_d * m_dc;
    if (l > 0.9999f)
        l = 0.9999f;
    return l / (1.0f - l) * m_lp * m_hp;
}

float plant_loop_peak_db(const plant_t *p)
{
    float f0 = 440.0f * fm_exp2f((p->note - 69.0f) * (1.0f / 12.0f)), best = 1e-9f, k = 1.0f;
    while (k * f0 < 0.49f * TF_FS) {   /* every harmonic up to 24, then 6 % apart (the peaks change slowly there) */
        float g = loop_gain_at(p, k * f0);
        if (g > best)
            best = g;
        k = k < 24.0f ? k + 1.0f : fm_floorf(k * 1.06f);
    }
    return 20.0f * fm_log2f(best) * 0.30103f;
}

float plant_margin_db(const plant_t *p, float fb_db) { return fb_db + plant_loop_peak_db(p); }

float plant_margin_now_db(const plant_t *p)
{
    return 20.0f * fm_log2f(fm_maxf(p->g, 1e-6f)) * 0.30103f + plant_loop_peak_db(p);
}

void plant_sanitize(plant_t *p)
{
    uint32_t c, i;
    int bad = !tf_finite(p->g) || !tf_finite(p->press) || !tf_finite(p->body_d);
    for (c = 0; c < 2u; c++) {
        tf_string_t *s = &p->s[c];
        bad |= !tf_finite(s->damp_y + s->dc_y1 + s->dc_x1) || !tf_finite(s->d);
        s->damp_y = tf_clean(s->damp_y);
        s->dc_y1 = tf_clean(s->dc_y1);
        s->dc_x1 = tf_clean(s->dc_x1);
        bad |= !tf_finite(p->lp[c].ic1 + p->lp[c].ic2 + p->hp[c].ic1 + p->hp[c].ic2);
        tf_svf_clean(&p->lp[c]);
        tf_svf_clean(&p->hp[c]);
    }
    p->imp_env = tf_clean(p->imp_env);
    if (bad) {                                  /* a NaN got in (it cannot from the loop itself): start again */
        for (c = 0; c < 2u; c++)
            for (i = 0; i < PLANT_STRING_LEN; i++)
                p->s[c].buf[i] = 0.0f;
        for (c = 0; c < 2u; c++)
            for (i = 0; i < PLANT_BODY_LEN; i++)
                p->body[c][i] = 0.0f;
        for (c = 0; c < 2u; c++) {
            p->s[c].damp_y = p->s[c].dc_y1 = p->s[c].dc_x1 = 0.0f;
            p->lp[c].ic1 = p->lp[c].ic2 = p->hp[c].ic1 = p->hp[c].ic2 = 0.0f;
            p->s[c].d = p->d_target;
        }
        p->g = fm_db2lin(PLANT_FB_LO_DB);
        p->press = 0.0f;
        p->body_d = p->body_target;
    }
}
