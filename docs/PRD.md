# Trifid: product requirements (MVP)

This is the spec Trifid's MVP is built to, with the owner's decisions applied. Where it and the code disagree, this
document is the source of truth; where fm1-base disagrees with it, fm1-base wins and the conflict is recorded in
[DECISIONS.md](DECISIONS.md).

Trifid is a personal custom firmware for the M-VAVE FM-1 (JieLi AC791N synth with 27 keys, 240 px colour screen,
endless encoders). It turns the FM-1 into an industrial feedback-noise instrument: a Karplus-Strong string inside a
feedback loop, excited by noise, an idea borrowed from Synthux Academy's Audrey II.

## Setup

- A repo with fm1-base (https://github.com/charlesvestal/fm1-base) as a git submodule pinned at commit `2d8ed74`
  (1 Nov 2026). Trifid's own code lives outside the submodule. Follow fm1-base's conventions (its README, template
  app, `plat.h`, `lib/touch.h` and `fm1.toml` docs).
- C, not C++. DSP modules make no platform calls, so they compile natively for unit tests and A/B renders.
- `fm1.toml`: `ble = false`, no WiFi, USB MIDI and USB audio on, `o2` for the DSP unit. `#define APP_TOUCH`. cpu2
  off (the second plant is a stretch goal).
- Device builds need the JieLi clang 4.0.1 pi32v2 toolchain. Keep the device build config correct, and document in
  `README.md` how the owner builds and flashes on macOS with fm1-base's Docker setup.
- An initial commit on `main`; the MVP on a branch, with a PR to `main`.

## Done looks like

1. The host simulator and the browser emulator both build and run Trifid (the same app on both).
2. Native unit tests for the DSP modules pass: the loop stays silent below ignition and self-excites above it, the
   limiter can never be exceeded, Starve cuts the gain, Grit at 0 is clean, there are no NaN/denormal blowups, and
   pitch tracking holds across MIDI 16–72.
3. A static RAM report from the build, checked against the `.pool` budget below.
4. `docs/usb-audio.md`, an investigation only (no implementation). Can fm1-base or felucca-bsp route the computer's
   USB audio into the app as a feed source? What would it cost in RAM and CPU? Give a recommendation. The owner
   suspects this will be cut.
5. A PR whose description lists what was verified, plus what only a device can verify: RAM fit on the real linker
   map, CPU under 50%, zero xruns in 10 minutes, and how key pressure feels.

## Sound engine

One feedback loop, fed from inside, with corrosion around a clean core. The loop runs per sample at 44,118 Hz, and
controls update once per 128-frame block. The core is float at full rate; only storage is cut down.

- **Feed mixer**: sums the feed sources into the string input. A noise seed is always on at about −90 dBFS.
- **String**: Karplus-Strong delay with a Hermite read, DC block and one-pole damping (written fresh, not from
  DaisySP). Range MIDI 16–72, a 2,304-sample line.
- **Drive**: soft-clip overdrive, about 0.4 by default. Grit can add a wavefold stage.
- **Loop filters**: 12 dB LPF (100 Hz–18 kHz) and HPF (10 Hz–4 kHz).
- **Rot reverb** (in-loop, crossfaded by Mix, 0 = bypass): one int16 buffer of 8,192 samples at fs/4. 3–4 read taps
  at prime offsets are summed back into the write, with a 20–80 sample allpass in its loop and 1–3 samples of slow
  tap jitter. The decimation filter scales with Grit: a one-pole pre-filter at low Grit, and zero-order hold with no
  filter at full Grit. Store with rounding and light dither; coarser quantization is allowed at high Grit.
- **Body delay**: 1–100 ms back into the feed mixer, with the right channel 4 samples shorter.
- **Feedback gain**: −60 to +12 dB. This sets whether the loop self-excites.
- **Tape echo** (after the loop): stereo, int16 at fs/4, **4 s**. An 800 Hz bandpass and soft clip in its feedback,
  which may exceed 1×. Wow and flutter from slow pointer jitter. Wear (Grit-linked) adds hiss, dropouts and bit loss.
  Freeze holds the buffer as a loop. Return feeds the echo output back into the feed mixer.
- **Feed sources in the MVP**: noise seed, pressure feed, velocity impulse (a short noise burst), echo return. Metal
  impulses from flash, FM bursts and FM cross-mod are v1.x. Leave hooks, don't build them.
- **Starve**: a fast, smoothed cut of feedback gain to the floor, held or latched.
- **Output**: the Grit macro scales Rot decimation and quantization, echo Wear and the wavefold. **Grit at 0 is fully
  clean**: proper anti-alias filtering and dither, no Wear, no fold. Speaker mode band-limits and saturates for the
  built-in driver; Line mode is flat. A hard limiter comes after everything and is never bypassed. MASTER is the
  hardware pot after the limiter. Boot is always quiet and safe.

## Keys (decided; differs from the original PRD)

Of the 16 white and 11 black keys, the 20 keys F3–C5 sense velocity and pressure. C#5–G5 have velocity only, through
a probe that compresses firm presses.

- All **16 white keys F3–G5** play the chosen scale (changed 2026-11-04: it was the 12 white keys F3–C5). Each key
  is the next scale degree up from the root, with glide. OCT− and OCT+ shift by an octave. The string is monophonic:
  the last-held key wins. The top four (D5–G5) have no pressure, so they feed from their strike velocity, held until
  release, as in velocity mode.
- **Pressure** on the held white key raises **only the noise feed level**, not the feedback gain. The gain belongs to
  the Feedback encoder. Release stops feeding.
- **Velocity** on a white key fires an impulse at that strength.
- **A#4** (the black key labeled GLO) re-strikes the held note. Velocity sets the impulse strength, the pitch doesn't
  change, and its pressure is unused.
- Every other black key is **reserved and does nothing**.
- Pressure fallback (pressure sensing is still experimental on this hardware): a Setup option switches feeding to
  velocity mode, where strike velocity sets a feed level held until release. ARP latch works in both modes.

## Controls

Fixed encoders: SELECT = Feedback (the ignition control; fine/coarse speed near the threshold; momentum is v1.x).
ALGORITHM = fine tune, or pitch when no key is held. PRESETS = Grit. MASTER = volume pot.

Pages, as button: KNOB 1 / 2 / 3 / 4:

- HOME (Plant): Body / Drive / LPF / HPF
- FX (Rot): Mix / Decay / Smear / Jitter
- SEQ (Echo): Send (EDIT-shift: Return) / Time / Feedback / Wear
- SEL (Keys): Scale / Root / Glide / Pressure depth. About 12 scales: major, natural minor, dorian, phrygian, lydian,
  mixolydian, locrian, harmonic minor, major and minor pentatonic, whole tone, chromatic.
- GLO (Setup): Output mode (Speaker/Line) / Velocity curve / Pressure sense (EDIT-shift: feed mode
  pressure/velocity) / Seed level

Buttons: PLAY = Starve (hold to cut, double-tap to latch). REC = freeze the echo. ARP = latch feed (the last key
keeps feeding after release). ENV / LFO = hold to step the echo time ×0.5 / ×2. EDIT = shift. SAVE = save the one
snapshot. EDIT+SAVE = recall the snapshot, gliding all values to it over about 1 s. Autosave always keeps the last
state.

Screen: the main view is a gain-margin meter showing how close the loop is to ignition, above an output scope. The
current page's four values and names run along the bottom. No menus deeper than one level.

## Platform and budgets

AC791N (WL82), two cores, single-precision FPU, about 240 MHz. No external RAM. fm1-base's linker gives a 336 KiB
`.pool`, 96 KiB for variables and stacks, and 24 KiB of RAM code. Stereo 24-bit out at 44,118 Hz, no audio input,
1 MiB flash. Core 1 misreads RAM on some units, so the second plant is never required.

`.pool` budget in bytes:

| Item | Bytes |
| --- | ---: |
| pool | 344,064 |
| fm1-base spare | 8,192 |
| screen canvas (240×144×2) | 69,120 |
| strings 2×9,216 | 18,432 |
| body delays 2×18,432 | 36,864 |
| Rot | 16,384 |
| echo 4 s stereo | 176,472 |
| free | ≈18,600 |

USB audio stays on for now: measure its RAM cost if you can and report it.

CPU target: one plant under 50% of one core at 240 MHz (`plat_cpu_pct`). Device only, but keep the inner loop lean.

## Modules

`dsp/plant` (string, drive, filters, body delay, feed mixer, feedback gain), `dsp/rot`, `dsp/echo`, `dsp/out` (Grit
mapping, Speaker/Line, limiter), `ui/` (pages, screen, encoders, keys and pressure), `app.c` (app_init, app_render,
app_frame, saved state).

## Licensing

Personal use, but keep the notices. Concept credit: Audrey II by Synthux Academy (Nick Donaldson / Infrasonic Audio,
Roey Tsemah), MIT; any code taken from it keeps its MIT notice. Don't use DaisySP-LGPL code (ReverbSc, Tone).
fm1-base and felucca-bsp are MPL-2.0. Keep the JieLi AC79 SDK Apache-2.0 notices.

## References

Synthux-Academy/Audrey-II, charlesvestal/fm1-base, hugelton/felucca-bsp, charlesvestal/fm1-velocity (key pressure
lab notes), jadamsowers/fm1-fimba (int16 decimated delay precedent), joakimeriksson/fm1-emu (emulator, clock and
core-1 notes).
