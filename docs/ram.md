# Trifid: static RAM report

From `tools/ram_report.py`: the firmware's own sources (fm1-base's `device/unity.c` with Trifid's app, and the o2 DSP units) compiled with the device build's configuration for a 32-bit target, every object's size read from the output. Exact sizes; alignment padding and the code are the real link's (`./build.sh` prints those).

## Totals

| Region | Used | Limit | Left |
| --- | ---: | ---: | ---: |
| RAM (.data + .bss) | 28,303 | 98,304 | 70,001 |
| .pool | 325,952 | 344,064 | 18,112 (fm1-base insists on 8,192 spare) |
| RAM, the device build's link (`app/build/trifid.elf`) | 28,388 | 98,304 | 69,916 |
| .pool, the device build's link | 325,952 | 344,064 | 18,112 |
| .ram_text (the platform's RAM code) | 9,296 | 32,768 | 23,472 |

The firmware image is 128,332 B (the app slot: 581 KB).

Verdict: **fits** (pool headroom 18,112 B, RAM headroom 70,001 B).

## .pool against the budget (docs/PRD.md)

| Item | Budget | Measured |
| --- | ---: | ---: |
| fm1-base spare | 8,192 | (kept free) |
| screen canvas (240x144x2) | 69,120 | 69,120 |
| strings 2x9,216 | 18,432 | 18,432 |
| body delays 2x18,432 | 36,864 | 36,864 |
| reverb (was Rot, 16,384) | 24,800 | 24,800 |
| echo 4 s stereo | 176,472 | 176,472 |
| everything else in the pool | 0 | 264 |
| free (after the spare) | ≈18,600 | 9,920 |

Pool objects:

| Object | Bytes |
| --- | ---: |
| `echo_buf` | 176,472 |
| `cv_px` | 69,120 |
| `body_buf` | 36,864 |
| `verb_buf` | 24,800 |
| `str_buf` | 18,432 |
| `ann` | 264 |

## RAM (.data + .bss), the largest

| Object | Section | Bytes |
| --- | --- | ---: |
| `uo_ring` | bss | 8,192 |
| `st_buf` | bss | 3,840 |
| `abuf` | bss | 2,048 |
| `ua_ring` | bss | 2,048 |
| `sn_key` | bss | 1,944 |
| `ota_stage.hdr` | bss | 1,024 |
| `eng` | bss | 1,016 |
| `ota_dec` | bss | 640 |
| `sx_frame` | bss | 640 |
| `app_render.l` | bss | 512 |
| `app_render.r` | bss | 512 |
| `sx_out_q` | bss | 512 |
| `ota_stage.b` | bss | 512 |
| `lcd_fillbuf` | bss | 480 |
| `fm1_in` | bss | 280 |
| `sn_probe` | bss | 270 |
| `midi_in_q` | bss | 256 |
| `midi_out_q` | bss | 256 |
| `live` | bss | 236 |
| `fm1_in_stat` | bss | 236 |
| (159 more) | | 2,849 |

## USB audio's cost

| Build | RAM | Pool |
| --- | ---: | ---: |
| as configured (USB audio in and out) | 28,303 | 325,952 |
| without the computer's audio (`usb_audio_out = false`) | 19,870 | 325,952 |
| without USB audio (`usb_audio_in = false`) | 17,557 | 325,952 |

USB audio costs 10,746 B of RAM and 0 B of pool in all, 8,433 B of RAM of it for the computer's audio (out). Its rings are in RAM by default (fm1-base `FM1B_UAC_RING_POOL` would move them to the pool).

