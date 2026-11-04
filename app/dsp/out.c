/* SPDX-License-Identifier: MIT
 * Copyright (C) 2026 john-s */
/* The output's block-rate side (out.h) */
#include "out.h"

void out_init(out_t *o)
{
    o->lim_g = 1.0f;
    o->lim_rel = tf_tau_a(0.08f, TF_FS);
    o->gain = 0.5f;
    o->peak = 0.0f;
}

void out_sanitize(out_t *o)
{
    if (!tf_finite(o->lim_g) || o->lim_g <= 0.0f)
        o->lim_g = 1.0f;
}
