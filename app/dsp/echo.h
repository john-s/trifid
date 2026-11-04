/* SPDX-License-Identifier: MIT
 * Copyright (C) 2026 john-s */
/* The tape echo, after the loop: stereo, int16 at fs/4, 4 s (44,118 samples a channel: 176,472 bytes in the pool).
 *
 *   in x Send -> decimate -> + -> [tape] -> read (wow and flutter: the pointer drifts) -> dropouts -> up to fs -> out
 *                            ^                    |
 *                            +-- soft clip <- x Feedback (0..1.25: past 1x, the clip holds it) <- 800 Hz bandpass
 *
 * Wear (the knob times Grit: none at Grit 0) adds hiss to what is written, coarser steps (bit loss) and dropouts on
 * the read. Freeze stops the writing from the input: the tape copies itself at a whole-sample delay, so the loop
 * plays on unchanged (no requantization, no filter) until it is let go. The echo's output also goes back into the
 * plant's feed (the Return, plant.h). */
#pragma once
#include "dsp_util.h"

#define ECHO_LEN 44118u                 /* 4 s at fs/4 */
#define ECHO_FS 2.0f                    /* full scale of the tape */
#define ECHO_MIN_S 0.02f
#define ECHO_MAX_S 3.99f

typedef struct {
    float send;                         /* 0..1 */
    float time_s;                       /* 0.02 .. 3.99 */
    float feedback;                     /* 0..1.25 */
    float wear;                         /* 0..1, already scaled by Grit */
    float grit;                         /* the decimation's cleanliness */
    int freeze;
} echo_params_t;

typedef struct {
    int16_t *buf[2];
    uint32_t w, ph;
    float d, d_target, d_a;             /* the delay, fs/4 samples: it glides (the tape speeds up, slows down) */
    float wow_ph, wow_inc, flut, flut_to;
    float send, fb, hiss;
    tf_svf_coef_t bp;
    tf_svf_t bps[2];
    tf_dec_coef_t dc;
    tf_dec_t dec[2];
    float prev[2], cur[2];
    float drop_g, drop_to, drop_rate;   /* the dropouts: the gain now, where it goes, how often */
    uint32_t drop_left;
    int32_t qstep;
    int frozen;
    uint32_t frz_d;                     /* the frozen loop's length, samples */
    uint32_t rng;
} echo_t;

void echo_init(echo_t *e, int16_t *buf_l, int16_t *buf_r, uint32_t seed);
void echo_reset(echo_t *e);
void echo_set(echo_t *e, const echo_params_t *k);   /* once a block */
void echo_sanitize(echo_t *e);

static inline float echo_read(const int16_t *b, uint32_t w, float d)
{
    int32_t di = (int32_t)d, i0 = (int32_t)w - di, i1;
    float f = d - (float)di;
    if (i0 < 0)
        i0 += (int32_t)ECHO_LEN;
    i1 = i0 == 0 ? (int32_t)ECHO_LEN - 1 : i0 - 1;
    return ((float)b[i0] + f * (float)(b[i1] - b[i0])) * (ECHO_FS / 32767.0f);
}

static inline void echo_core(echo_t *e, float in_l, float in_r)
{
    int c;
    float d, lp, bp, hp;
    if (e->frozen) {                    /* the tape copies itself: the loop plays on as it was */
        uint32_t r = e->w >= e->frz_d ? e->w - e->frz_d : e->w + ECHO_LEN - e->frz_d;
        for (c = 0; c < 2; c++) {
            e->buf[c][e->w] = e->buf[c][r];
            e->prev[c] = e->cur[c];
            e->cur[c] = (float)e->buf[c][r] * (ECHO_FS / 32767.0f) * e->drop_g;
        }
    } else {
        e->d += e->d_a * (e->d_target - e->d);
        e->wow_ph += e->wow_inc;
        if (e->wow_ph >= 1.0f)
            e->wow_ph -= 1.0f;
        e->flut += 0.004f * (e->flut_to - e->flut);
        d = e->d * (1.0f + 0.0015f * fm_sin_turns(e->wow_ph) + 0.0006f * e->flut);
        d = fm_clampf(d, 2.0f, (float)ECHO_LEN - 3.0f);
        for (c = 0; c < 2; c++) {
            float rd = echo_read(e->buf[c], e->w, d), x;
            tf_svf_tick(&e->bps[c], &e->bp, rd, &lp, &bp, &hp);
            x = tf_softclip(bp * e->bp.k * e->fb) + (c ? in_r : in_l) * e->send;
            if (e->hiss > 0.0f)
                x += e->hiss * tf_noise(&e->rng);
            e->buf[c][e->w] = tf_store16(x, 1.0f / ECHO_FS, e->qstep, &e->rng);
            e->prev[c] = e->cur[c];
            e->cur[c] = rd * e->drop_g;
        }
    }
    e->w = e->w + 1 == ECHO_LEN ? 0 : e->w + 1;
    e->drop_g += 0.01f * (e->drop_to - e->drop_g);
}

/* every frame: the loop's output in, the echo out (the wet signal alone) */
static inline void echo_tick(echo_t *e, float in_l, float in_r, float *out_l, float *out_r)
{
    float a = tf_dec_in(&e->dec[0], &e->dc, in_l), b = tf_dec_in(&e->dec[1], &e->dc, in_r);
    if (++e->ph == 4u) {
        e->ph = 0;
        echo_core(e, a, b);
    }
    *out_l = tf_dec_out(&e->dec[0], &e->dc, e->prev[0], e->cur[0], e->ph);
    *out_r = tf_dec_out(&e->dec[1], &e->dc, e->prev[1], e->cur[1], e->ph);
}
