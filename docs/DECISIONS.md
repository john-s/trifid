# Trifid: decisions

## Decisions

The owner settled these questions when the PRD was written. They're applied in `docs/PRD.md`, and the MVP is built
to them.

1. **Keys differ from the original PRD.** All 16 white keys F3–G5 play the chosen scale, one degree each up from
   the root, with glide (the owner widened it from the 12 white keys F3–C5 on 2026-11-04, after playing it). OCT−/OCT+
   shift by an octave. The string is monophonic and the last-held key wins.
2. **Velocity sets the noise feed level, held until release, on every key alike** (changed 2026-11-04). It raises
   only the feed, never the feedback gain, which belongs to the Feedback encoder (SELECT). Release stops feeding.
   Originally pressure fed the loop, with velocity as a Setup fallback. On the FM-1 the owner found pressing harder
   wasn't usable, and the top row (D5–G5) has no pressure at all, so pressure, the Sense knob and the feed-mode
   option were cut for one behaviour on all 16 keys.
3. **Velocity fires an impulse** (a short noise burst) at that strength.
4. **A#4 (labelled GLO) re-strikes the held note.** Velocity sets the impulse, and the pitch doesn't change.
5. **Every other black key is reserved** and does nothing.
6. **Speaker mode is cut** (2026-11-04, after playing it): the output is flat. Setup (GLO) is now Velocity curve /
   Seed level, with two free knobs.
7. **Echo length is 4 s**, stereo, int16 at fs/4 (176,472 bytes of pool).
8. **USB audio stays on for now.** Its cost is measured (`docs/ram.md`); routing it in as a feed source is
   investigated in `docs/usb-audio.md`, which recommends cutting it from the MVP.
9. **The second plant (cpu2) is a stretch goal.** cpu2 stays off: core 1 misreads RAM on some units, so the second
   plant is never required.
10. **Feed sources in the MVP**: noise seed, the key feed (from velocity), velocity impulse and echo return. Metal impulses from
    flash, FM bursts and FM cross-mod are v1.x. The hook is `tf_engine_render(..., ext)`, a per-sample signal summed
    into the feed mixer.
11. **Fixed encoders**: SELECT = Feedback (fine/coarse near the threshold; momentum is v1.x), ALGORITHM = fine tune,
    or pitch when no key is held, PRESETS = Pressure (was Grit: decision 15), MASTER = the hardware volume pot
    after the limiter.
12. **Grit at 0 was fully clean**: anti-aliased fs/4 paths, dithered 16-bit storage, no Wear, no wavefold. Grit is
    now replaced (decision 15); its successor is clean when bypassed (Glitter at Wet 0, the reverb at Mix 0, Wear 0).
13. **Boot is always quiet and safe.**
15. **Glitter replaces Grit, and a chorused reverb replaces Rot** (2026-11-04, after playing it: Grit mostly added
    noise). Glitter is Airwindows' BitGlitter, after the loop and before the echo, with its own four controls. The
    reverb is a chorused plate in the loop. FX pages between the two: Reverb (Mix / Decay / Mod / Rate), then
    Glitter (Input / Glitter / Output / Wet). PRESETS, freed, became **Pressure**: a standing noise feed added to the
    key's (the owner's idea). Echo Wear stands on its own knob, and the wavefold is gone.
14. **Licensing**: concept credit to Audrey II (Synthux Academy: Nick Donaldson / Infrasonic Audio, Roey Tsemah;
    MIT). No DaisySP code at all, LGPL or otherwise. fm1-base and felucca-bsp stay MPL-2.0 in their submodule, and
    the JieLi AC79 SDK's Apache-2.0 notices stay with the three SDK files the package build fetches (never committed).

## Implementation decisions

The PRD left these to the implementation. Each one is easy to change. The ones marked **(flag)** change how the
instrument behaves, and are worth the owner's ear.

### Sound engine

- **The body delay is locked to the pitch (flag).** Audrey II's body delay is free, so the loop's own period sets
  the pitch, which is "rough" and doesn't follow the note. The PRD requires pitch tracking across MIDI 16–72, so
  Trifid picks the body delay that makes the whole outer loop a whole number of string periods: the string at its
  peak, the LPF and HPF phase, and the body. It uses the whole number nearest the Body knob's 1–100 ms
  (`plant_body_delay`). The outer loop's modes then fall on the string's harmonics, and the loop plays the note
  within about 1.5 cents (tested every 4 semitones from 16 to 72). Consequences:
  - Body still moves from about 1 to 100 ms, but in steps of one period: fine at high notes (about 2 ms at MIDI 72),
    coarse at the lowest (about 48 ms at MIDI 16, so only one or two choices).
  - When the pitch glides, the body follows, smoothed over about 0.2 s (tape-like: it bends).
  - The right channel's body is still 4 samples shorter. That loop sits a hair off the note, which is where the
    stereo width comes from.
- **The string's damping tracks its pitch**: a one-pole at 10× the fundamental (80 Hz–12 kHz). With a fixed damping,
  the loop rang on whichever upper harmonic had the most gain (a detuned 6th, at MIDI 40). Tracking keeps the
  fundamental the loudest mode in the loop, as in a classic Karplus-Strong brightness. The string's own feedback is
  0.9, and its DC block sits at 3 Hz so the lowest notes keep their fundamental.
- **The drive is normalized**: `softclip(g·x)/g`. Drive moves the clip point, not the small-signal gain, so turning
  Drive never moves the ignition threshold. That keeps it the Feedback encoder's alone. A fixed makeup gain after
  the loop restores the level. Drive 0.4, the default, is ×3.25 into the clip.
- **Every stage in the dry loop has slope ≤ 1**: the cubic soft clip and the filters. By the small-gain theorem the
  dry loop can't ignite while its linear peak gain is below 1, however long it runs. The tests check this: −3 dB
  margin is silent for 20 s at every tested pitch.
- **The gain-margin meter** shows `feedback gain + the dry loop's peak gain`: the string's peak gain at each harmonic,
  through the damping, DC block, LPF and HPF, maximised over the harmonics. It's worked out in the main loop, only
  when the pitch or filters move. It's exact for the dry loop (Mix 0). With the reverb in, it says "(DRY)": the plate
  moves the real ignition point. Measured over Mix 0.25–1 and Decay 0–1 at MIDI 40 and 64, the loop ignited between
  12 dB after the meter's 0 (Mix 1, short decays: the wet adds back at random phase what the crossfade takes from
  the dry) and 10 dB before it (Mix 1, Decay 1, MIDI 64: the plate's resonances). The error depends on pitch, so no
  simple correction makes the meter exact. It leaves out the echo's Return, a secondary loop, and the echo's own
  self-oscillation above 1×.
- **Ignition sits about 18–19 dB below unity feedback** (the string rings +19 dB at its peaks). The Feedback range
  stays −60 to +12 dB as specified, so the top 30 dB is deep saturation.
- **Low notes build slowly.** At 3 dB over ignition, MIDI 16 takes more than 10 s to reach full level (the string's
  ring time at 20 Hz is about 0.4 s); at +6 dB it's there within 10 s. That's physical, and it's left as is.
- **The reverb (verb.c)**: Dattorro's plate (input bandwidth, four input diffusers, a cross-fed figure-eight tank,
  seven output taps a side), with every delay at 0.55 of Dattorro's sample counts at 29.8 kHz. The full plate at
  44.1 kHz would need about 67 KB, so this one is about 37% of the original size, in 12,400 int16 samples (24,800
  B). Lines run at the full rate, with no decimation. Decay maps to 0.2–0.97 (the exact float plate measures RT60
  0.3–14 s). Damping is fixed at 0.35, bandwidth 0.9995. Mod sweeps the tank's two allpasses by up to ±6 samples
  with a quadrature LFO at 0.05–5 Hz (Rate). Mix 0 bypasses it bit for bit, and the lines clear when it wakes.
  - **The wet scale**: 1 / the plate's energy gain at that decay, measured on an exact float build
    (`tests/dsp/verb_gain.c`, table in `verb.c`). So on average it neither adds nor removes loop gain. Scaling by
    its *peak* gain instead (3.5× to 92×) would make it inaudible at long decays, and Mix would only choke the loop.
  - **Storage**: rounded, with ±1 LSB TPDF dither on the tank's two recirculating writes. Plain rounding held the tail
    in a limit cycle at about −66 dBFS at any decay. Truncation cut the tails short instead, because its bias toward
    zero outweighs the decay at low levels. The dither leaves a floor near −87 dBFS.
  - **Boot**: with the reverb in, the loop can ignite up to ~10 dB before the dry threshold, so boot keeps the
    feedback up to 12 dB further below it, scaled by Mix × Decay.
- **The echo's fs/4 path**: a 6th-order Butterworth at 3.8 kHz before decimation, and linear interpolation plus a
  2-pole smoother after. Tones from 8.5 to 19 kHz fold down 52 dB below the input or more (measured). It's always
  clean now: the Grit control that could make it alias is gone.
- **The echo's storage**: rounding with light TPDF dither, ±1 LSB of 16 bits whatever the step. Wear's bit loss
  coarsens the step (16 down to 7 bits), and the rounding then crunches with the signal rather than hissing. (An
  earlier version dithered by ±1 *step*: Grit at full with Rot wet hissed at −47 dBFS below ignition. That's fixed,
  and moot now that Grit is gone.)
- **Glitter (glitter.c)**: BitGlitter ported line for line to single precision. Its steps' subtraction loops became a
  ceiling, and its trailing 32-bit float dither was dropped. Its quirk stays: `lastSample` is never updated, so each
  held sample is interpolated toward 0. Placement: after the loop's makeup gain, before the echo's send, so it
  never touches ignition. Input and Output are BitGlitter's −18..+18 dB (with its /14 law), and Wet 0 bypasses it bit
  for bit. At full Glitter the slower reducer holds each sample for 130 frames.
- **Echo**: time 20 ms–3.99 s (exponential knob). The tape glides to a new time over about 0.25 s. Feedback is
  0–1.25×, through the 800 Hz bandpass (Q 0.7, unity peak) and the soft clip: past about 0.8 on the knob it rings by
  itself, by design. Wow is 0.15% at 0.55 Hz, flutter 0.06% band-limited noise. Wear (its own knob; 0 by default) adds hiss (up to about −56 dB),
  dropouts (20–120 ms dips, about every 2 s at full Wear) and bit loss. **Freeze** stops writing the input and copies
  the tape onto itself at a whole-sample delay, so the loop plays unchanged indefinitely (no requantization, no
  filter). Return adds the echo's output to the feed mixer.
- **ENV / LFO are momentary (flag)**: held, they make the echo time ×0.5 / ×2, and it glides like a tape changing
  speed. Released, it goes back. (Read as "hold to step".)
- **Starve**: the feedback gain goes to the floor (−60 dB) with a 4 ms time constant (at the floor within about
  35 ms, without a click), and back with 30 ms when it's let go.
- **Output**: flat (Speaker mode, a band-limit and saturation for the built-in driver, was cut). Then the limiter: −1 dBFS ceiling, instant attack (no lookahead: the gain drops at once to keep the frame at the
  ceiling), 80 ms release, then a clamp that also maps NaN/∞ to 0. MASTER comes after it, in fm1-base, as a gain
  ≤ 1. Nothing past −1 dBFS can leave (tested with 2 million frames of noise, 1e30, ∞ and NaN, and 58 s of
  everything at full).
- **NaN and denormal safety**: every IIR state is flushed below 1e-20 once a block. A non-finite state resets that
  module's lines (the loop can't make one by itself; the guard is against the impossible).

### Controls and UI

- **Feedback steps**: 0.1 dB a detent within 2 dB of ignition, 0.5 within 6 dB, 1 dB beyond, rounded to 0.1 dB.
- **Page knobs**: 0.01 a detent, 0.03 when 3 or more detents arrive in one frame (a fast turn). Discrete values
  (scale, root, curve, seed) step by one.
- **Pitch**: the root is C2 + Root (E2 by default, natural minor), OCT± is ±2 octaves, and a note outside 16–72 is
  folded by octaves. ALGO with no key held moves the drone a semitone per detent. When the last key is released,
  the string stays on its note and ALGO moves it from there. Fine tune is ±100 cents, 1 cent per detent.
- **The key feed**: noise up to −12 dBFS into the string at full velocity × Depth (squared). The latch (ARP) keeps
  the feed level the key had when it was let go.
- **PLAY**: held = Starve. Two short taps (each under 250 ms, the second within 350 ms of the first) latch it, and the
  PLAY light turns red. A tap lets go.
- **Recall (EDIT+SAVE)**: the 24 continuous values glide over 1 s (smoothstep); scale, root, octave,
  curve, sense and feed mode jump. Touching any control during the glide ends it there.
- **Autosave: an append-only log that never erases while it plays.** fm1-base's store erases a 4 KiB flash
  sector on every save, with interrupts off for tens of ms, longer than an audio half (2.9 ms). The device confirmed
  it: one xrun, and that xrun was a save (`XRUN 1 AT 1:54 SAVE 1`, with the longest render at 0.85 ms). So the last
  state goes into a log in the data area: two sectors (0x97000, 0x98000), 32 slots of 128 bytes each. An autosave
  writes the next erased slot, one flash page, and fm1-base holds interrupts off for one page at a time (typically
  well under a millisecond, 3 ms at worst). Each record carries a sequence number and a checksum, and the newest
  valid one wins at boot. The boot, silent anyway, erases the sector that doesn't hold the newest record, so a
  session has 32 autosaves before it needs an erase. Past that, the erase waits until the output is below −60 dBFS
  (for example, Starve). The autosave still waits until the controls have been still for 4 s. A state saved by an
  older build in fm1-base's store (`OBJ_AUTO`) is read once if the log is empty, and is the fallback if a log write
  ever fails.
- **SAVE (the snapshot) still uses fm1-base's store**, so pressing it costs one short silence and one xrun. It's
  deliberate and rare. Setup counts it under `SAVE`.
- **Boot**: the lines are empty. Starve, Freeze and Latch are off. The feedback is the saved value or 6 dB below
  the threshold, whichever is lower. The rest of the saved state comes back as it was.
- **Screen**: the header (the name, the note, the page; a message for 1.3 s after a change), then the gain-margin meter
  (−24 to +12 dB, ignition the white line, the set margin a tick under the bar, so Starve shows as the bar falling
  away from it), then the feedback, the switches, Pressure, and the output scope (a point every 8 frames, 44 ms across,
  with the feed level at the right edge), then the page's four names and values. The meter and scope redraw every
  other frame (about 30 a second), and the header and the page when they change.
- **Lights**: the current page's button, PLAY (red when latched) for Starve, REC for Freeze, ARP for Latch, and held
  buttons. The 16 playing keys and A#4 glow at fm1-base's mid level, and a held key is lit.

### Build and repository

- **Layout**: CTL-1's convention: fm1-base as the submodule `base/` (pinned at `2d8ed74`), the app in `app/`, and
  `./build.sh` passing its arguments to `base/tools/build.py app`.
- **Package identity** `FM-1_74`, USB names "Trifid" / "TRIFID FM-1". The store: the autosave in fm1-base's settings
  pair at 0xFC000, the panel calibration at 0xDC000, the snapshot at 0xDE000.
- **`APP_BLOCK` = 128**: the controls update once per 128-frame block, as the PRD asks. With fm1-base's USB audio,
  the ring's start target is 2 × APP_BLOCK, which is 5.8 ms.
- **Trifid's own files are MIT (flag: the owner's call).** The PRD asks only that the notices be kept. MIT matches
  the concept's licence. fm1-base allows any licence for an app.
- **Fonts and palette**: fm1-base's (RED palette, Barlow), no fonts of Trifid's own.

## Conflicts with fm1-base, and where the PRD differs from it

Where fm1-base and the PRD disagree, fm1-base wins (as instructed). None of these blocks anything.

1. **Sample rate.** fm1-base's `device/main_fm1.c` names `FS 44100u`. felucca-bsp's `usb.c` says the I2S clock is
   about 44,117.6 Hz, which matches the PRD's 44,118. Trifid's DSP uses 44,118 Hz (`TF_FS`). The host simulator
   writes its WAVs labelled 44,100 Hz, so a rendered file plays 0.7 cents flat. Nothing to change.
2. **"96 KiB for variables and stacks".** In fm1-base's linker map (`device/app.ld`), the 96 KiB RAM region holds
   `.data` and `.bss` only. The stacks have their own region above the pool (`_ustack` 24 KB, `_sstack` 8 KB). Trifid
   uses about 28 KB of the 96 (`docs/ram.md`).
3. **"24 KiB of RAM code".** `app.ld`'s RAMTEXT region is 32 KiB (0x8000). Its comment says the last 8 KiB were
   freed for LOOM-8 on 2026-10-19. Trifid puts nothing in `.ram_text`: its DSP runs from flash (XIP) as o2 units, as
   CTL-1's does.
4. **Store saves silence the audio.** That's not in the PRD; it shapes the autosave (above).
5. **"USB MIDI on"** needs nothing: USB MIDI is always on in fm1-base. `usb_audio_in`/`usb_audio_out` are written out
   in `app/fm1.toml` even though they're fm1-base's defaults.
6. **`o2` "for the DSP unit"**: fm1-base's `o2` is a list of units. Trifid lists `dsp/*.c` (plant, verb, glitter, echo, out,
   engine), each at −O2. The per-sample code is `static inline` in the module headers and chained in `engine.c`, so
   the hot loop is one unit.
7. **The keys' sensing** is fm1-base's `lib/touch` (`APP_TOUCH`): the 20 analog-row keys F3–C5 have velocity and
   pressure, and the top row (C#5–G5) has a velocity probe only. Trifid uses velocity alone, so every key behaves the
   same (pressure is read but unused).
