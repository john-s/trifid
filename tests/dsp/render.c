/* SPDX-License-Identifier: MIT
 * Copyright (C) 2026 john-s */
/* A/B renders of the engine, natively: a WAV (16-bit stereo, 44,118 Hz) from settings on the command line.
 *   render OUT.wav [SECONDS] [name=value ...]
 * names: tf_params_t's (fb_db, note, fine, glide, body, drive, lpf, hpf, rot_mix, rot_decay, rot_smear, rot_jitter,
 * echo_send, echo_ret, echo_time, echo_fb, echo_wear, grit, seed_db, feed, out_mode 0 speaker / 1 line, starve,
 * freeze), plus margin=DB (the feedback set this far from the ignition threshold) and imp=AMP (a strike at the start).
 * Built by tests/run_tests.sh into app/build/tests/render. */
#include "tf_test.h"

static void put32(FILE *f, uint32_t v) { fputc(v & 255, f); fputc(v >> 8 & 255, f); fputc(v >> 16 & 255, f); fputc(v >> 24, f); }
static void put16(FILE *f, uint32_t v) { fputc(v & 255, f); fputc(v >> 8 & 255, f); }

int main(int argc, char **argv)
{
    tf_params_t p;
    double secs = argc > 2 && !strchr(argv[2], '=') ? atof(argv[2]) : 10.0, margin = 1e9;
    uint32_t blocks, i, k, frames;
    float l[TF_BLOCK], r[TF_BLOCK];
    FILE *f;
    int a;
    if (argc < 2) {
        fprintf(stderr, "render OUT.wav [SECONDS] [name=value ...]\n");
        return 2;
    }
    tb_init();
    tf_params_default(&p);
    for (a = 2; a < argc; a++) {
        char *eq = strchr(argv[a], '=');
        float v;
        if (!eq)
            continue;
        *eq = 0;
        v = (float)atof(eq + 1);
#define F(n) else if (!strcmp(argv[a], #n)) p.n = v;
        if (!strcmp(argv[a], "margin")) margin = v;
        else if (!strcmp(argv[a], "imp")) { p.imp_seq++; p.imp_amp = v; }
        else if (!strcmp(argv[a], "out_mode")) p.out_mode = (int32_t)v;
        else if (!strcmp(argv[a], "starve")) p.starve = (int32_t)v;
        else if (!strcmp(argv[a], "freeze")) p.freeze = (int32_t)v;
        F(fb_db) F(note) F(fine) F(glide) F(body) F(drive) F(lpf) F(hpf) F(rot_mix) F(rot_decay) F(rot_smear)
        F(rot_jitter) F(echo_send) F(echo_ret) F(echo_time) F(echo_fb) F(echo_wear) F(grit) F(seed_db) F(feed)
        else { fprintf(stderr, "render: no setting %s\n", argv[a]); return 2; }
    }
    if (margin < 1e8)
        p.fb_db = tf_engine_threshold_db(&tb_e, &p) + (float)margin;
    printf("threshold %.2f dB, feedback %.2f dB\n", tf_engine_threshold_db(&tb_e, &p), p.fb_db);
    blocks = (uint32_t)(secs * TF_FS / TF_BLOCK);
    frames = blocks * TF_BLOCK;
    if (!(f = fopen(argv[1], "wb")))
        return 1;
    fwrite("RIFF", 1, 4, f); put32(f, 36 + frames * 4); fwrite("WAVEfmt ", 1, 8, f); put32(f, 16); put16(f, 1);
    put16(f, 2); put32(f, 44118); put32(f, 44118 * 4); put16(f, 4); put16(f, 16); fwrite("data", 1, 4, f);
    put32(f, frames * 4);
    for (i = 0; i < blocks; i++) {
        tf_engine_render(&tb_e, &p, l, r, TF_BLOCK, 0);
        for (k = 0; k < TF_BLOCK; k++) {
            put16(f, (uint32_t)(int16_t)(l[k] * 32767.0f));
            put16(f, (uint32_t)(int16_t)(r[k] * 32767.0f));
        }
    }
    fclose(f);
    tf_engine_meter(&tb_e, &p);
    printf("%s: %.1f s, margin now %.2f dB\n", argv[1], secs, tb_e.margin_db);
    return 0;
}
