# Trifid

A personal custom firmware for the M-VAVE FM-1 that turns it into an industrial feedback-noise instrument: a
Karplus-Strong string inside a feedback loop, excited by noise from inside, with corrosion around a clean core. The
idea is borrowed from Synthux Academy's [Audrey II](https://github.com/Synthux-Academy/Audrey-II). It is built on
[fm1-base](https://github.com/charlesvestal/fm1-base) (a git submodule in `base/`, pinned at `2d8ed74`).

- The spec: [docs/PRD.md](docs/PRD.md). What was decided, and where it differs from Audrey II or fm1-base:
  [docs/DECISIONS.md](docs/DECISIONS.md).
- RAM: [docs/ram.md](docs/ram.md) (the static report against the pool budget).
- USB audio as a feed source: [docs/usb-audio.md](docs/usb-audio.md) (an investigation).
- On the FM-1: [docs/device-results.md](docs/device-results.md) (CPU peak 27%, zero xruns in 10 minutes).

## Playing it

The loop is silent until SELECT (Feedback) takes it past ignition. The meter on the screen shows how far above or
below ignition it is (the white line is 0 dB). Under it is the output's scope.

| Control | What |
| --- | --- |
| SELECT | Feedback: −60 to +12 dB, 0.1 dB a detent near ignition |
| ALGORITHM | the pitch (a semitone a detent) when no key is held; fine tune while one is |
| PRESETS | Grit: corrosion. Rot's aliasing and coarse steps (a crunch that follows the signal), the echo's Wear, and past a third, a wavefold in the loop. 0 is clean |
| MASTER | the volume, after the limiter |
| White keys F3–G5 | the scale, one degree each up from the root; the last held wins, with glide. Velocity strikes, and sets the noise feed, held until release |
| A#4 | strike the held note again |
| HOME / FX / SEQ / SEL / GLO | pages: Plant (Body, Drive, LPF, HPF), Rot (Mix, Decay, Smear, Jitter), Echo (Send / EDIT: Return, Time, Feedback, Wear), Keys (Scale, Root, Glide, Depth), Setup (Velocity curve, Seed; the CPU and xrun figures) |
| PLAY | Starve: hold to cut the loop; double-tap to latch, tap to let go |
| REC | freeze the echo |
| ARP | latch the feed: the last key keeps feeding after release |
| ENV / LFO | hold: the echo time ×0.5 / ×2 |
| SAVE / EDIT+SAVE | save the snapshot / recall it, every value gliding over a second |
| OCT− / OCT+ | an octave down / up |

The last state is saved by itself. Boot is quiet: the feedback starts at least 6 dB below ignition.

## Layout

| Path | What |
| --- | --- |
| `app/fm1.toml` | the build settings (fm1-base's `tools/build.py`) |
| `app/app.h`, `app/app.c` | the contract with fm1-base, the engine's pool buffers, the render, the saved state |
| `app/ui/` | `keys.c` (scale keys, velocity feed), `pages.c` (encoders, pages, buttons), `screen.c` (the meter, the scope) |
| `app/dsp/` | the DSP, plain C with no platform calls: `plant` (string, drive, filters, body delay, feed mixer, feedback gain), `rot`, `echo`, `out` (Grit map, limiter), `engine` (the chain), `dsp_util.h` |
| `app/host.c` | the host simulator's checks |
| `tests/` | `dsp/` (native DSP tests, renders, a benchmark), `scenarios/` (the whole app in the simulator), `run_tests.sh` |
| `tools/ram_report.py` | the static RAM report |

## Building and flashing on macOS

The device build uses JieLi's Linux x86-64 toolchain (clang 4.0.1 for pi32v2). On a Mac, fm1-base's build runs each
tool in a `linux/amd64` Docker container, through Rosetta on Apple silicon.

**Once:**

1. Docker Desktop (or podman, with a `docker` script that runs `exec podman "$@"` on your PATH). Keep this checkout
   somewhere the container can share, such as under `/Users`.
2. Python 3 with Pillow (Homebrew's Python, for the fonts):

   ```bash
   pip3 install Pillow
   ```

3. The repository, with fm1-base and felucca-bsp:

   ```bash
   git clone --recursive https://github.com/john-s/trifid
   ```

   For a checkout made without `--recursive`:

   ```bash
   git submodule update --init --recursive
   ```

4. JieLi's toolchain, into `~/.jieli/toolchain` (fm1-base's script downloads it from JieLi's package server):

   ```bash
   base/tools/get_toolchain.sh
   ```

5. The JieLi AC79 SDK (Apache-2.0). The package takes three files from it; Trifid links none of its libraries:

   ```bash
   git clone --depth 1 --branch AC79NN_SDK_V1.2.1_2023-12-13 https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK.git ~/fw-AC79_AIoT_SDK
   ```

**Build:**

```bash
./build.sh
```

That makes `app/build/trifid.fwsc` (the installable package), `app/build/trifid.bin` (the app) and
`app/build/loader/ota.bin`. On macOS the build uses Docker by itself; `JIELI_DOCKER=1` forces it on Linux too.
`JIELI_TOOLCHAIN` and `AC79_SDK` override the default paths. The build checks the image, RAM and pool (it fails with
under 8 KiB of pool spare), that no register access is outside felucca-bsp, and that no double-precision code is
linked. Its output includes `RAM .data+.bss N B of 98304; pool N B of 344064`: compare that with
[docs/ram.md](docs/ram.md).

For a release (identity `FM-1_74XXYYZZ`):

```bash
./build.sh --release 0.1
```

**Flash** over USB with fm1-base's installer (it needs mido with python-rtmidi). Plug the FM-1 in, switch it on,
then:

```bash
pip3 install mido python-rtmidi
```

```bash
python3 base/tools/fm1_install.py app/build/trifid.fwsc
```

It does the same update as fm1-base's web installer (`base/tools/make_site.py` builds that page for a firmware). If
an install is cut off, run it again: the FM-1 waits in update mode and the install finishes the write. To go back to
another firmware, install its package the same way.

**On the device**, `base/tools/fm1_remote.py` reads the screen and the last crash; e.g.
`python3 base/tools/fm1_remote.py listen screen log`.

## Without an FM-1

```bash
./build.sh --host
```

```bash
app/build/host/trifid_host tests/scenarios/basic.txt app/build/scenarios
```

The browser emulator needs Emscripten; serve `app/build/emu/` and open it:

```bash
./build.sh --web
```

All the tests, including the RAM report, which needs clang:

```bash
sh tests/run_tests.sh
```

`app/build/tests/render` renders the engine to a WAV for A/B listening, e.g.
`app/build/tests/render out.wav 20 note=40 margin=3 rot_mix=0.4 grit=0.3`.

## Licence

Trifid's own files are MIT. fm1-base and felucca-bsp are MPL-2.0, and the JieLi SDK files in the package are
Apache-2.0. See [LICENSING.md](LICENSING.md) and [CREDITS.md](CREDITS.md).
