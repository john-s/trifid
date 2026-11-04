/* SPDX-License-Identifier: MIT
 * Copyright (C) 2026 john-s */
/* Trifid: the app's half of the contract with fm1-base (device/unity.c, host/plat_host.c). */
#pragma once
#include <stdint.h>
#include "plat.h"

#define APP_NAME "TRIFID"
#define APP_TAGLINE "feedback noise"
#define APP_SPLASH_RGB RGB(232, 76, 60)
#define APP_PALETTE 3u                   /* gfx.c's RED */
#define APP_BLOCK 128u                   /* frames a render: the controls move once a block (2.9 ms) */
#define APP_TOUCH                        /* velocity and pressure from the keys (lib/touch.h) */

/* the store: the last state (autosaved), the panel calibration (the platform's), the one snapshot (SAVE) */
enum { OBJ_AUTO, OBJ_PANEL, OBJ_SNAP, OBJ_NOBJ };
#define APP_STORE_MAP {0xFC000u, 0xDC000u, 0xDE000u}

/* the contract: the platform calls these */
void app_init(void);                     /* boot: the engine, then the last state (quiet: below ignition) */
void app_render(int32_t *lr, uint32_t n, uint32_t master_q12);   /* audio interrupt: n stereo frames, 24-bit */
void app_panic(void);                    /* silence now (before an update rewrites the flash) */
void app_ui_init(void);                  /* the first screen, after the splash */
void app_frame(void);                    /* the UI: input and a redraw, about 60 times a second */
void app_input_only(void);               /* between frames: input only */
void app_say(const char *a, const char *b);   /* a short message on the screen and to a screen reader */

/* the host simulator's checks (host/plat_host.c "expect"): see app/host.c */
#define APP_HOST_EXPECT
int app_expect(const char *what, int *got);
#define APP_HOST_REBOOT
void app_host_reboot(void);
