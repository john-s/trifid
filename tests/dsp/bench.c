/* SPDX-License-Identifier: MIT
 * Copyright (C) 2026 john-s */
/* The engine's cost on this machine, ns a frame, with everything running (the reverb, Glitter, the echo, Wear): a
 * relative figure for keeping the inner loop lean. The FM-1's figure is plat_cpu_pct on the device. */
#include "tf_test.h"
#include <time.h>

int main(void)
{
    tf_params_t p;
    float l[TF_BLOCK], r[TF_BLOCK];
    struct timespec a, b;
    uint32_t i, n = 20000;
    double ns;
    tb_init();
    tf_params_default(&p);
    p.verb_mix = 0.5f;
    p.verb_depth = 0.5f;
    p.echo_send = 0.5f;
    p.echo_ret = 0.2f;
    p.glit_wet = 0.7f;
    p.glit_amount = 0.5f;
    p.echo_wear = 0.5f;
    p.fb_db = tf_engine_threshold_db(&tb_e, &p) + 3.0f;
    for (i = 0; i < 2000; i++)
        tf_engine_render(&tb_e, &p, l, r, TF_BLOCK, 0);
    clock_gettime(CLOCK_MONOTONIC, &a);
    for (i = 0; i < n; i++)
        tf_engine_render(&tb_e, &p, l, r, TF_BLOCK, 0);
    clock_gettime(CLOCK_MONOTONIC, &b);
    ns = ((b.tv_sec - a.tv_sec) * 1e9 + (b.tv_nsec - a.tv_nsec)) / ((double)n * TF_BLOCK);
    printf("engine: %.1f ns a frame on this machine (%.2f %% of real time at 44,118 Hz)\n", ns, ns * 44118e-9 * 100);
    return 0;
}
