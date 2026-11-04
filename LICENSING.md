# Trifid licensing

Trifid is a personal firmware. Its own files (`app/`, `tests/`, `tools/`, `docs/`, `build.sh`) are under the **MIT
licence** (`LICENSE`), but for Glitter, which is Airwindows' (below, MIT too); each source file carries an `SPDX-License-Identifier: MIT` header. Everything else keeps its
own licence and notices:

| What | Licence | Where |
| --- | --- | --- |
| The platform: fm1-base (boot, USB, storage, screen, panel, the keys' velocity and pressure, the host simulator, the browser emulator, the build and install tools) and felucca-bsp inside it, from Felucca by Leo Kuroshita (@kurogedelic), Hügelton Instruments | MPL-2.0 (file by file: a change to one of its files is published under MPL-2.0) | `base/` (a git submodule, pinned at `2d8ed74`); `base/LICENSE`, `base/LICENSING.md` |
| `fastmath.h`, the single-precision maths the DSP calls (header-only, from fm1-base) | MPL-2.0 | `base/include/fastmath.h` |
| Glitter: Airwindows' BitGlitter, ported to C in single precision, with its notice | MIT, Copyright (c) 2018 Chris Johnson (airwindows) | `app/dsp/glitter.c`, `glitter.h`; <https://github.com/airwindows/airwindows> |
| Fonts: Barlow Semi Condensed (The Barlow Project Authors), Terminus (Dimitar Toshkov Zhekov) | SIL OFL 1.1 | `base/assets/fonts/` |
| JieLi AC79 SDK: `uboot.boot`, `cfg_tool.bin`, `cfg/eq_cfg_hw.bin`, read from your SDK checkout at build time and placed in the package. Trifid links no JieLi library (no Bluetooth, no WiFi) and no SDK file is in this tree | Apache-2.0, Copyright (c) Zhuhai Jieli Technology Co., Ltd. (`base/LICENSES/Apache-2.0.txt`) | <https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK> |

## The idea

The instrument's concept (a Karplus-Strong string inside a feedback loop, fed a constant stream of inaudible noise,
with a drive, filters, a reverb and a body delay in the loop and a long saturating echo after it) is **Audrey II** by
Synthux Academy: Nick Donaldson (Infrasonic Audio) and Roey Tsemah, MIT
(<https://github.com/Synthux-Academy/Audrey-II>). **No code of Audrey II is used**: Trifid's DSP is written from
scratch in C. If any is ever taken from it, that file keeps Audrey II's MIT notice:

    Copyright (c) 2026 Synthux Academy (Audrey-II/LICENSE, MIT)

No DaisySP code is used at all (in particular none of its LGPL modules, ReverbSc and Tone). The reverb is written
fresh from the published plate topology (J. Dattorro, "Effect Design, Part 1", JAES 1997).

## Trademarks

"M-VAVE" and "FM-1" are trademarks of their respective owners, used here only to say what hardware Trifid runs on.
Trifid is independent firmware, not affiliated with, endorsed by or supported by M-VAVE, Synthux Academy or
Hügelton Instruments.
