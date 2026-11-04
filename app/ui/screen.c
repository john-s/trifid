/* SPDX-License-Identifier: MIT
 * Copyright (C) 2026 john-s */
/* The screen (240 x 240): the header (the name, the note; a message for a moment), the main view (the gain-margin
 * meter: how close the loop is to ignition, above the output's scope) and the page along the bottom (its four
 * knobs' names and values). One level, no menus. Part of app.c (the unity build). */

static uint32_t cpu_peak, boot_ms;       /* the render's most load since boot (Setup shows it) */

#define MARGIN_LO (-24.0f)               /* the meter's scale, dB */
#define MARGIN_HI 12.0f

static int32_t meter_x(float db)
{
    float t = (fm_clampf(db, MARGIN_LO, MARGIN_HI) - MARGIN_LO) / (MARGIN_HI - MARGIN_LO);
    return 8 + (int32_t)(t * 224.0f + 0.5f);
}

static void draw_head(void)
{
    char t[24], f[12];
    cv_begin(240, 32, C_BLACK);
    if (plat_ms() < ui.msg_until) {
        int32_t x = cv_text(8, 7, &FONT_B, ui.msg[0], C_WHITE);
        cv_text(x + 8, 7, &FONT_B, ui.msg[1], C_HI);
    } else {
        int32_t x;
        cv_text(8, 7, &FONT_B, APP_NAME, C_WHITE);
        note_text(live.note, t);
        if (st.u.v.fine >= 0.5f || st.u.v.fine <= -0.5f) {
            dec_text(st.u.v.fine, 0, 1, f);
            text_cat(t, " ");
            text_cat(t, f);
        }
        x = 232 - text_w(&FONT_B, t);
        cv_text(x, 7, &FONT_B, t, kb.nheld ? C_WHITE : C_HI);
        cv_text(x - 8 - text_w(&FONT_XS, PAGE_NAME[st.page]), 9, &FONT_XS, PAGE_NAME[st.page], C_GRAY);
    }
    cv_rect(0, 31, 240, 1, C_LINE);
    cv_blit(0, 0);
}

static int32_t chip(int32_t x, const char *s, int on)
{
    int32_t w = text_w(&FONT_T, s) + 6;
    if (on) {
        cv_rect(x, 51, w, 13, C_HI);
        cv_text(x + 3, 51, &FONT_T, s, C_BLACK);
    } else {
        cv_text(x + 3, 51, &FONT_T, s, C_DIM);
    }
    return x + w + 2;
}

static void draw_main(void)
{
    float m = eng.margin_db, set_m = st.u.v.fb_db + eng.c_peak;     /* (the peak the render worked out) */
    int32_t x0 = meter_x(MARGIN_LO), xz = meter_x(0.0f), xm = meter_x(m), xs = meter_x(set_m), i, x;
    uint16_t c = m >= 0.0f ? C_WHITE : m > -6.0f ? C_HI : C_AMB;
    char t[24];
    uint32_t w = eng.scope_w;
    cv_begin(240, 144, C_BLACK);
    /* the margin, in figures */
    cv_text(8, 2, &FONT_XS, m >= 0.0f ? "IGNITED" : "BELOW IGNITION", m >= 0.0f ? C_WHITE : C_GRAY);
    dec_text(m < -99.0f ? -99.0f : m, 1, 1, t);
    text_cat(t, " DB");
    cv_text(232 - text_w(&FONT_M, t), 0, &FONT_M, t, c);
    /* the bar: -24 .. +12 dB, ignition at 0; the set margin a tick (Starve pulls the bar away from it) */
    cv_rect(x0, 30, 224, 12, C_LINE);
    if (xm > x0)
        cv_rect(x0, 30, (xm < xz ? xm : xz) - x0, 12, m > -6.0f ? C_HI : C_DIM);
    if (xm > xz)
        cv_rect(xz, 30, xm - xz, 12, C_WHITE);
    cv_rect(xz, 26, 2, 20, C_WHITE);
    cv_rect(xs - 1, 44, 3, 4, C_HI);
    /* what else holds the loop: the feedback gain, and the switches */
    dec_text(st.u.v.fb_db, 1, 1, t);
    text_cat(t, "DB");
    x = cv_text(8, 50, &FONT_XS, "FB", C_GRAY);
    x = cv_text(x + 4, 50, &FONT_XS, t, C_HI);
    x = chip(x + 6, "STARVE", live.starve);
    x = chip(x, "FREEZE", live.freeze);
    x = chip(x, "LATCH", live.latch);
    {
        char g[12] = "GRIT ";
        int_text((int32_t)(st.u.v.grit * 100.0f + 0.5f), t);
        text_cat(g, t);
        chip(x, g, st.u.v.grit > 0.0f);
    }
    /* the scope: the output, a point every 8 frames (44 ms across), centred at row 106 */
    cv_rect(0, 70, 240, 1, C_LINE);
    cv_rect(0, 106, 240, 1, C_LINE);
    {
        int32_t prev = 106 - eng.scope[w % TF_SCOPE_N] * 34 / 127;
        for (i = 1; i < (int32_t)TF_SCOPE_N; i++) {
            int32_t y = 106 - eng.scope[(w + (uint32_t)i) % TF_SCOPE_N] * 34 / 127;
            int32_t a = y < prev ? y : prev, b = y < prev ? prev : y;
            cv_rect(i, a, 1, b - a + 1, c);
            prev = y;
        }
    }
    {   /* the feed, as a thin bar at the right edge */
        int32_t h = (int32_t)(live.feed * 70.0f);
        if (h > 0)
            cv_rect(236, 143 - h, 3, h, C_AMB);
    }
    cv_blit(0, 36);
}

static void draw_bottom(void)
{
    int i, shift = (ui.btn >> B_EDIT) & 1u;
    char t[16];
    cv_begin(240, 56, C_BLACK);
    cv_rect(0, 0, 240, 1, C_LINE);
    cv_text(8, 3, &FONT_XS, PAGE_BTN[st.page], C_GRAY);
    cv_text(8 + text_w(&FONT_XS, PAGE_BTN[st.page]) + 6, 3, &FONT_XS, PAGE_NAME[st.page], C_HI);
    if (shift) {
        cv_text(232 - text_w(&FONT_XS, "EDIT"), 3, &FONT_XS, "EDIT", C_WHITE);
    } else if (st.page == PG_GLO) {      /* Setup: the render's load (now, the most since boot) and the xruns */
        char d[40], v[12];
        uint32_t xr = plat_xruns();
        d[0] = 0;
        text_cat(d, "CPU ");
        int_text((int32_t)plat_cpu_pct(), v);
        text_cat(d, v);
        text_cat(d, "% PEAK ");
        int_text((int32_t)cpu_peak, v);
        text_cat(d, v);
        text_cat(d, "% XRUN ");
        int_text((int32_t)xr, v);
        text_cat(d, v);
        cv_text(232 - text_w(&FONT_XS, d), 3, &FONT_XS, d, cpu_peak >= 50u || xr ? C_WHITE : C_GRAY);
    }
    for (i = 0; i < 4; i++) {
        int k = knob_at(st.page, i, shift);
        int32_t x = i * 60;
        knob_text(k, t);
        cv_text(x + 4, 20, &FONT_XS, KN_NAME[k], k != PAGE_KN[st.page][i] ? C_WHITE : C_GRAY);
        cv_text(x + 4, 36, &FONT_B, t, C_HI);
    }
    cv_blit(0, 184);
}

/* a screen reader that has just started listening (or asks) hears the screen */
static void speak(void)
{
    char t[200], v[16];
    int i;
    if (ann_on() && ann_where()) {
        t[0] = 0;
        text_cat(t, APP_NAME ". ");
        text_cat(t, PAGE_NAME[st.page]);
        text_cat(t, ". MARGIN ");
        dec_text(eng.margin_db, 1, 1, v);
        text_cat(t, v);
        text_cat(t, " DB");
        for (i = 0; i < 4; i++) {
            int k = knob_at(st.page, i, 0);
            text_cat(t, ". ");
            text_cat(t, KN_NAME[k]);
            text_cat(t, " ");
            knob_text(k, v);
            text_cat(t, v);
        }
        if (st.page == PG_GLO) {
            text_cat(t, ". CPU ");
            int_text((int32_t)cpu_peak, v);
            text_cat(t, v);
            text_cat(t, " PERCENT PEAK. XRUNS ");
            int_text((int32_t)plat_xruns(), v);
            text_cat(t, v);
        }
        ann_say(t, 0, ANN_NOW);
    }
    ann_tick(plat_ms());
}

void app_ui_init(void)
{
    touch_on(1);
    boot_ms = plat_ms();
    lcd_fill(0, 0, 240, 240, C_BLACK);
    ui.redraw_head = ui.redraw_bottom = 1;
}

void app_frame(void)
{
    static uint32_t n, last_head, stats_at;
    uint32_t now = plat_ms(), head_key;
    input();
    speak();
    /* the header again when what it shows changed (the note, the page, a message come or gone) */
    head_key = (uint32_t)(live.note * 8.0f) ^ (uint32_t)st.page << 12 ^ (uint32_t)(st.u.v.fine * 2.0f) << 16 ^
               (uint32_t)kb.nheld << 24 ^ (now < ui.msg_until ? 0x80000000u : 0u);
    if (ui.redraw_head || head_key != last_head || (ui.msg_until && now >= ui.msg_until)) {
        if (ui.msg_until && now >= ui.msg_until)
            ui.msg_until = 0;
        ui.redraw_head = 0;
        last_head = head_key;
        draw_head();
    }
    if (plat_cpu_pct() > cpu_peak && now - boot_ms > 3000u)   /* (the first seconds: boot's own work) */
        cpu_peak = plat_cpu_pct();
    if (st.page == PG_GLO && now - stats_at >= 250u) {        /* Setup's load figures, four times a second */
        stats_at = now;
        ui.redraw_bottom = 1;
    }
    if (ui.redraw_bottom || live.gliding) {
        ui.redraw_bottom = 0;
        draw_bottom();
    }
    if ((n++ & 1u) == 0)                 /* the meter and the scope: every other frame (~30 a second) */
        draw_main();
}
