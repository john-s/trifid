/* SPDX-License-Identifier: MIT
 * Copyright (C) 2026 john-s */
/* The output's block-rate side (out.h) */
#include "out.h"

void out_grit_map(float grit, grit_map_t *m)
{
    float g = fm_clampf(grit, 0.0f, 1.0f);
    m->rot_grit = g;
    m->echo_grit = g * 0.8f;
    m->wear_scale = g;                          /* Wear does nothing at Grit 0 */
    m->fold = g <= 0.3f ? 0.0f : (g - 0.3f) * (1.0f / 0.7f);   /* the fold comes in past a third */
}

void out_init(out_t *o)
{
    int c;
    for (c = 0; c < 2; c++)
        o->hp[c].ic1 = o->hp[c].ic2 = o->lp[c].ic1 = o->lp[c].ic2 = 0.0f;
    tf_svf_set(&o->hpc, 140.0f, 0.7071f, TF_FS);
    tf_svf_set(&o->lpc, 9000.0f, 0.7071f, TF_FS);
    o->lim_g = 1.0f;
    o->lim_rel = tf_tau_a(0.08f, TF_FS);
    o->gain = 0.5f;
    o->peak = 0.0f;
    o->mode = OUT_SPEAKER;
}

void out_set_mode(out_t *o, int mode) { o->mode = mode == OUT_LINE ? OUT_LINE : OUT_SPEAKER; }

void out_sanitize(out_t *o)
{
    int c;
    for (c = 0; c < 2; c++) {
        tf_svf_clean(&o->hp[c]);
        tf_svf_clean(&o->lp[c]);
        if (!tf_finite(o->hp[c].ic1 + o->hp[c].ic2 + o->lp[c].ic1 + o->lp[c].ic2))
            o->hp[c].ic1 = o->hp[c].ic2 = o->lp[c].ic1 = o->lp[c].ic2 = 0.0f;
    }
    if (!tf_finite(o->lim_g) || o->lim_g <= 0.0f)
        o->lim_g = 1.0f;
}
