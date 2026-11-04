/* SPDX-License-Identifier: MIT
 * Copyright (C) 2026 john-s */
/* Rot's block-rate side (rot.h) */
#include "rot.h"

/* the taps, at fs/4: primes, so their echoes never line up (91 .. 401 ms) */
const uint16_t rot_tap_at[ROT_TAPS] = {1009, 1787, 2861, 4421};

void rot_init(rot_t *r, int16_t *buf, uint32_t seed)
{
    r->buf = buf;
    r->rng = seed ? seed : 0x2545F491u;
    rot_reset(r);
}

void rot_reset(rot_t *r)
{
    uint32_t i;
    for (i = 0; i < ROT_LEN; i++)
        r->buf[i] = 0;
    for (i = 0; i < ROT_AP_MAX; i++)
        r->ap[i] = 0.0f;
    for (i = 0; i < ROT_TAPS; i++)
        r->jit[i] = r->jit_to[i] = 0.0f;
    r->w = r->ph = r->ap_w = 0;
    r->ap_len = 20;
    r->ap_g = 0.5f;
    r->fb = 0.0f;
    r->wet_g = 1.0f;
    r->mix = 0.0f;
    r->jit_amt = 0.0f;
    r->jit_t = 0;
    r->qstep = 1;
    r->prev = r->cur = 0.0f;
    r->dec.a1.ic1 = r->dec.a1.ic2 = r->dec.a2.ic1 = r->dec.a2.ic2 = r->dec.a3.ic1 = r->dec.a3.ic2 = 0.0f;
    r->dec.post.ic1 = r->dec.post.ic2 = r->dec.op = 0.0f;
    tf_dec_set(&r->dc, 0.0f);
    r->active = 0;
}

void rot_set(rot_t *r, const rot_params_t *k)
{
    float grit = fm_clampf(k->grit, 0.0f, 1.0f), sm = fm_clampf(k->smear, 0.0f, 1.0f), j = fm_clampf(k->jitter, 0.0f, 1.0f);
    int i;
    uint32_t len;
    r->mix = fm_clampf(k->mix, 0.0f, 1.0f);
    r->fb = 0.95f * fm_clampf(k->decay, 0.0f, 1.0f);
    r->wet_g = 1.0f - r->fb;                    /* the wet path's peak gain held at 1 */
    len = 20u + (uint32_t)(sm * 60.0f + 0.5f);
    if (len != r->ap_len) {
        r->ap_len = len;
        if (r->ap_w >= len)
            r->ap_w = 0;
    }
    r->ap_g = 0.5f + 0.25f * sm;
    r->jit_amt = j > 0.0f ? 1.0f + 2.0f * j : 0.0f;
    /* coarser steps with Grit: 16 bits up to Grit 0.25, then down to 6 at 1 */
    r->qstep = grit <= 0.25f ? 1 : (int32_t)1 << (int32_t)((grit - 0.25f) * (10.0f / 0.75f) + 0.5f);
    tf_dec_set(&r->dc, grit);
    if (r->mix > 0.0f && !r->active) {          /* waking: whatever was in the line is from long ago */
        uint32_t n;
        for (n = 0; n < ROT_LEN; n++)
            r->buf[n] = 0;
        r->prev = r->cur = 0.0f;
    }
    r->active = r->mix > 0.0f;
    /* the jitter: each tap drifts to a new random place every ~0.7 s, smoothed over ~0.5 s (a block is 2.9 ms) */
    if (++r->jit_t >= 240u)
        r->jit_t = 0;
    for (i = 0; i < ROT_TAPS; i++) {
        if (r->jit_t == (uint32_t)i * 60u)
            r->jit_to[i] = r->jit_amt * 0.5f * (1.0f + tf_noise(&r->rng));
        r->jit[i] += 0.0058f * (r->jit_to[i] - r->jit[i]);
    }
}

void rot_sanitize(rot_t *r)
{
    uint32_t i;
    int bad = !tf_finite(r->prev + r->cur);
    for (i = 0; i < r->ap_len; i++) {
        bad |= !tf_finite(r->ap[i]);
        r->ap[i] = tf_clean(r->ap[i]);
    }
    tf_dec_clean(&r->dec);
    if (bad) {
        for (i = 0; i < ROT_AP_MAX; i++)
            r->ap[i] = 0.0f;
        r->prev = r->cur = 0.0f;
    }
}
