# Device results

What the FM-1 itself showed. The Setup page (GLO) shows these figures live: `PK`, the most load since boot (the
render's share of an audio half, smoothed over ~16 blocks); `MAX`, the longest single render against its 2.9 ms
budget; `XRUN`, the late renders and when the last one came; and `SAVE`, the xruns that flash saves caused.

Each run: 10 minutes, the loop ignited, with FX (Rot), SEQ (the echo) and PRESETS (Grit) turned up.

| Date | Build | PK | MAX | XRUN | What it showed |
| --- | --- | ---: | ---: | ---: | --- |
| 2026-11-04 | `8d5944f` | 33% | — | 2 | CPU well inside the 50% target; the xruns' cause unknown |
| 2026-11-04 | `d7eb61b` | 33% | — | 1 | no `SAVE` shown (that build counted save-caused xruns separately) |
| 2026-11-04 | `795b745` | 27% | 0.85 / 2.9 ms | 1 | `AT 1:54 SAVE 1`: the xrun was a flash save (fm1-base's store erases a sector with interrupts off) |
| 2026-11-04 | `3b8baf2` | 27% | 0.86 / 2.9 ms | **0** | the autosave as an append-only log (no erase while playing) |

**PRD targets: met.** One plant is under 50% of one core at 240 MHz: a peak of 27%, and the longest render used 30%
of its budget. There were zero xruns in 10 minutes. The fixes that got there: the meter's maths moved out of the
audio interrupt, and the autosave became a log that writes one flash page and erases only at boot or when quiet
(`docs/DECISIONS.md`).

Still costs an xrun: pressing SAVE (the snapshot), which goes through fm1-base's store and erases. It's deliberate and
rare.

Still to check on the device: how the keys' pressure feels as a feed (Depth, Sense, pressure versus velocity feed
mode), the A#4 re-strike, the top four white keys' velocity feed, and the screen's frame rate.
