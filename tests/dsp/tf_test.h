/* SPDX-License-Identifier: MIT
 * Copyright (C) 2026 john-s */
/* the DSP tests' shared parts: the engine's buffers (static here; the FM-1 puts them in the pool), a block runner,
 * levels and pitch */
#pragma once
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "engine.h"

static float tb_str[2][PLANT_STRING_LEN], tb_body[2][PLANT_BODY_LEN];
static int16_t tb_rot[ROT_LEN], tb_echo[2][ECHO_LEN];
static tf_engine_t tb_e;
static int tb_fails, tb_checks;

#define CHECK(cond, ...) do { tb_checks++; if (!(cond)) { tb_fails++; printf("  FAIL "); printf(__VA_ARGS__); printf("\n"); } \
                              else { printf("  ok   "); printf(__VA_ARGS__); printf("\n"); } } while (0)

static void tb_init(void)
{
    tf_buffers_t b = {{tb_str[0], tb_str[1]}, {tb_body[0], tb_body[1]}, tb_rot, {tb_echo[0], tb_echo[1]}};
    tf_engine_init(&tb_e, &b);
}

/* run s seconds; the output's RMS and peak over the last `tail` seconds (dBFS), and every sample finite */
typedef struct { double rms_db, peak, peak_db; int finite; double *keep; uint32_t kept; } tb_run_t;
static tb_run_t tb_run(const tf_params_t *p, double s, double tail, double *keep, uint32_t keep_max)
{
    tb_run_t o = {0, 0, 0, 1, keep, 0};
    uint32_t blocks = (uint32_t)(s * TF_FS / TF_BLOCK), tb = (uint32_t)(tail * TF_FS / TF_BLOCK), i, k;
    double sum = 0;
    uint64_t cnt = 0;
    float l[TF_BLOCK], r[TF_BLOCK];
    for (i = 0; i < blocks; i++) {
        tf_engine_render(&tb_e, p, l, r, TF_BLOCK, 0);
        for (k = 0; k < TF_BLOCK; k++) {
            if (!isfinite(l[k]) || !isfinite(r[k]))
                o.finite = 0;
            if (i >= blocks - tb) {
                sum += (double)l[k] * l[k] + (double)r[k] * r[k];
                cnt += 2;
                if (fabs(l[k]) > o.peak) o.peak = fabs(l[k]);
                if (fabs(r[k]) > o.peak) o.peak = fabs(r[k]);
                if (keep && o.kept < keep_max)
                    keep[o.kept++] = l[k];
            }
        }
    }
    o.rms_db = 10.0 * log10(sum / (cnt ? cnt : 1) + 1e-30);
    o.peak_db = 20.0 * log10(o.peak + 1e-30);
    return o;
}

/* the fundamental of x (n samples) by normalised autocorrelation between periods lo..hi samples, refined by a
 * parabola: Hz */
static double tb_pitch(const double *x, uint32_t n, double f_lo, double f_hi)
{
    uint32_t lo = (uint32_t)(TF_FS / f_hi), hi = (uint32_t)(TF_FS / f_lo) + 2, t, i, best = lo;
    static double ac[4096];
    double bv = -1e30, e0 = 0;
    for (i = 0; i < n - hi; i++) e0 += x[i] * x[i];
    for (t = lo; t <= hi && t < 4096; t++) {
        double s = 0, e1 = 0;
        for (i = 0; i < n - hi; i++) { s += x[i] * x[i + t]; e1 += x[i + t] * x[i + t]; }
        ac[t] = s / sqrt(e0 * e1 + 1e-30);
    }
    /* the first peak that reaches 90 % of the best (not a multiple of the period) */
    for (t = lo + 1; t < hi && t < 4095; t++) if (ac[t] > bv) bv = ac[t];
    for (t = lo + 1; t < hi && t < 4095; t++)
        if (ac[t] >= 0.9 * bv && ac[t] >= ac[t - 1] && ac[t] >= ac[t + 1]) { best = t; break; }
    {
        double a = ac[best - 1], b = ac[best], c = ac[best + 1], d = a - 2 * b + c;
        double off = fabs(d) > 1e-12 ? 0.5 * (a - c) / d : 0.0;
        return TF_FS / ((double)best + off);
    }
}
