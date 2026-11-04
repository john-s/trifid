/* SPDX-License-Identifier: MIT
 * Copyright (C) 2026 john-s */
/* Trifid's DSP modules, natively (no platform): tests/run_tests.sh builds and runs this.
 *   ignition   the loop is silent below its threshold and self-excites above it
 *   limiter    nothing ever leaves past the ceiling, whatever goes in
 *   starve     Starve cuts the feedback gain to the floor, fast but smoothly, and the loop dies
 *   grit0      Grit at 0 is clean: no fold, no Wear, 16-bit dithered storage, the fs/4 paths anti-aliased
 *   numerics   no NaN, no denormals: extreme settings, a long silence, a NaN pushed in
 *   pitch      the loop plays its note, MIDI 16..72
 *   echo       Freeze holds the tape as a loop */
#include "tf_test.h"
#include <float.h>

static tf_params_t base_params(float note)
{
    tf_params_t p;
    tf_params_default(&p);
    p.note = note;
    p.glide = 0.0f;
    p.out_mode = OUT_LINE;
    return p;
}

/* ------------------------------------------------------------- ignition --- */
static void test_ignition(void)
{
    static const float notes[] = {16, 28, 40, 52, 64, 72};
    unsigned i;
    printf("ignition\n");
    for (i = 0; i < sizeof notes / sizeof notes[0]; i++) {
        tf_params_t p = base_params(notes[i]);
        float th;
        tb_run_t a, b;
        tb_init();
        th = tf_engine_threshold_db(&tb_e, &p);
        p.fb_db = th - 3.0f;
        a = tb_run(&p, 10, 2, 0, 0);
        b = tb_run(&p, 10, 2, 0, 0);
        CHECK(b.rms_db < -60.0 && b.rms_db < a.rms_db + 1.0,
              "MIDI %.0f, 3 dB below ignition (%.1f dB): %.1f then %.1f dBFS after 10 and 20 s (silent, not growing)",
              notes[i], th, a.rms_db, b.rms_db);
        tb_init();
        p.fb_db = th + (notes[i] < 22 ? 6.0f : 3.0f);
        a = tb_run(&p, 10, 1, 0, 0);
        CHECK(a.rms_db > -25.0, "MIDI %.0f, %.0f dB above: %.1f dBFS after 10 s (self-excited)", notes[i],
              p.fb_db - th, a.rms_db);
    }
    {   /* with Rot wet and the echo sent (no return): still silent below; the margin meter knows */
        tf_params_t p = base_params(40);
        tb_run_t a;
        tb_init();
        p.rot_mix = 0.7f;
        p.rot_decay = 0.9f;
        p.echo_send = 1.0f;
        p.echo_fb = 0.7f;                      /* (past 0.8, 1x, the echo rings on its own: by design) */
        p.fb_db = tf_engine_threshold_db(&tb_e, &p) - 3.0f;
        a = tb_run(&p, 20, 2, 0, 0);
        CHECK(a.rms_db < -50.0, "Rot wet 0.7, decay 0.9, echo sent (feedback 0.9x), 3 dB below: %.1f dBFS", a.rms_db);
        CHECK(tb_e.margin_db < -2.9f && tb_e.margin_db > -3.1f, "the meter reads %.2f dB", tb_e.margin_db);
    }
}

/* -------------------------------------------------------------- limiter --- */
static void test_limiter(void)
{
    out_t o;
    uint32_t i, rng = 1, bad = 0;
    float worst = 0.0f;
    tf_params_t p = base_params(30);
    float l[TF_BLOCK], r[TF_BLOCK], ext[TF_BLOCK];
    printf("limiter\n");
    out_init(&o);
    for (i = 0; i < 2000000u; i++) {           /* anything at all into it, Speaker and Line */
        float a = tf_noise(&rng), b = tf_noise(&rng), x, y;
        uint32_t k = i % 7u;
        x = k == 0 ? a * 1e30f : k == 1 ? (a > 0 ? INFINITY : -INFINITY) : k == 2 ? NAN : a * (float)(i % 1000u);
        y = k == 3 ? NAN : b * 1e6f;
        if (i == 1000000u)
            out_set_mode(&o, OUT_SPEAKER);
        o.gain = 1.0f;
        out_tick(&o, &x, &y);
        if (!(fabsf(x) <= OUT_CEIL) || !(fabsf(y) <= OUT_CEIL))
            bad++;
        if (fabsf(x) > worst) worst = fabsf(x);
        if (fabsf(y) > worst) worst = fabsf(y);
        if ((i & 127u) == 0)
            out_sanitize(&o);
    }
    CHECK(bad == 0, "2,000,000 frames of noise, 1e30, infinities and NaN: none past %.4f (the most %.4f)", OUT_CEIL,
          worst);
    /* the whole engine flat out: every gain at its top, Grit full, impulses, an external feed of garbage */
    tb_init();
    p.fb_db = 12.0f;
    p.drive = 1.0f;
    p.grit = 1.0f;
    p.echo_send = 1.0f;
    p.echo_ret = 1.0f;
    p.echo_fb = 1.0f;
    p.echo_wear = 1.0f;
    p.rot_mix = 1.0f;
    p.rot_decay = 1.0f;
    p.feed = 1.0f;
    p.seed_db = -60.0f;
    bad = 0;
    worst = 0.0f;
    for (i = 0; i < 20000u; i++) {             /* 58 s */
        uint32_t k;
        for (k = 0; k < TF_BLOCK; k++)
            ext[k] = (i % 50u == 7u) ? tf_noise(&rng) * 1e9f : 0.0f;
        if (i % 100u == 0)
            p.imp_seq++, p.imp_amp = 1.0f;
        p.out_mode = (i / 2000u) & 1u ? OUT_SPEAKER : OUT_LINE;
        tf_engine_render(&tb_e, &p, l, r, TF_BLOCK, ext);
        for (k = 0; k < TF_BLOCK; k++) {
            if (!(fabsf(l[k]) <= OUT_CEIL) || !(fabsf(r[k]) <= OUT_CEIL))
                bad++;
            if (fabsf(l[k]) > worst) worst = fabsf(l[k]);
        }
    }
    CHECK(bad == 0, "58 s of everything at full (feedback +12 dB, return, Rot, Wear, 1e9 kicks): the most %.4f", worst);
}

/* --------------------------------------------------------------- starve --- */
static void test_starve(void)
{
    tf_params_t p = base_params(40);
    tb_run_t a, b, c;
    float g0, floor_g = powf(10.0f, PLANT_FB_LO_DB / 20.0f), lastg, maxstep = 0.0f, l[TF_BLOCK], r[TF_BLOCK];
    uint32_t i;
    printf("starve\n");
    tb_init();
    p.fb_db = tf_engine_threshold_db(&tb_e, &p) + 6.0f;
    a = tb_run(&p, 6, 1, 0, 0);
    g0 = tb_e.plant.g;
    p.starve = 1;
    lastg = tb_e.plant.g;
    for (i = 0; i < 12u; i++) {                /* 35 ms, watched block by block */
        tf_engine_render(&tb_e, &p, l, r, TF_BLOCK, 0);
        if (fabsf(tb_e.plant.g - lastg) > maxstep)
            maxstep = fabsf(tb_e.plant.g - lastg);
        lastg = tb_e.plant.g;
    }
    CHECK(tb_e.plant.g < floor_g * 1.5f, "feedback gain %.4f -> %.6f in 35 ms (the floor %.6f)", g0, tb_e.plant.g,
          floor_g);
    CHECK(maxstep < 0.6f * g0, "smoothed: no block takes more than %.0f %% of it at once", 100.0f * maxstep / g0);
    CHECK(tb_e.margin_db < -40.0f, "the meter: %.1f dB below ignition", tb_e.margin_db);
    b = tb_run(&p, 2, 0.5, 0, 0);
    CHECK(b.rms_db < a.rms_db - 40.0, "the loop dies: %.1f dBFS -> %.1f dBFS in 2 s", a.rms_db, b.rms_db);
    p.starve = 0;
    c = tb_run(&p, 8, 1, 0, 0);
    CHECK(c.rms_db > -25.0, "let go, it comes back: %.1f dBFS", c.rms_db);
}

/* ---------------------------------------------------------------- grit 0 --- */
/* a sine at f into Rot alone (wet only, no feedback) at this grit: the output's energy below 4 kHz (what folded
 * down), dB re the input's */
static double alias_db(float f, float grit)
{
    rot_t rt;
    rot_params_t k = {1.0f, 0.0f, 0.5f, 0.0f, 0.0f};
    uint32_t i, n = 44118u * 2u;
    double ph = 0, e_in = 0, e_lo = 0;
    tf_svf_coef_t c;
    tf_svf_t s1 = {0, 0}, s2 = {0, 0}, s3 = {0, 0};
    tf_svf_set(&c, 3500.0f, 0.7071f, TF_FS);  /* the measure: a 6-pole low-pass at 3.5 kHz on the output */
    rot_init(&rt, tb_rot, 7);
    k.grit = grit;
    for (i = 0; i < n; i++) {
        float x = 0.5f * sinf((float)ph), y, lo;
        ph += 2.0 * M_PI * f / TF_FS;
        if ((i & 127u) == 0)
            rot_set(&rt, &k);
        y = rot_tick(&rt, x);
        lo = tf_svf_lp(&s3, &c, tf_svf_lp(&s2, &c, tf_svf_lp(&s1, &c, y)));
        if (i > n / 4) {
            e_in += (double)x * x;
            e_lo += (double)lo * lo;
        }
    }
    return 10.0 * log10(e_lo / e_in + 1e-30);
}

static void test_grit0(void)
{
    tf_params_t p = base_params(40);
    grit_map_t g;
    uint32_t i, drops = 0, rng = 3;
    float l[TF_BLOCK], r[TF_BLOCK];
    double worst = -999;
    static const float tones[] = {8500, 10000, 12000, 15000, 19000};
    printf("grit0\n");
    out_grit_map(0.0f, &g);
    CHECK(g.fold == 0.0f && g.wear_scale == 0.0f && g.rot_grit == 0.0f && g.echo_grit == 0.0f,
          "Grit 0 maps to no fold, no Wear, clean decimation");
    tb_init();
    p.grit = 0.0f;
    p.echo_wear = 1.0f;                        /* the Wear knob up: Grit 0 still has none */
    p.echo_send = 1.0f;
    p.rot_mix = 0.5f;
    p.fb_db = tf_engine_threshold_db(&tb_e, &p) + 3.0f;
    for (i = 0; i < 3500u; i++) {              /* 10 s */
        tf_engine_render(&tb_e, &p, l, r, TF_BLOCK, 0);
        if (tb_e.echo.drop_g < 0.9999f)
            drops++;
    }
    CHECK(tb_e.plant.fold == 0.0f, "the plant's wavefold off");
    CHECK(tb_e.echo.hiss == 0.0f && tb_e.echo.qstep == 1 && drops == 0, "the echo: no hiss, 16-bit steps, no dropout "
          "in 10 s (Wear knob at full)");
    CHECK(tb_e.rot.qstep == 1, "Rot: 16-bit steps");
    for (i = 0; i < sizeof tones / sizeof tones[0]; i++) {
        double a0 = alias_db(tones[i], 0.0f);
        if (a0 > worst) worst = a0;
    }
    CHECK(worst < -40.0, "Rot at Grit 0: tones 8.5..19 kHz fold below 3.5 kHz at %.1f dB at most (anti-aliased)", worst);
    {
        double a1 = alias_db(10000.0f, 1.0f);
        CHECK(a1 > -20.0, "Rot at Grit 1: a 10 kHz tone folds down at %.1f dB (the grit is there when asked)", a1);
    }
    {   /* dither: a sine of a third of a step survives storage on average (rounding alone would make it 0) */
        double corr = 0, e = 0;
        for (i = 0; i < 200000u; i++) {
            float x = (1.0f / 3.0f) * (ROT_FS / 32767.0f) * sinf((float)i * 0.01f);
            int16_t q = tf_store16(x, 1.0f / ROT_FS, 1, &rng);
            corr += (double)q * x;
            e += (double)x * x;
        }
        CHECK(corr / e * (ROT_FS / 32767.0) > 0.9 && corr / e * (ROT_FS / 32767.0) < 1.1,
              "TPDF dither: a third-of-a-step sine comes back at %.2f of its level", corr / e * (ROT_FS / 32767.0));
    }
}

/* -------------------------------------------------------------- numerics --- */
static int subnormal(float x) { return x != 0.0f && fabsf(x) < FLT_MIN; }
static int states_ok(void)
{
    const plant_t *pl = &tb_e.plant;
    int c, bad = 0;
    uint32_t i;
    float st[64];
    unsigned n = 0, k;
    for (c = 0; c < 2; c++) {
        st[n++] = pl->s[c].damp_y; st[n++] = pl->s[c].dc_x1; st[n++] = pl->s[c].dc_y1;
        st[n++] = pl->lp[c].ic1; st[n++] = pl->lp[c].ic2; st[n++] = pl->hp[c].ic1; st[n++] = pl->hp[c].ic2;
        st[n++] = tb_e.echo.bps[c].ic1; st[n++] = tb_e.echo.bps[c].ic2;
        st[n++] = tb_e.echo.dec[c].a1.ic1; st[n++] = tb_e.echo.dec[c].a3.ic2; st[n++] = tb_e.echo.dec[c].post.ic1;
        st[n++] = tb_e.out.hp[c].ic1; st[n++] = tb_e.out.lp[c].ic2;
    }
    st[n++] = pl->g; st[n++] = pl->press; st[n++] = pl->imp_env; st[n++] = tb_e.rot.dec.a1.ic1;
    st[n++] = tb_e.rot.dec.op; st[n++] = tb_e.out.lim_g;
    for (k = 0; k < n; k++)
        if (!isfinite(st[k]) || subnormal(st[k]))
            bad++;
    for (c = 0; c < 2; c++) {
        for (i = 0; i < PLANT_STRING_LEN; i++)
            if (!isfinite(pl->s[c].buf[i]) || subnormal(pl->s[c].buf[i]))
                bad++;
        for (i = 0; i < PLANT_BODY_LEN; i++)
            if (!isfinite(pl->body[c][i]) || subnormal(pl->body[c][i]))
                bad++;
    }
    for (i = 0; i < ROT_AP_MAX; i++)
        if (!isfinite(tb_e.rot.ap[i]) || subnormal(tb_e.rot.ap[i]))
            bad++;
    return bad == 0;
}

static void test_numerics(void)
{
    tf_params_t p = base_params(72);
    tb_run_t a;
    float l[TF_BLOCK], r[TF_BLOCK], ext[TF_BLOCK];
    uint32_t i, k, nonfinite = 0;
    printf("numerics\n");
    /* a long silence: the floor of everything, the quietest seed, every filter decaying towards nothing */
    tb_init();
    p.fb_db = -60.0f;
    p.seed_db = -120.0f;
    p.rot_mix = 0.3f;
    p.echo_send = 1.0f;
    p.echo_fb = 0.2f;
    a = tb_run(&p, 60, 1, 0, 0);
    p.seed_db = -140.0f;
    p.rot_mix = 0.0f;
    p.echo_send = 0.0f;
    a = tb_run(&p, 60, 1, 0, 0);
    CHECK(a.finite && states_ok(), "2 minutes at the floor (seed -140 dBFS at the end): finite, no denormal state");
    /* extreme settings, swept */
    tb_init();
    for (i = 0; i < 6000u && nonfinite == 0; i++) {
        float t = (float)i / 6000.0f;
        p.fb_db = -60.0f + 72.0f * (0.5f + 0.5f * sinf(t * 40.0f));
        p.note = 16.0f + 56.0f * (0.5f + 0.5f * sinf(t * 23.0f));
        p.lpf = 0.5f + 0.5f * sinf(t * 31.0f);
        p.hpf = 0.5f + 0.5f * sinf(t * 17.0f);
        p.drive = 0.5f + 0.5f * sinf(t * 13.0f);
        p.grit = 0.5f + 0.5f * sinf(t * 7.0f);
        p.body = 0.5f + 0.5f * sinf(t * 11.0f);
        p.rot_mix = 1.0f; p.rot_decay = 1.0f; p.rot_smear = t; p.rot_jitter = 1.0f - t;
        p.echo_send = 1.0f; p.echo_ret = 1.0f; p.echo_fb = 1.0f; p.echo_wear = 1.0f;
        p.echo_time = 0.5f + 0.5f * sinf(t * 19.0f);
        p.freeze = (i / 700u) & 1u;
        tf_engine_render(&tb_e, &p, l, r, TF_BLOCK, 0);
        for (k = 0; k < TF_BLOCK; k++)
            if (!isfinite(l[k]) || !isfinite(r[k]))
                nonfinite++;
    }
    CHECK(nonfinite == 0 && states_ok(), "17 s of every control swept at once, flat out: finite, no denormal state");
    /* a NaN pushed in through the external feed: the output stays finite, the engine starts again */
    tb_init();
    p = base_params(40);
    p.fb_db = tf_engine_threshold_db(&tb_e, &p) + 6.0f;
    p.echo_send = 0.5f;
    p.echo_ret = 0.3f;
    tb_run(&p, 3, 1, 0, 0);
    for (k = 0; k < TF_BLOCK; k++)
        ext[k] = k == 5 ? NAN : k == 9 ? INFINITY : 0.0f;
    nonfinite = 0;
    tf_engine_render(&tb_e, &p, l, r, TF_BLOCK, ext);
    for (k = 0; k < TF_BLOCK; k++)
        if (!isfinite(l[k]) || !isfinite(r[k]))
            nonfinite++;
    for (i = 0; i < 4u; i++)
        tf_engine_render(&tb_e, &p, l, r, TF_BLOCK, 0);
    CHECK(nonfinite == 0 && states_ok(), "a NaN and an infinity fed in: the output finite, the states clean a block on");
    a = tb_run(&p, 8, 1, 0, 0);
    CHECK(a.finite && a.rms_db > -25.0, "and it ignites again: %.1f dBFS", a.rms_db);
}

/* ----------------------------------------------------------------- pitch --- */
static void test_pitch(void)
{
    static double keep[88236];
    float note, worst = 0.0f;
    int fails = 0;
    printf("pitch\n");
    for (note = 16.0f; note <= 72.0f; note += 4.0f) {
        tf_params_t p = base_params(note);
        double f, f0 = 440.0 * pow(2.0, (note - 69.0) / 12.0), cents;
        tb_init();
        p.fb_db = tf_engine_threshold_db(&tb_e, &p) + (note < 22.0f ? 6.0f : 3.0f);
        tb_run(&p, 8, 0, 0, 0);
        tb_run(&p, 2, 2, keep, 88236);
        f = tb_pitch(keep, 88236, 15, 1200);
        cents = 1200.0 * log2(f / f0);
        if (fabs(cents) > worst) worst = (float)fabs(cents);
        if (fabs(cents) > 5.0) {
            fails++;
            printf("       MIDI %.0f: %.2f Hz for %.2f (%.1f cents)\n", note, f, f0, cents);
        }
    }
    CHECK(fails == 0, "MIDI 16..72 every 4 semitones, self-excited: within %.2f cents of the note (5 allowed)", worst);
    {   /* and with a glide and the body at its ends: the pitch follows */
        tf_params_t p = base_params(30);
        double f;
        tb_init();
        p.fb_db = tf_engine_threshold_db(&tb_e, &p) + 4.0f;
        p.body = 0.0f;
        tb_run(&p, 6, 0, 0, 0);
        p.note = 60.0f;
        p.glide = 0.5f;
        p.body = 1.0f;
        tb_run(&p, 6, 0, 0, 0);
        tb_run(&p, 2, 2, keep, 88236);
        f = tb_pitch(keep, 88236, 15, 1200);
        CHECK(fabs(1200.0 * log2(f / 261.626)) < 5.0, "a glide from MIDI 30 to 60, the body from 1 to 100 ms: %.2f Hz "
              "(261.63)", f);
    }
}

/* ------------------------------------------------------------------ echo --- */
static void test_echo(void)
{
    tf_params_t p = base_params(52);
    tb_run_t a, b, c;
    printf("echo\n");
    tb_init();
    p.fb_db = tf_engine_threshold_db(&tb_e, &p) + 3.0f;
    p.echo_send = 1.0f;
    p.echo_fb = 0.5f;
    p.echo_time = 0.4f;
    tb_run(&p, 5, 0, 0, 0);
    p.freeze = 1;
    p.starve = 1;                              /* the loop gone: what plays is the frozen tape */
    tb_run(&p, 1, 0, 0, 0);
    a = tb_run(&p, 2, 2, 0, 0);
    p.echo_send = 0.0f;
    b = tb_run(&p, 10, 2, 0, 0);
    CHECK(a.rms_db > -40.0 && fabs(b.rms_db - a.rms_db) < 1.0, "frozen: the tape plays on as a loop (%.1f then %.1f "
          "dBFS 10 s later, the loop starved)", a.rms_db, b.rms_db);
    p.freeze = 0;
    c = tb_run(&p, 6, 2, 0, 0);
    CHECK(c.rms_db < b.rms_db - 20.0, "let go (feedback 0.5, nothing sent): it fades, %.1f dBFS", c.rms_db);
}

int main(int argc, char **argv)
{
    const char *only = argc > 1 ? argv[1] : 0;
#define RUN(name) if (!only || !strcmp(only, #name)) test_##name()
    RUN(ignition);
    RUN(limiter);
    RUN(starve);
    RUN(grit0);
    RUN(numerics);
    RUN(pitch);
    RUN(echo);
    printf("dsp: %d checks, %d failed\n", tb_checks, tb_fails);
    return tb_fails != 0;
}
