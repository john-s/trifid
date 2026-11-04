# USB audio as a feed source: investigation

**Question.** Can fm1-base (at `2d8ed74`) or felucca-bsp route the computer's USB audio into Trifid's app, so it
can excite the loop as a feed source? What would it cost in RAM and CPU?

**Short answer.** Not through any interface fm1-base offers today. The computer's audio is mixed into the output
*after* the app's render, and the app never sees it. The cost of the audio itself is already paid: about 8.4 KB of
RAM for the ring, a few flops a sample, no pool. Routing it into the loop would cost almost nothing more. The work is
a small API change in fm1-base and felucca-bsp. **Recommendation: cut it from the MVP.** If it comes back in v1.x,
do it as an upstream fm1-base API (below), not by reaching into felucca-bsp's internals.

## How the audio moves today

Measured on the source at the pinned commit: `base/device/main_fm1.c`, `base/device/uac_mix.h` and
`base/bsp/chip/usb.c`.

- With `usb_audio_out = true` (fm1-base's default, and Trifid's `fm1.toml`), felucca-bsp's `usb.c` exposes an
  isochronous OUT endpoint (EP4, 16-bit stereo at 44.1 kHz, adaptive). `uo_service()` copies each packet into
  `uo_ring` (2,048 frames of `L | R << 16`, 8 KB), from the TIMER5 interrupt.
- `render_block()` in `main_fm1.c` calls `app_render()` first. Then it calls `uac_mix()`, which runs:
  1. `uac_tap()`: the USB *recording* takes the app's output on its own;
  2. `uac_play()`: reads the ring, resamples it from 44.1 kHz to the I2S clock (about 44,117.6 Hz, with the rate
     steered by the ring's fill), and **adds** it to the output at `plat_usb_play_gain` × MASTER².
- `plat.h` gives the app one control over this, `plat_usb_play_gain(q12)`: the level, with 0 muting it. Even when
  muted, `uac_play` keeps reading the ring so its rate tracking stays locked. Nothing in `plat.h` hands those
  samples to the app.

So the computer's audio only ever reaches the speaker and headphones, and only after the limiter and the render.
Trifid can't feed it into the loop without changing the platform.

## The ways it could be done

1. **An fm1-base API (recommended if it's wanted).** Add something like
   `uint32_t plat_usb_play_take(float *lr, uint32_t n)` to `plat.h`. Called from `app_render`, it would return the
   next `n` resampled frames (the same resampler, the same rate steering), consume them, and tell `uac_mix` not to
   add them to the output for that block. The change lives in felucca-bsp's `usb.c` (the resampler split from the
   mix) and fm1-base's `uac_mix.h` / `plat_fm1.c` / `plat_host.c`. Both are MPL-2.0, so the change gets published
   under MPL-2.0. That's fine for Trifid, and the natural home is a PR to fm1-base.
2. **Peek at the ring from the app (works, not recommended).** `usb.c` is compiled into the same unity unit as
   `app.c` (`device/unity.c` includes the platform, then the app), so `uo_ring`, `uo_r`, `uo_w` and `uo` are in scope
   for the app. With `plat_usb_play_gain(0)`, the app could read the frames `uac_play` is about to consume at
   `uo_r`, with the same step. It needs no platform change, but it depends on felucca-bsp's private names and its
   resampler's state. Any bump of the submodule could silently break it, and the host simulator and emulator have
   no such ring, so it can't be tested off the device.
3. **A fork of fm1-base.** Same as option 1 without upstreaming. It costs maintenance on every fm1-base update.

## What it would cost

| | RAM | Pool | CPU (one core, 240 MHz) |
| --- | ---: | ---: | --- |
| USB audio out as it is now (the ring, the endpoint buffer) | 8,433 B | 0 | `uac_play`: about 12 flops a frame, already spent |
| Routing it into the feed (option 1) | ~0 (a `float` pair a frame, on the stack, or the existing ring) | 0 | about the same: the resampler moves; the feed mixer gets one more add a sample |
| Turning USB audio out off (`usb_audio_out = false`) | −8,433 B | 0 | −`uac_play` |
| Turning all USB audio off (`usb_audio_in = false`) | −10,746 B | 0 | −`uac_tap` and `uac_play` |

The RAM figures come from `tools/ram_report.py` (`docs/ram.md`), which builds the unity with and without
`FM1B_UAC_OUT` and `FM1B_UAC` and diffs the results. The rings are in RAM, not the pool (fm1-base's
`FM1B_UAC_RING_POOL` would move the input's ring there). RAM isn't Trifid's tight budget: it uses about 28 KB of
96 KB. The pool is the tight one, and USB audio takes none of it.

Latency: the ring aims to start each render with `2 × APP_BLOCK` frames in hand (256 frames, 5.8 ms with
Trifid's 128-frame blocks), plus the computer's own output buffer.

## Things to weigh before building it

- **The loop amplifies its feed.** Below ignition, the string rings the feed by up to +19 dB at its pitch. Above
  ignition, any feed starts the loop. A computer's program audio at line level would drive the loop hard. It needs
  its own level control, near the seed level's range (−90 dBFS) rather than unity, plus a way to turn it off.
- **What the owner hears.** With the feed routed, the computer's audio would not also play directly (`uac_mix`
  would skip it), so the "play through the FM-1" use goes away while it's routed.
- **USB recording is separate.** Recording the FM-1 over USB (`usb_audio_in`, the tap) isn't affected by any of
  this, and it's worth keeping on: it's how to capture Trifid without an interface.
- **macOS.** fm1-base's default `usb_midi_last = true` already orders the interfaces so macOS 13–15 show the
  audio device. Nothing to change.

## Recommendation

Cut it from the MVP, as the owner suspects:

- **Keep `usb_audio_in = true`**, to record Trifid over the cable.
- **Keep `usb_audio_out = true` for now.** It costs only RAM, which is plentiful, and no pool. If RAM ever gets
  tight, it's the first 8.4 KB to give back (`usb_audio_out = false` in `app/fm1.toml`).
- If the feed source is wanted later, propose `plat_usb_play_take` (option 1) to fm1-base. In Trifid it becomes a
  fifth feed source next to the `ext` hook the engine already has (`tf_engine_render(..., ext)`), with a level knob
  on the Setup page.
