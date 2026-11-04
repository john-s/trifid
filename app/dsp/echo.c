/* SPDX-License-Identifier: MIT
 * Copyright (C) 2026 john-s */
/* The tape echo's block-rate side (echo.h) */
#include "echo.h"

void echo_init(echo_t *e, int16_t *buf_l, int16_t *buf_r, uint32_t seed)
{
    e->buf[0] = buf_l;
    e->buf[1] = buf_r;
    e->rng = seed ? seed : 0x9E3779B9u;
    echo_reset(e);
}

void echo_reset(echo_t *e)
{
    uint32_t i;
    int c;
    for (c = 0; c < 2; c++) {
        for (i = 0; i < ECHO_LEN; i++)
            e->buf[c][i] = 0;
        e->bps[c].ic1 = e->bps[c].ic2 = 0.0f;
        e->dec[c].a1.ic1 = e->dec[c].a1.ic2 = e->dec[c].a2.ic1 = e->dec[c].a2.ic2 = e->dec[c].a3.ic1 = e->dec[c].a3.ic2 = 0.0f;
        e->dec[c].post.ic1 = e->dec[c].post.ic2 = e->dec[c].op = 0.0f;
        e->prev[c] = e->cur[c] = 0.0f;
    }
    e->w = e->ph = 0;
    e->d = e->d_target = 0.5f * TF_FS4;
    e->d_a = tf_tau_a(0.25f, TF_FS4);
    e->wow_ph = 0.0f;
    e->wow_inc = 0.55f / TF_FS4;
    e->flut = e->flut_to = 0.0f;
    e->send = e->fb = e->hiss = 0.0f;
    tf_svf_set(&e->bp, 800.0f, 0.7f, TF_FS4);
    tf_dec_set(&e->dc, 0.0f);
    e->drop_g = e->drop_to = 1.0f;
    e->drop_rate = 0.0f;
    e->drop_left = 0;
    e->qstep = 1;
    e->frozen = 0;
    e->frz_d = 0;
}

void echo_set(echo_t *e, const echo_params_t *k)
{
    float wear = fm_clampf(k->wear, 0.0f, 1.0f);
    e->send = fm_clampf(k->send, 0.0f, 1.0f);
    e->fb = fm_clampf(k->feedback, 0.0f, 1.25f);
    e->d_target = fm_clampf(k->time_s, ECHO_MIN_S, ECHO_MAX_S) * TF_FS4;
    e->hiss = wear * wear * 0.0015f;                        /* tape hiss: about -56 dB at full Wear */
    e->qstep = wear <= 0.0f ? 1 : (int32_t)1 << (int32_t)(wear * 9.0f + 0.5f);   /* 16 bits .. 7 */
    tf_dec_set(&e->dc, k->grit);
    if (k->freeze && !e->frozen) {
        uint32_t d = (uint32_t)(e->d + 0.5f);
        e->frz_d = d < 2u ? 2u : d > ECHO_LEN - 1u ? ECHO_LEN - 1u : d;
    }
    e->frozen = k->freeze != 0;
    /* flutter: a new random place every block, smoothed per sample (a few Hz) */
    e->flut_to = tf_noise(&e->rng);
    /* dropouts, more often and deeper with Wear: a dip of 20..120 ms, about every 2 s at full Wear */
    if (e->drop_left) {
        if (--e->drop_left == 0)
            e->drop_to = 1.0f;
    } else if (wear > 0.0f && (tf_noise(&e->rng) * 0.5f + 0.5f) < wear * 0.0015f) {
        e->drop_to = 1.0f - 0.9f * wear * (0.5f + 0.5f * (tf_noise(&e->rng) * 0.5f + 0.5f));
        e->drop_left = 7u + (uint32_t)((tf_noise(&e->rng) * 0.5f + 0.5f) * 34.0f);   /* blocks: 20..120 ms */
    }
    if (wear <= 0.0f)
        e->drop_to = 1.0f, e->drop_left = 0;
}

void echo_sanitize(echo_t *e)
{
    int c;
    int bad = !tf_finite(e->d + e->drop_g + e->flut);
    for (c = 0; c < 2; c++) {
        bad |= !tf_finite(e->prev[c] + e->cur[c] + e->bps[c].ic1 + e->bps[c].ic2);
        tf_svf_clean(&e->bps[c]);
        tf_dec_clean(&e->dec[c]);
    }
    if (bad) {
        for (c = 0; c < 2; c++) {
            e->bps[c].ic1 = e->bps[c].ic2 = 0.0f;
            e->prev[c] = e->cur[c] = 0.0f;
        }
        e->d = e->d_target;
        e->drop_g = e->drop_to = 1.0f;
        e->flut = 0.0f;
    }
}
