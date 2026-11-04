/* SPDX-License-Identifier: MIT
 * Copyright (C) 2026 john-s */
/* The plate's impulse response (wet only, unscaled, no modulation, in float: the exact linear system) at decay 0, 0.1 .. 1, to measure its peak gain over
 * frequency (verb.c's table): verb_gain OUTDIR writes OUTDIR/irD.f32 (left, x 16), D = 0..10; its RMS sum is the energy gain */
#include <stdio.h>
#include <stdlib.h>
#define VERB_GAIN_TABLE 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1
#define VERB_FLOAT_TEST
#include "verb.c"
static float buf[VERB_LEN];
int main(int argc, char **argv)
{
    int d;
    for (d = 0; d <= 10; d++) {
        verb_t v;
        verb_params_t k = {1.0f, d / 10.0f, 0.0f, 0.5f};
        uint32_t i, n = 1u << 21;
        char path[512];
        FILE *f;
        snprintf(path, sizeof path, "%s/ir%d.f32", argc > 1 ? argv[1] : ".", d);
        f = fopen(path, "wb");
        verb_init(&v, buf);
        for (i = 0; i < n; i++) {
            float l = i == 0 ? 0.25f : 0.0f, r = l;   /* (a quarter: the tank rings up past it) */
            if ((i & 127u) == 0) {
                verb_set(&v, &k);
                v.wet_g = 1.0f;
            }
            verb_tick(&v, &l, &r);
            l *= 4.0f;
            fwrite(&l, 4, 1, f);
        }
        fclose(f);
    }
    return 0;
}
