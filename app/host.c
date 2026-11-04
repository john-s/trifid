/* SPDX-License-Identifier: MIT
 * Copyright (C) 2026 john-s */
/* The host simulator's part (fm1-base host/plat_host.c): Trifid's own checks for a script's "expect WHAT VALUE".
 *   fb10        the feedback gain, dB x 10            margin10   the loop's margin now, dB x 10 (the meter)
 *   setmargin10 the margin the feedback is set to      thresh10   the ignition threshold, dB x 10
 *   note10      the string's target note x 10          drone10    the drone pitch x 10
 *   fine        cents                                   feed100    the feed level x 100
 *   imp         impulses fired                          page       PG_* (0 HOME .. 4 GLO)
 *   starve, starvelatch, freeze, latch                  timemul100 the echo time's multiplier x 100
 *   grit100, scale, root, octave, outmode, feedmode, curve, sense, seed (dB)
 *   knob100_N   knob N (KN_*) x 100                     peakdb     the output's peak this block, dBFS
 *   gliding     a recall gliding                        dirty      changed since the last autosave
 * Part of app.c. */
#include <string.h>
#include <stdlib.h>

int app_expect(const char *what, int *got)
{
    float th = threshold_db();
#define R10(x) ((int)((x) * 10.0f + ((x) >= 0.0f ? 0.5f : -0.5f)))
    if (!strcmp(what, "fb10")) *got = R10(st.u.v.fb_db);
    else if (!strcmp(what, "margin10")) *got = R10(eng.margin_db);
    else if (!strcmp(what, "setmargin10")) *got = R10(st.u.v.fb_db - th);
    else if (!strcmp(what, "thresh10")) *got = R10(th);
    else if (!strcmp(what, "note10")) *got = R10(live.note);
    else if (!strcmp(what, "drone10")) *got = R10(st.u.v.drone);
    else if (!strcmp(what, "fine")) *got = (int)st.u.v.fine;
    else if (!strcmp(what, "feed100")) *got = (int)(live.feed * 100.0f + 0.5f);
    else if (!strcmp(what, "imp")) *got = (int)live.imp_seq;
    else if (!strcmp(what, "page")) *got = st.page;
    else if (!strcmp(what, "starve")) *got = live.starve;
    else if (!strcmp(what, "starvelatch")) *got = live.starve_latch;
    else if (!strcmp(what, "freeze")) *got = live.freeze;
    else if (!strcmp(what, "latch")) *got = live.latch;
    else if (!strcmp(what, "timemul100")) *got = (int)(live.time_mul * 100.0f + 0.5f);
    else if (!strcmp(what, "grit100")) *got = (int)(st.u.v.grit * 100.0f + 0.5f);
    else if (!strcmp(what, "scale")) *got = st.scale;
    else if (!strcmp(what, "root")) *got = st.root;
    else if (!strcmp(what, "octave")) *got = st.octave;
    else if (!strcmp(what, "outmode")) *got = st.out_mode;
    else if (!strcmp(what, "feedmode")) *got = st.feed_mode;
    else if (!strcmp(what, "curve")) *got = st.vel_curve;
    else if (!strcmp(what, "sense")) *got = st.press_sense;
    else if (!strcmp(what, "seed")) *got = (int)st.u.v.seed_db;
    else if (!strcmp(what, "gliding")) *got = live.gliding;
    else if (!strcmp(what, "dirty")) *got = live.dirty;
    else if (!strcmp(what, "peakdb")) *got = (int)(20.0f * fm_log2f(fm_maxf(eng.peak, 1e-6f)) * 0.30103f);
    else if (!strncmp(what, "knob100_", 8)) {
        float *f = knob_f(atoi(what + 8));
        *got = f ? (int)(*f * 100.0f + 0.5f) : -1;
    } else
        return 0;
    return 1;
}

/* a reboot: what RAM held is gone */
void app_host_reboot(void)
{
    memset(&live, 0, sizeof live);
    memset(&kb, 0, sizeof kb);
    memset(&ui, 0, sizeof ui);
}
