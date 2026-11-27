/*
 * frank-sdk — keyboard, mouse and gamepads, whatever the board has.
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * SPDX-License-Identifier: MIT
 *
 * Sources, merged:
 *   USB host   TinyUSB on the native port (FRANK_USB_HOST=native) or on PIO-USB
 *              (=pio: Z0's second socket, FZ's J7, M1/M2 with an adapter),
 *              through a hub: keyboards, mice, HID and XInput gamepads
 *              (frank-war2/castle's driver: vendor layouts, a report parser);
 *   PS/2       keyboard and mouse on one PIO (frank-dos); the keyboard's set-2
 *              codes come out as HID usage codes, like a USB keyboard's;
 *   NES/SNES   up to two pads on the shared latch/clock (frank-dos).
 * Keys are HID usage codes (class/hid/hid.h HID_KEY_*). Gamepad buttons are the
 * FRANK_PAD_* bits below, the same for every kind of pad.
 *
 * USB host on the native port leaves no USB console: build with the UART
 * console (-uart), or put the host on PIO-USB and keep the CDC console
 * (frank-dos's dual-role usb_console). Call frank_input_poll() from the main
 * loop on core 0: TinyUSB must never be entered from core 1 (it toggles the USB
 * interrupt on the calling core and took the HDMI IRQ down once).
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FRANK_PAD_UP     0x00001u
#define FRANK_PAD_DOWN   0x00002u
#define FRANK_PAD_LEFT   0x00004u
#define FRANK_PAD_RIGHT  0x00008u
#define FRANK_PAD_SOUTH  0x00010u   /* A on Xbox, B on Nintendo, cross */
#define FRANK_PAD_EAST   0x00020u
#define FRANK_PAD_WEST   0x00040u
#define FRANK_PAD_NORTH  0x00080u
#define FRANK_PAD_L1     0x00100u
#define FRANK_PAD_R1     0x00200u
#define FRANK_PAD_L2     0x00400u
#define FRANK_PAD_R2     0x00800u
#define FRANK_PAD_START  0x01000u
#define FRANK_PAD_SELECT 0x02000u
#define FRANK_PAD_L3     0x04000u
#define FRANK_PAD_R3     0x08000u
#define FRANK_PAD_HOME   0x10000u

typedef struct {
    uint8_t key;        /* HID usage code */
    bool down;
    uint8_t modifiers;  /* HID modifier bits after this event */
} frank_key_t;

void frank_input_init(void);
/* From the main loop (core 0), every pass or frame. */
void frank_input_poll(void);

/* The next key event; false when none is queued. */
bool frank_input_key(frank_key_t *ev);
/* Mouse motion since the last call, the wheel, the buttons (bit 0 left,
 * 1 right, 2 middle). False when no mouse moved or clicked. */
bool frank_input_mouse(int *dx, int *dy, int *wheel, uint8_t *buttons);
/* Pad 0: the first USB pad or the first NES pad, merged; pad 1: the second. */
uint32_t frank_input_pad(int n);
/* Pad 0's analog sticks: LX LY RX RY, -127..127, +x right, +y down (0 when
 * centred, or the pad has none: NES pads, D-pad-only USB pads). Pad 1: 0. */
void frank_input_pad_axes(int n, int8_t axes[4]);
/* A USB gamepad is connected (a NES port cannot tell). */
bool frank_input_pad_connected(void);
/* The same buttons by source, for firmware that maps them differently: USB
 * pads only, or NES/SNES port n only (B = SOUTH, A = EAST, Y = WEST, X = NORTH). */
uint32_t frank_input_pad_usb(void);
uint32_t frank_input_pad_nes(int n);

/* "keys" console command: what is connected, the last events */
void frank_input_report(void);

#ifdef __cplusplus
}
#endif
