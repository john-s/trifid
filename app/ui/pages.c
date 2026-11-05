/* SPDX-License-Identifier: MIT
 * Copyright (C) 2026 john-s */
/* The controls (docs/PRD.md, Controls): the fixed encoders, the five pages of four knobs, the buttons.
 *   SELECT   Feedback, the ignition control: 0.1 dB a detent within 2 dB of ignition, 0.5 within 6, else 1
 *   ALGO     fine tune while a key plays; the pitch (a semitone a detent) when none does
 *   PRESETS  Pressure: a standing noise feed, added to the key's
 *   HOME  Plant  Body / Drive / LPF / HPF
 *   FX    Reverb  Mix / Decay / Mod (depth) / Rate; FX again: Glitter  Input / Glitter / Output / Wet
 *   SEQ   Echo   Send (EDIT: Return) / Time / Feedback / Wear
 *   SEL   Keys   Scale / Root / Glide / Depth (the feed's level at full velocity)
 *   GLO   Setup  Velocity curve / Seed level / Keys: Feed or Pluck (and the render's load, the xruns)
 *   PLAY hold: Starve; double-tap: latched (a tap lets go). REC: freeze the echo. ARP: latch the feed.
 *   ENV / LFO held: the echo time x 0.5 / x 2. EDIT: shift. SAVE: the snapshot; EDIT + SAVE: recall it.
 * Part of app.c (the unity build). */

enum { KN_BODY, KN_DRIVE, KN_LPF, KN_HPF, KN_VMIX, KN_VDECAY, KN_VMOD, KN_VRATE, KN_GIN, KN_GLIT, KN_GOUT, KN_GWET,
       KN_SEND, KN_RETURN, KN_TIME, KN_EFB, KN_WEAR, KN_SCALE, KN_ROOT, KN_GLIDE, KN_DEPTH, KN_CURVE, KN_SEED,
       KN_PLUCK, KN_NONE, NKN };
static const char *const KN_NAME[NKN] = {"BODY", "DRIVE", "LPF", "HPF", "MIX", "DECAY", "MOD", "RATE", "INPUT",
                                         "GLITTER", "OUTPUT", "WET", "SEND", "RETURN", "TIME", "FDBK", "WEAR", "SCALE",
                                         "ROOT", "GLIDE", "DEPTH", "CURVE", "SEED", "KEYS", ""};
static const uint8_t PAGE_KN[NPAGES][4] = {
    {KN_BODY, KN_DRIVE, KN_LPF, KN_HPF},
    {KN_VMIX, KN_VDECAY, KN_VMOD, KN_VRATE},
    {KN_GIN, KN_GLIT, KN_GOUT, KN_GWET},
    {KN_SEND, KN_TIME, KN_EFB, KN_WEAR},
    {KN_SCALE, KN_ROOT, KN_GLIDE, KN_DEPTH},
    {KN_CURVE, KN_SEED, KN_PLUCK, KN_NONE}};
static const char *const PAGE_NAME[NPAGES] = {"PLANT", "REVERB", "GLITTER", "ECHO", "KEYS", "SETUP"};
static const char *const PAGE_BTN[NPAGES] = {"HOME", "FX", "FX", "SEQ", "SEL", "GLO"};
/* the page each button opens (FX: the reverb, then each press the next effect page) */
static const uint8_t PAGE_OF_BTN[NB] = {
    [B_HOME] = PG_HOME + 1, [B_FX] = PG_VERB + 1, [B_SEQ] = PG_SEQ + 1, [B_SEL] = PG_SEL + 1, [B_GLO] = PG_GLO + 1};

static struct {
    uint32_t btn;                        /* held at the last poll */
    uint32_t play_at;                    /* PLAY's last press */
    uint32_t play_tap;                   /* when the last short tap of PLAY began (a double tap latches) */
    uint8_t play_held;
    uint8_t redraw_bottom, redraw_head;
    char msg[2][24];
    uint32_t msg_until;
} ui;

/* the knob on page slot i, with EDIT held or not */
static int knob_at(int page, int i, int shift)
{
    int k = PAGE_KN[page][i];
    if (shift && k == KN_SEND)
        return KN_RETURN;
    return k;
}

static float *knob_f(int k)              /* a 0..1 knob's value */
{
    switch (k) {
    case KN_BODY: return &st.u.v.body;
    case KN_DRIVE: return &st.u.v.drive;
    case KN_LPF: return &st.u.v.lpf;
    case KN_HPF: return &st.u.v.hpf;
    case KN_VMIX: return &st.u.v.verb_mix;
    case KN_VDECAY: return &st.u.v.verb_decay;
    case KN_VMOD: return &st.u.v.verb_depth;
    case KN_VRATE: return &st.u.v.verb_rate;
    case KN_GIN: return &st.u.v.glit_input;
    case KN_GLIT: return &st.u.v.glit_amount;
    case KN_GOUT: return &st.u.v.glit_output;
    case KN_GWET: return &st.u.v.glit_wet;
    case KN_SEND: return &st.u.v.echo_send;
    case KN_RETURN: return &st.u.v.echo_ret;
    case KN_TIME: return &st.u.v.echo_time;
    case KN_EFB: return &st.u.v.echo_fb;
    case KN_WEAR: return &st.u.v.echo_wear;
    case KN_GLIDE: return &st.u.v.glide;
    case KN_DEPTH: return &st.u.v.press_depth;
    default: return 0;
    }
}

/* a value as the screen shows it */
static void knob_text(int k, char *b)
{
    float *f = knob_f(k), v = f ? *f : 0.0f;
    char t[12];
    b[0] = 0;
    switch (k) {
    case KN_BODY:
        dec_text(tf_body_ms(v), tf_body_ms(v) < 10.0f ? 1 : 0, 0, b);
        text_cat(b, "MS");
        return;
    case KN_LPF:
    case KN_HPF: {
        float hz = k == KN_LPF ? tf_lpf_hz(v) : tf_hpf_hz(v);
        if (hz >= 1000.0f) {
            dec_text(hz * 0.001f, hz < 10000.0f ? 1 : 0, 0, b);
            text_cat(b, "K");
        } else {
            dec_text(hz, 0, 0, b);
        }
        return;
    }
    case KN_TIME: {
        float s = tf_echo_s(v);
        if (s < 1.0f) {
            dec_text(s * 1000.0f, 0, 0, b);
            text_cat(b, "MS");
        } else {
            dec_text(s, 2, 0, b);
            text_cat(b, "S");
        }
        return;
    }
    case KN_VRATE:
        dec_text(0.05f * fm_exp2f(6.643856f * v), 2, 0, b);    /* (verb.c's map: 0.05 .. 5 Hz) */
        text_cat(b, "HZ");
        return;
    case KN_GIN:
    case KN_GOUT:
        dec_text(v * 36.0f - 18.0f, 0, 1, b);                 /* (glitter.c's map: -18 .. +18 dB) */
        text_cat(b, "DB");
        return;
    case KN_EFB:
        dec_text(tf_echo_fb(v), 2, 0, b);
        text_cat(b, "X");
        return;
    case KN_GLIDE:
        dec_text(tf_glide_s(v), 2, 0, b);
        text_cat(b, "S");
        return;
    case KN_SCALE:
        text_cat(b, SCALE_NAME[st.scale]);
        return;
    case KN_ROOT:
        text_cat(b, NOTE_NAME[st.root]);
        return;
    case KN_CURVE:
        int_text(st.vel_curve, t);
        if (st.vel_curve > 0)
            text_cat(b, "+");
        text_cat(b, t);
        return;
    case KN_PLUCK:
        text_cat(b, st.pluck ? "PLUCK" : "FEED");
        return;
    case KN_NONE:
        return;
    case KN_SEED:
        dec_text(st.u.v.seed_db, 0, 0, b);
        text_cat(b, "DB");
        return;
    default:
        int_text((int32_t)(v * 100.0f + 0.5f), b);
        return;
    }
}

void app_say(const char *a, const char *b)
{
    uint32_t i;
    char t[56];
    for (i = 0; i < 23u && a && a[i]; i++)
        ui.msg[0][i] = a[i];
    ui.msg[0][i] = 0;
    for (i = 0; i < 23u && b && b[i]; i++)
        ui.msg[1][i] = b[i];
    ui.msg[1][i] = 0;
    ui.msg_until = plat_ms() + 1300u;
    ui.redraw_head = 1;
    if (ann_on()) {                      /* and to a screen reader (fm1-base announce.c) */
        t[0] = 0;
        text_cat(t, ui.msg[0]);
        if (ui.msg[1][0]) {
            text_cat(t, " ");
            text_cat(t, ui.msg[1]);
        }
        ann_say(t, 0, ANN_NOW);
    }
}

static void knob_turn(int k, int32_t e)
{
    if (k == KN_NONE)                    /* (a free slot) */
        return;
    float *f = knob_f(k);
    char t[16];
    int32_t a = e < 0 ? -e : e;
    float step = a >= 3 ? 0.03f : 0.01f;  /* turned fast: three times the step */
    live.gliding = 0;                    /* a hand on a knob ends a recall's glide where it is */
    if (f) {
        *f = fm_clampf(*f + (float)e * step, 0.0f, 1.0f);
    } else {
        switch (k) {
        case KN_SCALE: st.scale = (int8_t)clamp(st.scale + e, 0, NSCALES - 1); keys_retune(); break;
        case KN_ROOT: st.root = (int8_t)clamp(st.root + e, 0, 11); keys_retune(); break;
        case KN_CURVE: st.vel_curve = (int8_t)clamp(st.vel_curve + e, -5, 5); touch_curve(st.vel_curve); break;
        case KN_PLUCK: st.pluck = e > 0; if (st.pluck) live.latch = 0; break;
        case KN_SEED: st.u.v.seed_db = fm_clampf(st.u.v.seed_db + (float)e, -120.0f, -60.0f); break;
        default: break;
        }
    }
    mark();
    knob_text(k, t);
    app_say(KN_NAME[k], t);
    ui.redraw_bottom = 1;
}

/* SELECT: the feedback gain, finer near ignition */
static void feedback_turn(int32_t e)
{
    float m = st.u.v.fb_db + eng.c_peak, am = fm_fabsf(m), step = am < 2.0f ? 0.1f : am < 6.0f ? 0.5f : 1.0f;
    char t[16];
    live.gliding = 0;
    st.u.v.fb_db = fm_clampf(st.u.v.fb_db + (float)e * step, PLANT_FB_LO_DB, PLANT_FB_HI_DB);
    st.u.v.fb_db = fm_floorf(st.u.v.fb_db * 10.0f + 0.5f) * 0.1f;
    mark();
    dec_text(st.u.v.fb_db, 1, 1, t);
    text_cat(t, "DB");
    app_say("FEEDBACK", t);
}

static void algo_turn(int32_t e)
{
    char t[16];
    live.gliding = 0;
    if (kb.nheld) {                      /* a key plays: fine tune */
        st.u.v.fine = fm_clampf(st.u.v.fine + (float)e, -100.0f, 100.0f);
        dec_text(st.u.v.fine, 0, 1, t);
        text_cat(t, "C");
        app_say("FINE", t);
    } else {                             /* none: the pitch */
        st.u.v.drone = fm_clampf(fm_floorf(st.u.v.drone + 0.5f) + (float)e, PLANT_NOTE_LO, PLANT_NOTE_HI);
        live.note = st.u.v.drone;
        note_text(st.u.v.drone, t);
        app_say("PITCH", t);
    }
    mark();
    ui.redraw_head = 1;
}

/* PRESETS: Pressure, a standing noise feed added to the key's (a hand on the excitation; 0 at every boot) */
static void pressure_turn(int32_t e)
{
    char t[8];
    int32_t a = e < 0 ? -e : e;
    live.gliding = 0;
    st.u.v.pressure = fm_clampf(st.u.v.pressure + (float)e * (a >= 3 ? 0.03f : 0.01f), 0.0f, 1.0f);
    mark();
    int_text((int32_t)(st.u.v.pressure * 100.0f + 0.5f), t);
    app_say("PRESSURE", t);
}

static void octave_step(int32_t d)
{
    char t[4];
    int32_t o = clamp(st.octave + d, -2, 2);
    if (o == st.octave)
        return;
    st.octave = (int8_t)o;
    keys_retune();
    mark();
    t[0] = o < 0 ? '-' : '+';
    t[1] = (char)('0' + (o < 0 ? -o : o));
    t[2] = 0;
    app_say("OCTAVE", t);
}

static void buttons_poll(void)
{
    uint32_t btn = plat_buttons(), down = btn & ~ui.btn, k, now = plat_ms();
    int shift = (btn >> B_EDIT) & 1u;
    for (k = 0; k < NB; k++) {
        if (!(down >> k & 1u))
            continue;
        if (PAGE_OF_BTN[k]) {
            if (k == B_FX && (st.page == PG_VERB || st.page == PG_GLIT))
                st.page = st.page == PG_VERB ? PG_GLIT : PG_VERB;   /* FX again: the next effect page */
            else
                st.page = (int8_t)(PAGE_OF_BTN[k] - 1);
            mark();
            app_say(PAGE_NAME[st.page], "");
            ui.redraw_bottom = 1;
        } else if (k == B_PLAY) {        /* Starve: held; a double tap latches it, a tap lets it go */
            if (live.starve_latch) {
                live.starve_latch = 0;
                ui.play_at = 0;
            } else if (ui.play_tap && now - ui.play_tap < 350u) {
                live.starve_latch = 1;   /* the second of two taps */
                ui.play_tap = 0;
                app_say("STARVE", "LATCHED");
            } else {
                ui.play_tap = 0;
            }
            ui.play_at = now ? now : 1u;
        } else if (k == B_REC) {
            live.freeze = !live.freeze;
            app_say("FREEZE", live.freeze ? "ON" : "OFF");
        } else if (k == B_ARP) {
            live.latch = !live.latch;
            if (!live.latch && kb.top < 0)
                live.feed = 0.0f;
            app_say("LATCH", live.latch ? "ON" : "OFF");
        } else if (k == B_SAVE) {
            if (shift)
                snapshot_recall();
            else
                snapshot_save();
        } else if (k == B_OCTDN) {
            octave_step(-1);
        } else if (k == B_OCTUP) {
            octave_step(1);
        }
    }
    if ((ui.btn & ~btn) >> B_PLAY & 1u)  /* PLAY let go: a short press is a tap, the first of a double one */
        ui.play_tap = ui.play_at && now - ui.play_at < 250u && !live.starve_latch ? ui.play_at : 0u;
    if ((btn ^ ui.btn) >> B_EDIT & 1u)   /* the shifted names show while EDIT is held */
        ui.redraw_bottom = 1;
    live.starve = (uint8_t)(live.starve_latch || (btn >> B_PLAY & 1u));
    live.time_mul = ((btn >> B_ENV) & 1u) && !((btn >> B_LFO) & 1u) ? 0.5f
                  : ((btn >> B_LFO) & 1u) && !((btn >> B_ENV) & 1u) ? 2.0f : 1.0f;
    ui.btn = btn;
}

static void encoders_poll(void)
{
    int32_t e;
    int i, shift = (ui.btn >> B_EDIT) & 1u;
    if ((e = plat_enc(EN_SELECT)) != 0)
        feedback_turn(e);
    if ((e = plat_enc(EN_ALGO)) != 0)
        algo_turn(e);
    if ((e = plat_enc(EN_PRESET)) != 0)
        pressure_turn(e);
    for (i = 0; i < 4; i++)
        if ((e = plat_enc(EN_K1 + i)) != 0)
            knob_turn(knob_at(st.page, i, shift), e);
}

static void lights(void)
{
    static uint32_t last_b = 0xFFFFFFFFu, last_k = 0xFFFFFFFFu;
    uint32_t b = 0, k = kb.keys & KEYS_LIT_MASK;
    b |= 1u << (st.page == PG_HOME ? B_HOME : st.page == PG_VERB || st.page == PG_GLIT ? B_FX
                : st.page == PG_SEQ ? B_SEQ : st.page == PG_SEL ? B_SEL : B_GLO);
    if (live.starve)
        b |= 1u << B_PLAY;
    if (live.freeze)
        b |= 1u << B_REC;
    if (live.latch)
        b |= 1u << B_ARP;
    b |= ui.btn & (1u << B_EDIT | 1u << B_ENV | 1u << B_LFO | 1u << B_SAVE | 1u << B_OCTDN | 1u << B_OCTUP);
    if (b == last_b && k == last_k)
        return;
    last_b = b;
    last_k = k;
    plat_glow(1);
    plat_play_red(live.starve_latch);
    plat_leds(b, k);
    plat_lights_mid(0, KEYS_LIT_MASK & ~k);   /* the keys that do something, a little lit */
}

static void input(void)
{
    buttons_poll();
    keys_poll();
    encoders_poll();
    recall_glide();
    if (!kb.nheld)
        live.note = st.u.v.drone;
    params_push();
    lights();
    autosave();
    saves_poll();
    tf_engine_meter(&eng, &prm);
}

void app_input_only(void) { input(); }

static void ui_boot(void)
{
    keys_boot();
    ui.btn = plat_buttons();
    ui.play_at = ui.play_tap = 0;
    ui.msg_until = 0;
    ui.redraw_bottom = ui.redraw_head = 1;
}
