/*
 * frank-sdk — PS/2 set-2 scan codes to HID usage codes (the USB keyboard's
 * codes), so a firmware handles one key code set.
 *
 * The tables are the murmulator PS/2 keyboard library's (ps2kbd_mrmltr.cpp,
 * through frank-dos); the decoder is a C rewrite of its state machine:
 * E0 selects the second page, F0 marks a release, E1 starts Pause's
 * eight-byte sequence (reported as HID Pause).
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdbool.h>
#include <stdint.h>

#include "class/hid/hid.h"

static const uint8_t page0[] = {
  /* 00 (  0) */ HID_KEY_NONE,
  /* 01 (  1) */ HID_KEY_F9,
  /* 02 (  2) */ 0x00,
  /* 03 (  3) */ HID_KEY_F5,
  /* 04 (  4) */ HID_KEY_F3,
  /* 05 (  5) */ HID_KEY_F1,
  /* 06 (  6) */ HID_KEY_F2,
  /* 07 (  7) */ HID_KEY_F12,
  /* 08 (  8) */ HID_KEY_F13,
  /* 09 (  9) */ HID_KEY_F10,
  /* 0A ( 10) */ HID_KEY_F8,
  /* 0B ( 11) */ HID_KEY_F6,
  /* 0C ( 12) */ HID_KEY_F4,
  /* 0D ( 13) */ HID_KEY_TAB,
  /* 0E ( 14) */ HID_KEY_GRAVE,
  /* 0F ( 15) */ HID_KEY_KEYPAD_EQUAL,
  /* 10 ( 16) */ HID_KEY_F14,
  /* 11 ( 17) */ HID_KEY_ALT_LEFT,
  /* 12 ( 18) */ HID_KEY_SHIFT_LEFT,
  /* 13 ( 19) */ 0x00,
  /* 14 ( 20) */ HID_KEY_CONTROL_LEFT,
  /* 15 ( 21) */ HID_KEY_Q,
  /* 16 ( 22) */ HID_KEY_1,
  /* 17 ( 23) */ 0x00,
  /* 18 ( 24) */ HID_KEY_F15,
  /* 19 ( 25) */ 0x00,
  /* 1A ( 26) */ HID_KEY_Z,
  /* 1B ( 27) */ HID_KEY_S,
  /* 1C ( 28) */ HID_KEY_A,
  /* 1D ( 29) */ HID_KEY_W,
  /* 1E ( 30) */ HID_KEY_2,
  /* 1F ( 31) */ 0x00,
  /* 20 ( 32) */ HID_KEY_F16,
  /* 21 ( 33) */ HID_KEY_C,
  /* 22 ( 34) */ HID_KEY_X,
  /* 23 ( 35) */ HID_KEY_D,
  /* 24 ( 36) */ HID_KEY_E,
  /* 25 ( 37) */ HID_KEY_4,
  /* 26 ( 38) */ HID_KEY_3,
  /* 27 ( 39) */ 0x00,
  /* 28 ( 40) */ HID_KEY_F17,
  /* 29 ( 41) */ HID_KEY_SPACE,
  /* 2A ( 42) */ HID_KEY_V,
  /* 2B ( 43) */ HID_KEY_F,
  /* 2C ( 44) */ HID_KEY_T,
  /* 2D ( 45) */ HID_KEY_R,
  /* 2E ( 46) */ HID_KEY_5,
  /* 2F ( 47) */ 0x00,
  /* 30 ( 48) */ HID_KEY_F18,
  /* 31 ( 49) */ HID_KEY_N,
  /* 32 ( 50) */ HID_KEY_B,
  /* 33 ( 51) */ HID_KEY_H,
  /* 34 ( 52) */ HID_KEY_G,
  /* 35 ( 53) */ HID_KEY_Y,
  /* 36 ( 54) */ HID_KEY_6,
  /* 37 ( 55) */ 0x00,
  /* 38 ( 56) */ HID_KEY_F19,
  /* 39 ( 57) */ 0x00,
  /* 3A ( 58) */ HID_KEY_M,
  /* 3B ( 59) */ HID_KEY_J,
  /* 3C ( 60) */ HID_KEY_U,
  /* 3D ( 61) */ HID_KEY_7,
  /* 3E ( 62) */ HID_KEY_8,
  /* 3F ( 63) */ 0x00,
  /* 40 ( 64) */ HID_KEY_F20,
  /* 41 ( 65) */ HID_KEY_COMMA,
  /* 42 ( 66) */ HID_KEY_K,
  /* 43 ( 67) */ HID_KEY_I,
  /* 44 ( 68) */ HID_KEY_O,
  /* 45 ( 69) */ HID_KEY_0,
  /* 46 ( 70) */ HID_KEY_9,
  /* 47 ( 71) */ 0x00,
  /* 48 ( 72) */ HID_KEY_F21,
  /* 49 ( 73) */ HID_KEY_PERIOD,
  /* 4A ( 74) */ HID_KEY_SLASH,
  /* 4B ( 75) */ HID_KEY_L,
  /* 4C ( 76) */ HID_KEY_SEMICOLON,
  /* 4D ( 77) */ HID_KEY_P,
  /* 4E ( 78) */ HID_KEY_MINUS,
  /* 4F ( 79) */ 0x00,
  /* 50 ( 80) */ HID_KEY_F22,
  /* 51 ( 81) */ 0x00,
  /* 52 ( 82) */ HID_KEY_APOSTROPHE,
  /* 53 ( 83) */ 0x00,
  /* 54 ( 84) */ HID_KEY_BRACKET_LEFT,
  /* 55 ( 85) */ HID_KEY_EQUAL,
  /* 56 ( 86) */ 0x00,
  /* 57 ( 87) */ HID_KEY_F23,
  /* 58 ( 88) */ HID_KEY_CAPS_LOCK,
  /* 59 ( 89) */ HID_KEY_SHIFT_RIGHT,
  /* 5A ( 90) */ HID_KEY_ENTER, // RETURN ??
  /* 5B ( 91) */ HID_KEY_BRACKET_RIGHT,
  /* 5C ( 92) */ 0x00,
  /* 5D ( 93) */ HID_KEY_BACKSLASH,
  /* 5E ( 94) */ 0x00,
  /* 5F ( 95) */ HID_KEY_F24,
  /* 60 ( 96) */ 0x00,
  /* 61 ( 97) */ HID_KEY_EUROPE_2,
  /* 62 ( 98) */ 0x00,
  /* 63 ( 99) */ 0x00,
  /* 64 (100) */ 0x00,
  /* 65 (101) */ 0x00,
  /* 66 (102) */ HID_KEY_BACKSPACE,
  /* 67 (103) */ 0x00,
  /* 68 (104) */ 0x00,
  /* 69 (105) */ HID_KEY_KEYPAD_1,
  /* 6A (106) */ 0x00,
  /* 6B (107) */ HID_KEY_KEYPAD_4,
  /* 6C (108) */ HID_KEY_KEYPAD_7,
  /* 6D (109) */ 0x00,
  /* 6E (110) */ 0x00,
  /* 6F (111) */ 0x00,
  /* 70 (112) */ HID_KEY_KEYPAD_0,
  /* 71 (113) */ HID_KEY_KEYPAD_DECIMAL,
  /* 72 (114) */ HID_KEY_KEYPAD_2,
  /* 73 (115) */ HID_KEY_KEYPAD_5,
  /* 74 (116) */ HID_KEY_KEYPAD_6,
  /* 75 (117) */ HID_KEY_KEYPAD_8,
  /* 76 (118) */ HID_KEY_ESCAPE,
  /* 77 (119) */ HID_KEY_NUM_LOCK,
  /* 78 (120) */ HID_KEY_F11,
  /* 79 (121) */ HID_KEY_KEYPAD_ADD,
  /* 7A (122) */ HID_KEY_KEYPAD_3,
  /* 7B (123) */ HID_KEY_KEYPAD_SUBTRACT,
  /* 7C (124) */ HID_KEY_KEYPAD_MULTIPLY,
  /* 7D (125) */ HID_KEY_KEYPAD_9,
  /* 7E (126) */ HID_KEY_SCROLL_LOCK,
  /* 7F (127) */ 0x00,
  /* 80 (128) */ 0x00,
  /* 81 (129) */ 0x00,
  /* 82 (130) */ 0x00,
  /* 83 (131) */ HID_KEY_F7
};

static uint8_t page1(uint8_t c) {
    switch (c) {
    case 0x11: return HID_KEY_ALT_RIGHT;
    case 0x1f: return HID_KEY_GUI_LEFT;
    case 0x14: return HID_KEY_CONTROL_RIGHT;
    case 0x27: return HID_KEY_GUI_RIGHT;
    case 0x2f: return HID_KEY_APPLICATION;
    case 0x4a: return HID_KEY_KEYPAD_DIVIDE;
    case 0x5a: return HID_KEY_KEYPAD_ENTER;
    case 0x69: return HID_KEY_END;
    case 0x6b: return HID_KEY_ARROW_LEFT;
    case 0x6c: return HID_KEY_HOME;
    case 0x7c: return HID_KEY_PRINT_SCREEN;
    case 0x70: return HID_KEY_INSERT;
    case 0x71: return HID_KEY_DELETE;
    case 0x72: return HID_KEY_ARROW_DOWN;
    case 0x74: return HID_KEY_ARROW_RIGHT;
    case 0x75: return HID_KEY_ARROW_UP;
    case 0x7a: return HID_KEY_PAGE_DOWN;
    case 0x7d: return HID_KEY_PAGE_UP;
    default: return 0;
    }
}

/* One byte from the keyboard. Returns true with *hid and *down set when it
 * completes a key event. */
bool ps2_set2_decode(uint8_t b, uint8_t *hid, bool *down) {
    static bool ext, rel;
    static uint8_t pause_left;
    if (pause_left) {               /* E1 14 77 E1 F0 14 F0 77 */
        if (--pause_left == 0) {
            *hid = HID_KEY_PAUSE;
            *down = true;
            return true;
        }
        return false;
    }
    switch (b) {
    case 0xAA: case 0xFA: case 0xFE: case 0xEE: return false;   /* self test, ack, resend, echo */
    case 0xE0: ext = true; return false;
    case 0xF0: rel = true; return false;
    case 0xE1: pause_left = 7; return false;
    default: break;
    }
    uint8_t h = ext ? page1(b) : (b < sizeof(page0) ? page0[b] : 0);
    *down = !rel;
    ext = rel = false;
    if (!h) return false;           /* E0 12 / E0 F0 12: Print Screen's fake shifts */
    *hid = h;
    return true;
}
