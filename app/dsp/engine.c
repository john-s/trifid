/* SPDX-License-Identifier: MIT
 * Copyright (C) 2026 john-s */
/* Trifid's engine (engine.h): the per-sample chain, the block's controls */
#include "engine.h"

#define TF_MAKEUP 0.8f                  /* the loop's level back up after the drive's normalisation */
#define TF_OUT_GAIN 0.45f

void tf_params_default(tf_params_t *p)
{
    p->fb_db = -30.0f;
    p->note = 40.0f;
    p->fine = 0.0f;
    p->glide = 0.25f;
    p->body = 0.5f;                     /* 10 ms */
    p->drive = 0.4f;
    p->lpf = 0.85f;                     /* ~8 kHz */
    p->hpf = 0.0f;                      /* 10 Hz: the lowest notes keep their fundamental */
    p->verb_mix = 0.0f;
    p->verb_decay = 0.6f;
    p->verb_depth = 0.4f;
    p->verb_rate = 0.4f;
    p->glit_input = 0.5f;               /* 0 dB in and out */
    p->glit_amount = 0.3f;
    p->glit_output = 0.5f;
    p->glit_wet = 0.0f;                 /* bypassed */
    p->echo_send = 0.0f;
    p->echo_ret = 0.0f;
    p->echo_time = 0.6f;
    p->echo_fb = 0.4f;
    p->echo_wear = 0.0f;
    p->time_mul = 1.0f;
    p->seed_db = -90.0f;
    p->feed = 0.0f;
    p->starve = 0;
    p->freeze = 0;
    p->imp_seq = 0;
    p->imp_amp = 0.0f;
}

void tf_engine_init(tf_engine_t *e, const tf_buffers_t *b)
{
    uint32_t i;
    plant_init(&e->plant, b->str[0], b->str[1], b->body[0], b->body[1], 0x1234567u);
    verb_init(&e->verb, b->verb);
    glitter_init(&e->glit);
    echo_init(&e->echo, b->echo[0], b->echo[1], 0x5EED5EEDu);
    out_init(&e->out);
    e->note_now = 40.0f;
    e->ret_l = e->ret_r = 0.0f;
    e->imp_seen = 0;
    e->primed = 0;
    e->c_note = e->c_lpf = e->c_hpf = -1.0f;
    e->c_peak = 0.0f;
    e->margin_db = -99.0f;
    e->peak = 0.0f;
    for (i = 0; i < TF_SCOPE_N; i++)
        e->scope[i] = 0;
    e->scope_w = e->scope_ph = 0;
}

/* the knobs to the modules: what the block runs with */
static void map(tf_engine_t *e, const tf_params_t *p, plant_params_t *pp, verb_params_t *rp, echo_params_t *ep,
                float note)
{
    pp->note = note + p->fine * 0.01f;
    pp->body = p->body;
    pp->drive = p->drive;
    pp->lpf_hz = tf_lpf_hz(p->lpf);
    pp->hpf_hz = tf_hpf_hz(p->hpf);
    pp->fb_db = p->fb_db;
    pp->seed_db = p->seed_db;
    pp->press = 0.25f * p->feed * p->feed;     /* full pressure: noise at -12 dBFS into the string */
    pp->ret = p->echo_ret;
    pp->starve = p->starve;
    rp->mix = p->verb_mix;
    rp->decay = p->verb_decay;
    rp->depth = p->verb_depth;
    rp->rate = p->verb_rate;
    ep->send = p->echo_send;
    ep->time_s = tf_echo_s(p->echo_time) * (p->time_mul > 0.0f ? p->time_mul : 1.0f);
    ep->feedback = tf_echo_fb(p->echo_fb);
    ep->wear = p->echo_wear;
    ep->freeze = p->freeze;
    (void)e;
}

/* the meter, from the main loop (its sums are too heavy for the audio interrupt): the loop's peak gain, worked out
 * again only when the pitch or the filters moved, plus the feedback gain the render has now */
void tf_engine_meter(tf_engine_t *e, const tf_params_t *p)
{
    float note = e->note_now, lpf = tf_lpf_hz(p->lpf), hpf = tf_hpf_hz(p->hpf);
    if (fm_fabsf(note - e->c_note) > 0.02f || fm_fabsf(lpf - e->c_lpf) > 1.0f || fm_fabsf(hpf - e->c_hpf) > 0.1f) {
        tf_params_t q = *p;
        e->c_note = note;
        e->c_lpf = lpf;
        e->c_hpf = hpf;
        q.note = note;
        e->c_peak = -tf_engine_threshold_db(e, &q);
    }
    e->margin_db = 20.0f * fm_log2f(fm_maxf(e->plant.g, 1e-6f)) * 0.30103f + e->c_peak;
}

float tf_engine_threshold_db(tf_engine_t *e, const tf_params_t *p)
{
    plant_params_t pp;
    verb_params_t rp;
    echo_params_t ep;
    plant_t t = e->plant;               /* a copy: the running plant is left alone */
    map(e, p, &pp, &rp, &ep, p->note);
    plant_set(&t, &pp);
    return -plant_loop_peak_db(&t);
}

void tf_engine_render(tf_engine_t *e, const tf_params_t *p, float *l, float *r, uint32_t n, const float *ext)
{
    plant_params_t pp;
    verb_params_t rp;
    echo_params_t ep;
    plant_t *pl = &e->plant;
    float glide = tf_glide_s(p->glide), makeup;
    uint32_t i;
    if (!e->primed) {                   /* the first block: the string starts at its note, no glide from nowhere */
        e->note_now = p->note;
        e->primed = 1;
    }
    if (glide <= 0.0f)
        e->note_now = p->note;
    else                                /* a glide in the pitch domain (exponential in Hz), a block at a time */
        e->note_now += (1.0f - fm_expf(-(float)n / (glide * 0.5f * TF_FS))) * (p->note - e->note_now);
    map(e, p, &pp, &rp, &ep, e->note_now);
    plant_set(pl, &pp);
    verb_set(&e->verb, &rp);
    {
        glitter_params_t gp;
        gp.input = p->glit_input;
        gp.glitter = p->glit_amount;
        gp.output = p->glit_output;
        gp.wet = p->glit_wet;
        glitter_set(&e->glit, &gp);
    }
    echo_set(&e->echo, &ep);
    e->out.gain = TF_OUT_GAIN;
    if (p->imp_seq != e->imp_seen) {
        e->imp_seen = p->imp_seq;
        plant_impulse(pl, p->imp_amp);
    }
    makeup = TF_MAKEUP * pl->drive_g;
    e->out.peak = 0.0f;
    for (i = 0; i < n; i++) {
        float fl, fr, yl, yr, el, er, ol, orr;
        plant_tick_a(pl, e->ret_l, e->ret_r, ext ? ext[i] : 0.0f, &fl, &fr);
        yl = fl;                        /* the reverb: the loop's mid in, stereo wet, crossfaded by Mix */
        yr = fr;
        verb_tick(&e->verb, &yl, &yr);
        plant_tick_b(pl, yl, yr);
        ol = yl * makeup;
        orr = yr * makeup;
        glitter_tick(&e->glit, &ol, &orr);   /* (after the loop: the echo repeats the crushed sound) */
        echo_tick(&e->echo, ol, orr, &el, &er);
        e->ret_l = el;
        e->ret_r = er;
        ol += el;
        orr += er;
        out_tick(&e->out, &ol, &orr);
        l[i] = ol;
        r[i] = orr;
        if (++e->scope_ph == 8u) {
            uint32_t w = e->scope_w;
            e->scope_ph = 0;
            e->scope[w] = (int8_t)(ol * (127.0f / OUT_CEIL));
            e->scope_w = w + 1 == TF_SCOPE_N ? 0 : w + 1;
        }
    }
    plant_sanitize(pl);
    verb_sanitize(&e->verb);
    glitter_sanitize(&e->glit);
    echo_sanitize(&e->echo);
    out_sanitize(&e->out);
    e->ret_l = tf_clean(e->ret_l);
    e->ret_r = tf_clean(e->ret_r);
    e->peak = e->out.peak;
}
