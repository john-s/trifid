/* SPDX-License-Identifier: MIT
 * Copyright (C) 2026 john-s */
/* The keys (docs/PRD.md, Keys): the 16 white keys F3..G5 play the scale, one degree each up from the root, the
 * last held winning (the string is monophonic), with glide. A key's velocity fires an impulse and sets the noise feed,
 * held until the key is let go (only the feed: the gain is SELECT's); every key alike. Pressure is not used: on the
 * FM-1 it was not playable, and the top row has none. A#4 strikes the held note again; the other black keys do
 * nothing (reserved). ARP latches the feed. Pluck (GLO K3): the keys strike and do not feed.
 * Part of app.c (the unity build). */

#define NPLAY 16
static const uint8_t PLAY_KEY[NPLAY] = {0, 2, 4, 6, 7, 9, 11, 12, 14, 16, 18, 19, 21, 23, 24, 26};   /* F3 .. G5 */
#define KEY_RESTRIKE 17u                 /* A#4, the black key under GLO */
#define PLAY_MASK 0x5AD5AD5u             /* the 16 above, as key bits */
#define KEYS_LIT_MASK (PLAY_MASK | 1u << KEY_RESTRIKE)

#define NSCALES 12
static const char *const SCALE_NAME[NSCALES] = {"MAJOR", "MINOR", "DORIAN", "PHRYGIAN", "LYDIAN", "MIXOLYD",
                                                "LOCRIAN", "HARM MIN", "MAJ PENT", "MIN PENT", "WHOLE", "CHROMA"};
static const uint8_t SCALE_LEN[NSCALES] = {7, 7, 7, 7, 7, 7, 7, 7, 5, 5, 6, 12};
static const uint8_t SCALE_STEP[NSCALES][12] = {
    {0, 2, 4, 5, 7, 9, 11}, {0, 2, 3, 5, 7, 8, 10}, {0, 2, 3, 5, 7, 9, 10}, {0, 1, 3, 5, 7, 8, 10},
    {0, 2, 4, 6, 7, 9, 11}, {0, 2, 4, 5, 7, 9, 10}, {0, 1, 3, 5, 6, 8, 10}, {0, 2, 3, 5, 7, 8, 11},
    {0, 2, 4, 7, 9},        {0, 3, 5, 7, 10},       {0, 2, 4, 6, 8, 10},    {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11}};
static const char *const NOTE_NAME[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

static struct {
    uint32_t keys;                       /* held at the last poll */
    uint8_t held[NPLAY], nheld;             /* the playing keys' degrees, the oldest first */
    uint8_t vel;                         /* the top key's strike velocity (the velocity feed) */
    float recent[8];                     /* the feed over the last 8 frames: what a latch keeps at release */
    uint8_t rec_i;
    int32_t top;                         /* the key now playing (key number), -1 none */
} kb;

/* the note of degree d: the root (C2 + root, OCT- / OCT+ an octave each), d scale steps up; kept in MIDI 16..72 */
static float degree_note(uint32_t d)
{
    uint32_t n = SCALE_LEN[st.scale];
    int32_t m = 36 + st.root + 12 * st.octave + 12 * (int32_t)(d / n) + SCALE_STEP[st.scale][d % n];
    while (m > 72)
        m -= 12;
    while (m < 16)
        m += 12;
    return (float)m;
}

static void impulse(uint32_t vel)
{
    float v = (float)vel * (1.0f / 127.0f);
    live.imp_amp = v * fm_sqrtf(v);      /* a little firmer than linear: soft strikes stay soft */
    live.imp_seq++;
}

static void keys_boot(void)
{
    kb.keys = 0;
    kb.nheld = 0;
    kb.top = -1;
    kb.vel = 0;
    kb.rec_i = 0;
    for (int i = 0; i < 8; i++)
        kb.recent[i] = 0.0f;
    touch_curve(st.vel_curve);
}

static int degree_of(uint32_t k)
{
    int d;
    for (d = 0; d < NPLAY; d++)
        if (PLAY_KEY[d] == k)
            return d;
    return -1;
}

static void held_remove(uint8_t d)
{
    uint32_t i, j = 0;
    for (i = 0; i < kb.nheld; i++)
        if (kb.held[i] != d)
            kb.held[j++] = kb.held[i];
    kb.nheld = (uint8_t)j;
}

static float latched_feed(void)          /* the most the feed was in the last ~130 ms (a lifting key's pressure falls) */
{
    float m = 0.0f;
    for (int i = 0; i < 8; i++)
        m = fm_maxf(m, kb.recent[i]);
    return m;
}

/* every frame */
static void keys_poll(void)
{
    uint32_t keys, k, bit;
    float feed;
    touch_poll();
    keys = touch_keys();
    for (k = 0; k < NKEYS; k++) {
        int d;
        bit = 1u << k;
        if ((keys & bit) == (kb.keys & bit))
            continue;
        if (k == KEY_RESTRIKE) {         /* A#4: the held note again, at this velocity; its pressure is unused */
            if (keys & bit)
                impulse(touch_vel(k));
            continue;
        }
        if ((d = degree_of(k)) < 0)      /* reserved */
            continue;
        if (keys & bit) {                /* pressed: it plays now (the last held wins), struck at its velocity */
            held_remove((uint8_t)d);
            kb.held[kb.nheld++] = (uint8_t)d;
            kb.top = (int32_t)k;
            kb.vel = touch_vel(k);
            live.note = degree_note((uint32_t)d);
            impulse(kb.vel);
        } else {                         /* released: the one held before it plays again (no strike) */
            held_remove((uint8_t)d);
            if (kb.nheld) {
                kb.top = PLAY_KEY[kb.held[kb.nheld - 1]];
                live.note = degree_note(kb.held[kb.nheld - 1]);
            } else {
                kb.top = -1;
                st.u.v.drone = live.note;   /* the string stays where it was: ALGO moves it from there */
                mark();
                if (live.latch)
                    live.feed = latched_feed();
            }
        }
    }
    kb.keys = keys;
    /* the feed: the key's strike velocity, held until release, times the depth. Pluck mode: none, the strike alone
     * (a plain Karplus-Strong pluck, ringing out as the loop lets it) */
    if (kb.top >= 0 && !st.pluck) {
        feed = (float)kb.vel * (1.0f / 127.0f) * st.u.v.press_depth;
        live.feed = feed;
    } else if (st.pluck) {
        live.feed = 0.0f;
    } else if (!live.latch) {
        live.feed = 0.0f;                /* release stops the feeding */
    }
    kb.recent[kb.rec_i++ & 7u] = kb.top >= 0 ? live.feed : 0.0f;
}

/* the root (and the octave) changed: the key playing moves with it */
static void keys_retune(void)
{
    if (kb.nheld)
        live.note = degree_note(kb.held[kb.nheld - 1]);
}

static void note_text(float note, char *b)
{
    int32_t m = (int32_t)(note + 0.5f);
    b[0] = 0;
    text_cat(b, NOTE_NAME[m % 12]);
    {
        char o[4];
        int_text(m / 12 - 1, o);
        text_cat(b, o);
    }
}
