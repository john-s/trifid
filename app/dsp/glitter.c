/* SPDX-License-Identifier: MIT
 * Copyright (c) 2016 airwindows (Chris Johnson)
 * Copyright (C) 2026 john-s: the port to C, single precision
 *
 * BitGlitter, from airwindows (https://github.com/airwindows/airwindows, plugins/LinuxVST/src/BitGlitter), under the
 * MIT License:
 *
 *   Copyright (c) 2018 Chris Johnson
 *
 *   Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated
 *   documentation files (the "Software"), to deal in the Software without restriction, including without limitation
 *   the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and
 *   to permit persons to whom the Software is furnished to do so, subject to the following conditions:
 *
 *   The above copyright notice and this permission notice shall be included in all copies or substantial portions of
 *   the Software.
 *
 *   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
 *   THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 *   AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
 *   CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 *   IN THE SOFTWARE.
 *
 * Changes from the original: single precision (the FM-1 has no double FPU); the steps' loops of subtractions written
 * as a ceiling; the 32-bit float dither at the end and the denormal guard at the start left out (the engine flushes
 * its states each block); the reducers' rates from the FM-1's 44,118 Hz. */
#include "glitter.h"

void glitter_init(glitter_t *g)
{
    int c;
    for (c = 0; c < 2; c++)
        g->half[c] = g->last_in[c] = g->held_a[c] = g->held_b[c] = g->pos_a[c] = g->pos_b[c] = g->last_out[c] = 0.0f;
    g->ingain = g->outgain = 1.0f;
    g->wet = 0.0f;
    g->rate_a = g->rate_b = 0.0f;
}

void glitter_set(glitter_t *g, const glitter_params_t *k)
{
    float f = 1.0f + fm_clampf(k->glitter, 0.0f, 1.0f), divvy;
    f = f * f * f * f * f * f * f + 2.0f;                       /* (1 + Glitter)^7 + 2 */
    divvy = fm_floorf(f * (TF_FS / 44100.0f));                  /* (int) in BitGlitter */
    if (divvy < 1.0f)
        divvy = 1.0f;
    g->rate_a = 1.0f / divvy;
    g->rate_b = 1.61803398875f / divvy;
    g->ingain = fm_powf(10.0f, (fm_clampf(k->input, 0.0f, 1.0f) * 36.0f - 18.0f) / 14.0f);
    g->outgain = fm_powf(10.0f, (fm_clampf(k->output, 0.0f, 1.0f) * 36.0f - 18.0f) / 14.0f);
    g->wet = fm_clampf(k->wet, 0.0f, 1.0f);
}

void glitter_sanitize(glitter_t *g)
{
    int c;
    for (c = 0; c < 2; c++) {
        g->last_in[c] = tf_clean(g->last_in[c]);
        g->held_a[c] = tf_clean(g->held_a[c]);
        g->held_b[c] = tf_clean(g->held_b[c]);
        g->last_out[c] = tf_clean(g->last_out[c]);
        if (!tf_finite(g->pos_a[c]) || !tf_finite(g->pos_b[c]))
            g->pos_a[c] = g->pos_b[c] = 0.0f;
    }
}
