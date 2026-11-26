/*
 * frank-sdk — keyboard, mouse and gamepads (frank/input.h).
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * SPDX-License-Identifier: MIT
 */
#include "frank/input.h"

#include <stdio.h>

#include "frank/platform.h"
#include "hardware/clocks.h"
#include "pico/stdlib.h"

#if FRANK_USB_HOST
#include "usbhid.h"
#endif
#ifdef FRANK_HAS_PS2
#include "ps2.h"
extern bool ps2_set2_decode(uint8_t b, uint8_t *hid, bool *down);
#ifndef FRANK_PS2_MOUSE_CLK
#define FRANK_PS2_NO_MOUSE 1
#define FRANK_PS2_MOUSE_CLK FRANK_PS2_KBD_CLK   /* (not initialised) */
#define FRANK_PS2_MOUSE_DAT FRANK_PS2_KBD_DAT
#endif
#if FRANK_PS2_KBD_DAT != FRANK_PS2_KBD_CLK + 1 || FRANK_PS2_MOUSE_DAT != FRANK_PS2_MOUSE_CLK + 1
#error "frank-sdk PS/2: data must be clock + 1 (the PIO program's layout)"
#endif
#endif
#ifdef FRANK_HAS_NESPAD
#include "nespad.h"
#endif

/* ---- PS/2 keyboard events, queued with their modifiers ---- */
#define KQ 32
static frank_key_t kq[KQ];
static unsigned kq_head, kq_tail;
static uint8_t ps2_mods;
static frank_key_t last_key;
static uint32_t key_count;

static void kq_put(uint8_t key, bool down, uint8_t mods) {
    if (kq_head - kq_tail >= KQ) return;
    kq[kq_head++ % KQ] = (frank_key_t){key, down, mods};
}

static uint8_t mod_bit(uint8_t hid) {
    return hid >= 0xE0 && hid <= 0xE7 ? (uint8_t)(1u << (hid - 0xE0)) : 0;
}

void frank_input_init(void) {
#if FRANK_USB_HOST
    usbhid_init();
#endif
#ifdef FRANK_HAS_PS2
    ps2_init(PIO_INSTANCE(FRANK_PIO_PS2), FRANK_PS2_KBD_CLK, FRANK_PS2_MOUSE_CLK);
#if !FRANK_PS2_NO_MOUSE
    ps2_mouse_init_device();
#endif
#endif
#ifdef FRANK_HAS_NESPAD
    nespad_begin(clock_get_hz(clk_sys) / 1000, FRANK_NESPAD_CLK, FRANK_NESPAD_DATA, FRANK_NESPAD_LATCH);
#endif
    printf("input:%s%s%s\n",
#if FRANK_USB_HOST
           FRANK_USB_HOST == 2 ? " USB host (PIO-USB)" : " USB host",
#else
           "",
#endif
#ifdef FRANK_HAS_PS2
           " PS/2",
#else
           "",
#endif
#ifdef FRANK_HAS_NESPAD
           " NES pad"
#else
           ""
#endif
    );
}

void frank_input_poll(void) {
#if FRANK_USB_HOST
    frank_usb_poll(usbhid_task);
#endif
#ifdef FRANK_HAS_PS2
    for (int b; (b = ps2_kbd_get_byte()) >= 0;) {
        uint8_t hid;
        bool down;
        if (!ps2_set2_decode((uint8_t)b, &hid, &down)) continue;
        uint8_t m = mod_bit(hid);
        ps2_mods = down ? (ps2_mods | m) : (ps2_mods & ~m);
        kq_put(hid, down, ps2_mods);
        if (hid == 0x48) kq_put(hid, false, ps2_mods);   /* Pause has no release */
    }
    ps2_mouse_poll();
#endif
#ifdef FRANK_HAS_NESPAD
    nespad_read();
#endif
}

bool frank_input_key(frank_key_t *ev) {
#if FRANK_USB_HOST
    uint8_t code;
    int down;
    if (usbhid_get_key_action(&code, &down)) {
        static uint8_t usb_mods;
        uint8_t m = mod_bit(code);
        usb_mods = down ? (usb_mods | m) : (usb_mods & ~m);
        *ev = (frank_key_t){code, down != 0, usb_mods};
        last_key = *ev;
        key_count++;
        return true;
    }
#endif
    if (kq_tail == kq_head) return false;
    *ev = kq[kq_tail++ % KQ];
    last_key = *ev;
    key_count++;
    return true;
}

bool frank_input_mouse(int *dx, int *dy, int *wheel, uint8_t *buttons) {
    bool any = false;
    int x = 0, y = 0, w = 0;
    uint8_t b = 0;
#if FRANK_USB_HOST
    usbhid_mouse_state_t m;
    usbhid_get_mouse_state(&m);
    if (m.has_motion) {
        x += m.dx;
        y += m.dy;
        w += m.wheel;
        b |= m.buttons;
        any = true;
    }
#endif
#ifdef FRANK_HAS_PS2
    int16_t px, py;
    int8_t pw;
    uint8_t pb;
    if (ps2_mouse_get_state(&px, &py, &pw, &pb)) {
        x += px;
        y += py;
        w += pw;
        b |= pb;
        any = true;
    }
#endif
    *dx = x;
    *dy = y;
    *wheel = w;
    *buttons = b;
    return any;
}

#ifdef FRANK_HAS_NESPAD
/* nespad bits -> FRANK_PAD_* (B = south, A = east on the NES/SNES layout) */
static uint32_t from_nes(uint32_t s) {
    uint32_t p = 0;
    if (s & DPAD_UP) p |= FRANK_PAD_UP;
    if (s & DPAD_DOWN) p |= FRANK_PAD_DOWN;
    if (s & DPAD_LEFT) p |= FRANK_PAD_LEFT;
    if (s & DPAD_RIGHT) p |= FRANK_PAD_RIGHT;
    if (s & DPAD_A) p |= FRANK_PAD_EAST;
    if (s & DPAD_B) p |= FRANK_PAD_SOUTH;
    if (s & DPAD_X) p |= FRANK_PAD_NORTH;
    if (s & DPAD_Y) p |= FRANK_PAD_WEST;
    if (s & DPAD_LT) p |= FRANK_PAD_L1;
    if (s & DPAD_RT) p |= FRANK_PAD_R1;
    if (s & DPAD_START) p |= FRANK_PAD_START;
    if (s & DPAD_SELECT) p |= FRANK_PAD_SELECT;
    return p;
}
#endif

uint32_t frank_input_pad(int n) {
    uint32_t p = 0;
#if FRANK_USB_HOST
    if (n == 0) p |= usbhid_gamepad_buttons();   /* GP_* == FRANK_PAD_* */
#endif
#ifdef FRANK_HAS_NESPAD
    p |= from_nes(n == 0 ? nespad_state : nespad_state2);
#endif
    (void)n;
    return p;
}

void frank_input_report(void) {
#if FRANK_USB_HOST
    printf("USB: keyboard %s, mouse %s, gamepad %s\n", usbhid_keyboard_connected() ? "yes" : "no",
           usbhid_mouse_connected() ? "yes" : "no", usbhid_gamepad_connected() ? "yes" : "no");
#endif
#ifdef FRANK_HAS_PS2
    printf("PS/2: mouse %s\n", ps2_mouse_is_initialized() ? "yes" : "no");
#endif
    printf("keys seen %lu, last: 0x%02x %s (mods 0x%02x); pads 0x%05lx 0x%05lx\n", (unsigned long)key_count,
           last_key.key, last_key.down ? "down" : "up", last_key.modifiers,
           (unsigned long)frank_input_pad(0), (unsigned long)frank_input_pad(1));
}
