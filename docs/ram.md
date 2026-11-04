# Trifid: static RAM report

From `tools/ram_report.py`: the firmware's own sources (fm1-base's `device/unity.c` with Trifid's app, and the o2 DSP units) compiled with the device build's configuration for a 32-bit target, every object's size read from the output. Exact sizes; alignment padding and the code are the real link's (`./build.sh` prints those).

## Totals

| Region | Used | Limit | Left |
| --- | ---: | ---: | ---: |
| RAM (.data + .bss) | 28,615 | 98,304 | 69,689 |
| .pool | 317,536 | 344,064 | 26,528 (fm1-base insists on 8,192 spare) |
| RAM, the device build's link (`app/build/trifid.elf`) | 28,756 | 98,304 | 69,548 |
| .pool, the device build's link | 317,536 | 344,064 | 26,528 |
| .ram_text (the platform's RAM code) | 3,132 | 32,768 | 29,636 |

The firmware image is 128,272 B (the app slot: 581 KB).

Verdict: **fits** (pool headroom 26,528 B, RAM headroom 69,689 B).

## .pool against the budget (docs/PRD.md)

| Item | Budget | Measured |
| --- | ---: | ---: |
| fm1-base spare | 8,192 | (kept free) |
| screen canvas (240x144x2) | 69,120 | 69,120 |
| strings 2x9,216 | 18,432 | 18,432 |
| body delays 2x18,432 | 36,864 | 36,864 |
| Rot | 16,384 | 16,384 |
| echo 4 s stereo | 176,472 | 176,472 |
| everything else in the pool | 0 | 264 |
| free (after the spare) | ≈18,600 | 18,336 |

Pool objects:

| Object | Bytes |
| --- | ---: |
| `echo_buf` | 176,472 |
| `cv_px` | 69,120 |
| `body_buf` | 36,864 |
| `str_buf` | 18,432 |
| `rot_buf` | 16,384 |
| `ann` | 264 |

## RAM (.data + .bss), the largest

| Object | Section | Bytes |
| --- | --- | ---: |
| `uo_ring` | bss | 8,192 |
| `st_buf` | bss | 3,840 |
| `abuf` | bss | 2,048 |
| `ua_ring` | bss | 2,048 |
| `sn_key` | bss | 1,944 |
| `eng` | bss | 1,392 |
| `ota_stage.hdr` | bss | 1,024 |
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
| `fm1_in_stat` | bss | 236 |
| `live` | bss | 204 |
| (158 more) | | 2,817 |

## USB audio's cost

| Build | RAM | Pool |
| --- | ---: | ---: |
| as configured (USB audio in and out) | 28,615 | 317,536 |
| without the computer's audio (`usb_audio_out = false`) | 20,182 | 317,536 |
| without USB audio (`usb_audio_in = false`) | 17,869 | 317,536 |

USB audio costs 10,746 B of RAM and 0 B of pool in all, 8,433 B of RAM of it for the computer's audio (out). Its rings are in RAM by default (fm1-base `FM1B_UAC_RING_POOL` would move them to the pool).

