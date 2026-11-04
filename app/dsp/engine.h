/* SPDX-License-Identifier: MIT
 * Copyright (C) 2026 john-s */
/* Trifid's engine: the plant, Rot, the echo and the output, chained per sample; the controls once a block.
 *
 *   plant_tick_a -> Rot (crossfade) -> plant_tick_b (gain, body) ... the loop
 *                                  \-> x makeup -> + echo (send in, wet out; its output back to the feed) -> out
 *
 * The UI (main loop) writes tf_params_t in knob units (0..1 for most) and reads back the meters; the render reads
 * the parameters once a block. Each field is a 32-bit word written whole, so no lock is needed. Events (an impulse)
 * go through a sequence number. No platform calls: tests/dsp and tools render it natively. */
#pragma once
#include "plant.h"
#include "rot.h"
#include "echo.h"
#include "out.h"

typedef struct {
    float fb_db;                        /* SELECT: -60 .. +12 */
    float note;                         /* MIDI, the string's target (the key or the drone pitch) */
    float fine;                         /* cents, -100 .. +100 */
    float glide;                        /* 0..1: 0 .. 2 s */
    float body, drive, lpf, hpf;        /* HOME, 0..1 */
    float rot_mix, rot_decay, rot_smear, rot_jitter;   /* FX, 0..1 */
    float echo_send, echo_ret, echo_time, echo_fb, echo_wear;   /* SEQ, 0..1 */
    float time_mul;                     /* ENV / LFO held: 0.5, 2; else 1 */
    float grit;                         /* PRESETS, 0..1 */
    float seed_db;                      /* GLO: -120 .. -60 */
    float feed;                         /* the key's feed level, 0..1 (pressure or the held velocity) */
    int32_t starve, freeze;
    float imp_amp;                      /* (before imp_seq: a word-by-word copy publishes the level first) */
    uint32_t imp_seq;                   /* + 1 for an impulse at imp_amp */
} tf_params_t;

#define TF_SCOPE_N 240u
typedef struct {
    plant_t plant;
    rot_t rot;
    echo_t echo;
    out_t out;
    float note_now;                     /* the glide's position */
    float ret_l, ret_r;                 /* the echo's last output: the next frame's return */
    uint32_t imp_seen;
    int primed;
    float c_note, c_lpf, c_hpf, c_peak;  /* what the loop's peak gain was last worked out for */
    /* the meters, for the UI */
    float margin_db;                    /* how far above (+) or below ignition the loop runs now (tf_engine_meter) */
    volatile float peak;                /* the output's peak this block (0..OUT_CEIL) */
    int8_t scope[TF_SCOPE_N];           /* the left output, a point every 8 frames */
    volatile uint32_t scope_w;
    uint32_t scope_ph;
} tf_engine_t;

/* the knobs in physical units, for the engine and the screen alike */
static inline float tf_lpf_hz(float k) { return 100.0f * fm_exp2f(fm_clampf(k, 0.0f, 1.0f) * 7.4919f); }   /* 100 .. 18k */
static inline float tf_hpf_hz(float k) { return 10.0f * fm_exp2f(fm_clampf(k, 0.0f, 1.0f) * 8.6439f); }    /* 10 .. 4k */
static inline float tf_body_ms(float k) { return fm_exp2f(fm_clampf(k, 0.0f, 1.0f) * 6.643856f); }        /* 1 .. 100 */
static inline float tf_echo_s(float k) { return ECHO_MIN_S * fm_exp2f(fm_clampf(k, 0.0f, 1.0f) * 7.6403f); }  /* 0.02 .. 3.99 */
static inline float tf_echo_fb(float k) { return 1.25f * fm_clampf(k, 0.0f, 1.0f); }
static inline float tf_glide_s(float k) { float c = fm_clampf(k, 0.0f, 1.0f); return 2.0f * c * c; }

/* buffers: the app's (the pool on the FM-1); sizes PLANT_STRING_LEN, PLANT_BODY_LEN, ROT_LEN, ECHO_LEN */
typedef struct {
    float *str[2], *body[2];
    int16_t *rot, *echo[2];
} tf_buffers_t;

void tf_engine_init(tf_engine_t *e, const tf_buffers_t *b);
void tf_params_default(tf_params_t *p);
/* one block (n <= TF_BLOCK frames, normally TF_BLOCK): the parameters read once, then the samples. l / r in
 * [-OUT_CEIL, OUT_CEIL]. ext: the v1.x feed sources (metal impulses, FM bursts), n samples, or 0 */
void tf_engine_render(tf_engine_t *e, const tf_params_t *p, float *l, float *r, uint32_t n, const float *ext);
/* the gain-margin meter (margin_db, c_peak), worked out in the main loop: call it before reading them */
void tf_engine_meter(tf_engine_t *e, const tf_params_t *p);
/* the ignition threshold (dB of feedback gain) for these parameters, without running anything */
float tf_engine_threshold_db(tf_engine_t *e, const tf_params_t *p);
