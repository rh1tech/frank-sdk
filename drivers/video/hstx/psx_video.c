/*
 * frank-castle — HSTX DVI/HDMI output for PS1 BGR555 scanlines.
 * Derived from pico_hdmi video_output.c by fliperama86 (public domain).
 * Changes: 640x480 only, clk_hstx derived from any 126 MHz multiple,
 * BGR555 lane mapping, per-line DMA source with 16-bit pixel doubling.
 * frank-react: VESA 800x600@72 (50.4 MHz pixel clock from clk_hstx 252 MHz,
 * +H +V sync), sync polarity on lane 0 only, resync threshold from the mode.
 * The data islands' TERC4 sync bits follow the mode's polarity (hstx_packet.c).
 * The mode is chosen at run time (psxv_set_mode): 800x600@72 or VESA 640x480@60
 * (25.2 MHz from clk_hstx 126 MHz, -H -V sync).
 */
#include "psx_video.h"

#include "hstx_data_island_queue.h"
#include "hstx_packet.h"

#include "pico/stdlib.h"

#include "hardware/clocks.h"
#include "hardware/timer.h"
#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "hardware/structs/bus_ctrl.h"
#include "hardware/structs/clocks.h"
#include "hardware/structs/hstx_ctrl.h"
#include "hardware/structs/hstx_fifo.h"

#include <math.h>
#include <string.h>

typedef struct {
    uint16_t h_fp, h_sync, h_bp, h_active;
    uint16_t v_fp, v_sync, v_bp, v_active;
    bool sync_positive;
    uint32_t pixel_hz;      // clk_hstx is 5x this (CSR_CLKDIV 5)
} psxv_mode_t;

static const psxv_mode_t modes[] = {
    // VESA 800x600 @ 72 Hz (DMT 0x0A): 50.0 MHz nominal, here 50.4 MHz
    // (1040 x 666 -> 48.46 kHz, 72.8 Hz), +H +V sync; clk_hstx 252 MHz (sys 504 / 2)
    {56, 120, 64, 800, 37, 6, 23, 600, true, 50400000u},
    // VESA 640x480 @ 60 Hz (DMT 0x04): 25.175 MHz nominal, here 25.2 MHz
    // (800 x 525 -> 31.5 kHz, 60 Hz), -H -V sync; clk_hstx 126 MHz
    {16, 96, 48, 640, 10, 2, 33, 480, false, 25200000u},
};
/* the mode in use: a copy in SRAM (the line IRQ reads it, never flash) */
static psxv_mode_t cur_mode;
static const psxv_mode_t *md_sel = &modes[0];
#define md (&cur_mode)

#define MODE_H_FRONT_PORCH (md->h_fp)
#define MODE_H_SYNC_WIDTH (md->h_sync)
#define MODE_H_BACK_PORCH (md->h_bp)
#define MODE_H_ACTIVE_PIXELS (md->h_active)
#define MODE_V_FRONT_PORCH (md->v_fp)
#define MODE_V_SYNC_WIDTH (md->v_sync)
#define MODE_V_BACK_PORCH (md->v_bp)
#define MODE_V_ACTIVE_LINES (md->v_active)
#define MODE_SYNC_POSITIVE (md->sync_positive)
#define MODE_PIXEL_HZ (md->pixel_hz)
#define MODE_H_TOTAL_PIXELS (MODE_H_FRONT_PORCH + MODE_H_SYNC_WIDTH + MODE_H_BACK_PORCH + MODE_H_ACTIVE_PIXELS)
#define MODE_V_TOTAL_LINES (MODE_V_FRONT_PORCH + MODE_V_SYNC_WIDTH + MODE_V_BACK_PORCH + MODE_V_ACTIVE_LINES)
#define MODE_MAX_H_ACTIVE 800

// A frame shorter than 80 % of the mode's period means the DMA lost its place
static uint32_t mode_resync_us;
#define MODE_RESYNC_US mode_resync_us
#define MODE_HSTX_CSR_CLKDIV 5

// HSTX pins (M2 layout): 12=CLK-, 13=CLK+, 14/15=D0, 16/17=D1, 18/19=D2.
// frank-sdk: every HSTX board has this layout; the board file must agree.
#define PSXV_PIN_BASE FRANK_VIDEO_HDMI_BASE
#if FRANK_VIDEO_HDMI_BASE != 12 || FRANK_VIDEO_HDMI_CLK != 12 || FRANK_VIDEO_HDMI_DATA != 14 || \
    !FRANK_VIDEO_HDMI_INVERT_PAIRS
#error "psx_video: HSTX lanes are wired for the M2 layout (clock GPIO12-13 inverted, data 14-19)"
#endif

// PS1 BGR555: R=4..0, G=9..5, B=14..10. Each lane takes NBITS+1 bits from
// bit 7 down of the data rotated right by ROT. Lane 0=B, 1=G, 2=R.
#ifndef PSXV_ROT_R
#define PSXV_ROT_R 29
#endif
#ifndef PSXV_ROT_G
#define PSXV_ROT_G 2
#endif
#ifndef PSXV_ROT_B
#define PSXV_ROT_B 7
#endif

// ============================================================================
// DVI/HSTX Constants
// ============================================================================

#define TMDS_CTRL_00 0x354u // vsync=0 hsync=0
#define TMDS_CTRL_01 0x0abu // vsync=0 hsync=1
#define TMDS_CTRL_10 0x154u // vsync=1 hsync=0
#define TMDS_CTRL_11 0x2abu // vsync=1 hsync=1

// Lane 0 carries the syncs. In the names below V1/H1 mean "sync inactive"
// and V0/H0 "sync pulse" (the levels of a -/- mode); with positive sync the
// wire levels are the opposite. Lanes 1 and 2 carry CTL bits: never inverted.
#define L0_POS(v, h) ((v) ? ((h) ? TMDS_CTRL_00 : TMDS_CTRL_01) : ((h) ? TMDS_CTRL_10 : TMDS_CTRL_11))
#define L0_NEG(v, h) ((v) ? ((h) ? TMDS_CTRL_11 : TMDS_CTRL_10) : ((h) ? TMDS_CTRL_01 : TMDS_CTRL_00))
#define L0(v, h) (MODE_SYNC_POSITIVE ? L0_POS(v, h) : L0_NEG(v, h))

// Sync symbols: Lane 0 carries sync, Lanes 1&2 are always CTRL_00
#define SYNC_V0_H0 (L0(0, 0) | (TMDS_CTRL_00 << 10) | (TMDS_CTRL_00 << 20))
#define SYNC_V0_H1 (L0(0, 1) | (TMDS_CTRL_00 << 10) | (TMDS_CTRL_00 << 20))
#define SYNC_V1_H0 (L0(1, 0) | (TMDS_CTRL_00 << 10) | (TMDS_CTRL_00 << 20))
#define SYNC_V1_H1 (L0(1, 1) | (TMDS_CTRL_00 << 10) | (TMDS_CTRL_00 << 20))

// Data Island preamble: Lane 0 = sync, Lanes 1&2 = CTRL_01 pattern
// Per HDMI 1.3a Table 5-2: CTL0=1, CTL1=0, CTL2=1, CTL3=0
#define PREAMBLE_V0_H0 (L0(0, 0) | (TMDS_CTRL_01 << 10) | (TMDS_CTRL_01 << 20))
#define PREAMBLE_V1_H0 (L0(1, 0) | (TMDS_CTRL_01 << 10) | (TMDS_CTRL_01 << 20))

// Video preamble: Lane 0 = sync, Lane 1 = CTRL_01, Lane 2 = CTRL_00
// Per HDMI 1.3a Table 5-2: CTL0=1, CTL1=0, CTL2=0, CTL3=0
#define VIDEO_PREAMBLE_V0_H1 (L0(0, 1) | (TMDS_CTRL_01 << 10) | (TMDS_CTRL_00 << 20))
#define VIDEO_PREAMBLE_V1_H1 (L0(1, 1) | (TMDS_CTRL_01 << 10) | (TMDS_CTRL_00 << 20))

// Video guard band: Per HDMI 1.3a Table 5-5
// CH0 = 0b1011001100 (0x2CC), CH1 = 0b0100110011 (0x133), CH2 = 0b1011001100 (0x2CC)
#define VIDEO_GUARD_BAND (0x2CCu | (0x133u << 10) | (0x2CCu << 20))

#define HSTX_CMD_RAW (0x0u << 12)
#define HSTX_CMD_RAW_REPEAT (0x1u << 12)
#define HSTX_CMD_TMDS (0x2u << 12)
#define HSTX_CMD_TMDS_REPEAT (0x3u << 12)
#define HSTX_CMD_NOP (0xfu << 12)

#define SYNC_AFTER_DI (MODE_H_SYNC_WIDTH - W_PREAMBLE - W_DATA_ISLAND)

// Video preamble and guard band widths (HDMI 1.3a Section 5.2.2)
#define W_VIDEO_PREAMBLE 8
#define W_VIDEO_GUARD_BAND 2

// ============================================================================
// Audio/Video State
// ============================================================================

volatile uint32_t psxv_frame_count = 0;

/* Self-healing: the ping-pong DMA cannot survive one IRQ serviced too late
 * (the channels then run lines out of order and the sink loses sync for
 * good). A frame period well under 16.7 ms means that happened: restart the
 * output from the top of the frame. */
volatile uint32_t psxv_resyncs;
static uint32_t last_vsync_us;
static bool resync_needed;
/* a mode change: the IRQ stops the output and says so, psxv_set_mode goes on */
static volatile bool stop_req, stopped, started;

// DVI mode: when true, disables all HDMI Data Islands (pure DVI output, no audio)
// Some monitors have trouble syncing with HDMI Data Islands
static bool dvi_mode = false; // Default to HDMI mode (full features with audio)

static uint16_t blank_line[MODE_MAX_H_ACTIVE] __attribute__((aligned(4)));
static psxv_line_t next_line;
static volatile uint32_t v_scanline = 2;

/* frank-diablo: the line being prepared, as an active-picture line: 0..h-1
 * active, h.. the vertical blank (for drawing in bands behind the beam) */
uint32_t psxv_beam_line(void) {
    uint32_t v = v_scanline;
    return (v + MODE_V_TOTAL_LINES - (MODE_V_TOTAL_LINES - MODE_V_ACTIVE_LINES)) % MODE_V_TOTAL_LINES;
}
static bool vactive_cmdlist_posted = false;
static bool dma_pong = false;

static psxv_line_cb_t line_callback = NULL;
static psxv_vsync_cb_t vsync_callback = NULL;

#define DMACH_PING 0
#define DMACH_PONG 1

// ============================================================================
// Command Lists
// ============================================================================

// Pure DVI command lists (no Data Islands), filled for the mode by build_cmdlists()
static uint32_t vblank_line_vsync_off[9], vblank_line_vsync_on[9];
// Active video line for DVI mode (no Data Island, just sync + pixels)
static uint32_t vactive_line_dvi[9];
/* the same with the left margin sent as black by the HSTX (PSXV_LINE_DOUBLE_LM) */
static uint32_t vactive_line_dvi_lm[11];

static void build_cmdlists(void)
{
    for (int v = 0; v < 2; v++) {
        uint32_t *l = v ? vblank_line_vsync_on : vblank_line_vsync_off;
        uint32_t h0 = v ? SYNC_V0_H0 : SYNC_V1_H0, h1 = v ? SYNC_V0_H1 : SYNC_V1_H1;
        l[0] = HSTX_CMD_RAW_REPEAT | MODE_H_FRONT_PORCH;
        l[1] = h1;
        l[2] = HSTX_CMD_NOP;
        l[3] = HSTX_CMD_RAW_REPEAT | MODE_H_SYNC_WIDTH;
        l[4] = h0;
        l[5] = HSTX_CMD_NOP;
        l[6] = HSTX_CMD_RAW_REPEAT | (MODE_H_BACK_PORCH + MODE_H_ACTIVE_PIXELS);
        l[7] = h1;
        l[8] = HSTX_CMD_NOP;
    }
    for (int lm = 0; lm < 2; lm++) {
        uint32_t *l = lm ? vactive_line_dvi_lm : vactive_line_dvi;
        memcpy(l, vblank_line_vsync_off, 6 * sizeof *l);
        l[6] = HSTX_CMD_RAW_REPEAT | MODE_H_BACK_PORCH;
        l[7] = SYNC_V1_H1;
        if (lm) {
            l[8] = HSTX_CMD_TMDS_REPEAT | PSXV_LM_OUT;
            l[9] = 0;
            l[10] = HSTX_CMD_TMDS | (MODE_H_ACTIVE_PIXELS - PSXV_LM_OUT);
        } else {
            l[8] = HSTX_CMD_TMDS | MODE_H_ACTIVE_PIXELS;
        }
    }
}

static uint32_t vactive_di_ping[128], vactive_di_pong[128], vactive_di_null[128], vactive_di_null_lm[128];
static uint32_t vactive_di_len, vactive_di_null_len, vactive_di_null_lm_len;

static uint32_t vblank_di_ping[128], vblank_di_pong[128], vblank_di_null[128];
static uint32_t vblank_di_len, vblank_di_null_len;

static uint32_t vblank_acr_vsync_on[64], vblank_acr_vsync_on_len;
static uint32_t vblank_acr_vsync_off[64], vblank_acr_vsync_off_len;
static uint32_t vblank_infoframe_vsync_on[64], vblank_infoframe_vsync_on_len;
static uint32_t vblank_infoframe_vsync_off[64], vblank_infoframe_vsync_off_len;
static uint32_t vblank_avi_infoframe[64], vblank_avi_infoframe_len;

// ============================================================================
// HSTX Resync - Reset output to sync with input VSYNC
// ============================================================================

static inline void __scratch_x("") set_dma_size(dma_channel_hw_t *ch, uint32_t size)
{
    hw_write_masked(&ch->al1_ctrl, size << DMA_CH0_CTRL_TRIG_DATA_SIZE_LSB, DMA_CH0_CTRL_TRIG_DATA_SIZE_BITS);
}

static void __scratch_x("") hstx_resync(void)
{
    // 1. Abort DMA chains
    dma_channel_abort(DMACH_PING);
    dma_channel_abort(DMACH_PONG);

    // 2. Disable HSTX (resets shift register, clock generator, and flushes FIFO)
    hstx_ctrl_hw->csr &= ~HSTX_CTRL_CSR_EN_BITS;

    // Small delay to ensure HSTX fully stops
    __asm volatile("nop\nnop\nnop\nnop");

    // 3. Reset state to start of frame (two blank lines are queued below)
    v_scanline = 2;
    vactive_cmdlist_posted = false;
    dma_pong = false;

    // 4. Clear any pending DMA interrupts
    dma_hw->ints0 = (1U << DMACH_PING) | (1U << DMACH_PONG);

    // 5. Configure DMA PING to start from beginning of frame (Line 0)
    dma_channel_hw_t *ch_ping = &dma_hw->ch[DMACH_PING];
    set_dma_size(ch_ping, DMA_SIZE_32);
    set_dma_size(&dma_hw->ch[DMACH_PONG], DMA_SIZE_32);
    ch_ping->read_addr = (uintptr_t)vblank_line_vsync_off;
    ch_ping->transfer_count = count_of(vblank_line_vsync_off);

    // 6. Configure DMA PONG for the NEXT line (Line 1)
    // This ensures that when PING finishes and chains to PONG, PONG is ready.
    dma_channel_hw_t *ch_pong = &dma_hw->ch[DMACH_PONG];
    ch_pong->read_addr = (uintptr_t)vblank_line_vsync_off; // Line 1 is also blank
    ch_pong->transfer_count = count_of(vblank_line_vsync_off);

    // 7. Re-enable HSTX then start DMA
    hstx_ctrl_hw->csr |= HSTX_CTRL_CSR_EN_BITS;
    dma_channel_start(DMACH_PING);
}


// ============================================================================
// Internal Helpers
// ============================================================================

static uint32_t build_line_with_di_lm(uint32_t *buf, const uint32_t *di_words, bool vsync, bool active, bool lm);
static uint32_t build_line_with_di(uint32_t *buf, const uint32_t *di_words, bool vsync, bool active)
{
    return build_line_with_di_lm(buf, di_words, vsync, active, false);
}

static uint32_t __not_in_flash_func(build_line_with_di_lm)(uint32_t *buf, const uint32_t *di_words, bool vsync, bool active, bool lm)
{
    uint32_t *p = buf;
    uint32_t sync_h0 = vsync ? SYNC_V0_H0 : SYNC_V1_H0;
    uint32_t sync_h1 = vsync ? SYNC_V0_H1 : SYNC_V1_H1;
    uint32_t preamble = vsync ? PREAMBLE_V0_H0 : PREAMBLE_V1_H0;

    *p++ = HSTX_CMD_RAW_REPEAT | MODE_H_FRONT_PORCH;
    *p++ = sync_h1;
    *p++ = HSTX_CMD_NOP;

    *p++ = HSTX_CMD_RAW_REPEAT | W_PREAMBLE;
    *p++ = preamble;
    *p++ = HSTX_CMD_NOP;

    *p++ = HSTX_CMD_RAW | W_DATA_ISLAND;
    for (int i = 0; i < W_DATA_ISLAND; i++)
        *p++ = di_words[i];
    *p++ = HSTX_CMD_NOP;

    *p++ = HSTX_CMD_RAW_REPEAT | SYNC_AFTER_DI;
    *p++ = sync_h0;
    *p++ = HSTX_CMD_NOP;

    if (active) {
        // HDMI 1.3a Section 5.2.2: Video Data Period requires preamble and guard band
        uint32_t video_preamble = vsync ? VIDEO_PREAMBLE_V0_H1 : VIDEO_PREAMBLE_V1_H1;

        // Control period (back porch minus preamble and guard band)
        *p++ = HSTX_CMD_RAW_REPEAT | (MODE_H_BACK_PORCH - W_VIDEO_PREAMBLE - W_VIDEO_GUARD_BAND);
        *p++ = sync_h1;
        *p++ = HSTX_CMD_NOP;

        // Video Preamble (8 pixels)
        *p++ = HSTX_CMD_RAW_REPEAT | W_VIDEO_PREAMBLE;
        *p++ = video_preamble;
        *p++ = HSTX_CMD_NOP;

        // Video Guard Band (2 pixels)
        *p++ = HSTX_CMD_RAW_REPEAT | W_VIDEO_GUARD_BAND;
        *p++ = VIDEO_GUARD_BAND;

        // Active video pixels (PSXV_LINE_DOUBLE_LM: a black margin first)
        if (lm) {
            *p++ = HSTX_CMD_TMDS_REPEAT | PSXV_LM_OUT;
            *p++ = 0;
            *p++ = HSTX_CMD_TMDS | (MODE_H_ACTIVE_PIXELS - PSXV_LM_OUT);
        } else {
            *p++ = HSTX_CMD_TMDS | MODE_H_ACTIVE_PIXELS;
        }
    } else {
        *p++ = HSTX_CMD_RAW_REPEAT | (MODE_H_BACK_PORCH + MODE_H_ACTIVE_PIXELS);
        *p++ = sync_h1;
        *p++ = HSTX_CMD_NOP;
    }
    return (uint32_t)(p - buf);
}

typedef struct {
    bool vsync_active;
    bool front_porch;
    bool back_porch;
    bool active_video;
    bool send_acr;
    uint32_t active_line;
} scanline_state_t;

static inline void __scratch_x("") get_scanline_state(uint32_t v_scanline, scanline_state_t *state)
{
    state->vsync_active = (v_scanline >= MODE_V_FRONT_PORCH && v_scanline < (MODE_V_FRONT_PORCH + MODE_V_SYNC_WIDTH));
    state->front_porch = (v_scanline < MODE_V_FRONT_PORCH);
    state->back_porch = (v_scanline >= MODE_V_FRONT_PORCH + MODE_V_SYNC_WIDTH &&
                         v_scanline < MODE_V_FRONT_PORCH + MODE_V_SYNC_WIDTH + MODE_V_BACK_PORCH);
    state->active_video = (!state->vsync_active && !state->front_porch && !state->back_porch);

    state->send_acr = (v_scanline >= (MODE_V_FRONT_PORCH + MODE_V_SYNC_WIDTH) &&
                       v_scanline < (MODE_V_TOTAL_LINES - MODE_V_ACTIVE_LINES) && (v_scanline % 4 == 0));

    if (state->active_video) {
        state->active_line = v_scanline - (MODE_V_TOTAL_LINES - MODE_V_ACTIVE_LINES);
    } else {
        state->active_line = 0;
    }
}

static inline void __scratch_x("") video_output_handle_vsync(dma_channel_hw_t *ch, uint32_t v_scanline)
{
    if (dvi_mode) {
        // Pure DVI: simple vsync line without Data Islands
        ch->read_addr = (uintptr_t)vblank_line_vsync_on;
        ch->transfer_count = count_of(vblank_line_vsync_on);
        if (v_scanline == MODE_V_FRONT_PORCH) {
            psxv_frame_count++;
            {
                uint32_t now = timer_hw->timerawl;
                if (last_vsync_us && now - last_vsync_us < MODE_RESYNC_US) resync_needed = true;
                last_vsync_us = now;
            }
            if (vsync_callback)
                vsync_callback();
        }
    } else {
        if (v_scanline == MODE_V_FRONT_PORCH) {
            ch->read_addr = (uintptr_t)vblank_acr_vsync_on;
            ch->transfer_count = vblank_acr_vsync_on_len;
            psxv_frame_count++;
            {
                uint32_t now = timer_hw->timerawl;
                if (last_vsync_us && now - last_vsync_us < MODE_RESYNC_US) resync_needed = true;
                last_vsync_us = now;
            }
            if (vsync_callback)
                vsync_callback();
        } else {
            ch->read_addr = (uintptr_t)vblank_infoframe_vsync_on;
            ch->transfer_count = vblank_infoframe_vsync_on_len;
        }
    }
}

static inline void __scratch_x("")
    video_output_handle_active_start(dma_channel_hw_t *ch, uint32_t v_scanline, uint32_t active_line, bool dma_pong)
{
    (void)v_scanline;
    next_line.pixels = blank_line;
    next_line.mode = PSXV_LINE_NATIVE;
    if (line_callback)
        line_callback(active_line, &next_line);

    bool lm = next_line.mode == PSXV_LINE_DOUBLE_LM;
    if (dvi_mode) {
        // Pure DVI: simple active line without Data Islands
        ch->read_addr = lm ? (uintptr_t)vactive_line_dvi_lm : (uintptr_t)vactive_line_dvi;
        ch->transfer_count = lm ? count_of(vactive_line_dvi_lm) : count_of(vactive_line_dvi);
    } else {
        uint32_t *buf = dma_pong ? vactive_di_ping : vactive_di_pong;
        const uint32_t *di_words = hstx_di_queue_get_audio_packet();
        if (di_words) {
            vactive_di_len = build_line_with_di_lm(buf, di_words, false, true, lm);
            ch->read_addr = (uintptr_t)buf;
            ch->transfer_count = vactive_di_len;
        } else if (lm) {
            ch->read_addr = (uintptr_t)vactive_di_null_lm;
            ch->transfer_count = vactive_di_null_lm_len;
        } else {
            ch->read_addr = (uintptr_t)vactive_di_null;
            ch->transfer_count = vactive_di_null_len;
        }
    }
}

static inline void __scratch_x("")
    video_output_handle_blanking(dma_channel_hw_t *ch, uint32_t v_scanline, bool send_acr, bool dma_pong)
{
    if (dvi_mode) {
        // Pure DVI: simple blanking line without Data Islands
        (void)send_acr;
        (void)dma_pong;
        (void)v_scanline;
        ch->read_addr = (uintptr_t)vblank_line_vsync_off;
        ch->transfer_count = count_of(vblank_line_vsync_off);
    } else {
        if (send_acr) {
            ch->read_addr = (uintptr_t)vblank_acr_vsync_off;
            ch->transfer_count = vblank_acr_vsync_off_len;
        } else if (v_scanline == 0) {
            ch->read_addr = (uintptr_t)vblank_avi_infoframe;
            ch->transfer_count = vblank_avi_infoframe_len;
        } else {
            const uint32_t *di_words = hstx_di_queue_get_audio_packet();
            if (di_words) {
                uint32_t *buf = dma_pong ? vblank_di_ping : vblank_di_pong;
                vblank_di_len = build_line_with_di(buf, di_words, false, false);
                ch->read_addr = (uintptr_t)buf;
                ch->transfer_count = vblank_di_len;
            } else {
                ch->read_addr = (uintptr_t)vblank_di_null;
                ch->transfer_count = vblank_di_null_len;
            }
        }
    }
}

static inline void __scratch_x("") video_output_handle_active_data(dma_channel_hw_t *ch)
{
    ch->read_addr = (uintptr_t)next_line.pixels;
    if (next_line.mode == PSXV_LINE_DOUBLE) {
        // 16-bit reads, replicated into both FIFO halves -> pixel doubling
        set_dma_size(ch, DMA_SIZE_16);
        ch->transfer_count = MODE_H_ACTIVE_PIXELS / 2;
    } else if (next_line.mode == PSXV_LINE_DOUBLE_LM) {
        set_dma_size(ch, DMA_SIZE_16);
        ch->transfer_count = (MODE_H_ACTIVE_PIXELS - PSXV_LM_OUT) / 2;
    } else {
        set_dma_size(ch, DMA_SIZE_32);
        ch->transfer_count = MODE_H_ACTIVE_PIXELS / 2;
    }
}

// ============================================================================
// DMA IRQ Handler
// ============================================================================

volatile uint32_t psxv_irq_us; /* time spent in this IRQ (diagnostic) */
volatile uint32_t psxv_irq_max_us;
static void __scratch_x("") dma_irq_handler_body(void);
static void __scratch_x("") dma_irq_handler(void)
{
    uint32_t t0 = timer_hw->timerawl;
    dma_irq_handler_body();
    hstx_di_queue_post(); /* after the DMA is set up again */
    uint32_t dt = timer_hw->timerawl - t0;
    psxv_irq_us += dt;
    if (dt > psxv_irq_max_us) psxv_irq_max_us = dt;
}

volatile uint32_t psxv_late;  /* IRQs that came after the next channel had finished too */
volatile uint32_t psxv_fifo_low;  /* IRQs that found the HSTX FIFO empty */
static void __scratch_x("") dma_irq_handler_body(void)
{
    uint32_t ch_num = dma_pong ? DMACH_PONG : DMACH_PING;
    dma_channel_hw_t *ch = &dma_hw->ch[ch_num];
    if (!(dma_hw->ch[ch_num ^ 1].ctrl_trig & DMA_CH0_CTRL_TRIG_BUSY_BITS))
        psxv_late++;
    if (hstx_fifo_hw->stat & HSTX_FIFO_STAT_EMPTY_BITS)
        psxv_fifo_low++;
    dma_hw->intr = 1U << ch_num;
    dma_pong = !dma_pong;

    // Advance audio/data-island scheduler exactly once per scanline (HDMI mode only)
    if (!dvi_mode && !vactive_cmdlist_posted) {
        hstx_di_queue_tick();
    }

    if (stop_req) {
        dma_channel_abort(DMACH_PING);
        dma_channel_abort(DMACH_PONG);
        hstx_ctrl_hw->csr &= ~HSTX_CTRL_CSR_EN_BITS;
        dma_hw->ints0 = (1U << DMACH_PING) | (1U << DMACH_PONG);
        stop_req = false;
        stopped = true;
        return;
    }

    if (resync_needed) {
        resync_needed = false;
        psxv_resyncs++;
        last_vsync_us = 0;
        hstx_resync();
        return;
    }

    scanline_state_t state;
    get_scanline_state(v_scanline, &state);
    if (!(state.active_video && vactive_cmdlist_posted))
        set_dma_size(ch, DMA_SIZE_32);

    if (state.vsync_active) {
        video_output_handle_vsync(ch, v_scanline);
    } else if (state.active_video && !vactive_cmdlist_posted) {
        video_output_handle_active_start(ch, v_scanline, state.active_line, dma_pong);
        vactive_cmdlist_posted = true;
    } else if (state.active_video && vactive_cmdlist_posted) {
        video_output_handle_active_data(ch);
        vactive_cmdlist_posted = false;
    } else {
        video_output_handle_blanking(ch, v_scanline, state.send_acr, dma_pong);
    }
    if (!vactive_cmdlist_posted)
        v_scanline = (v_scanline + 1) % MODE_V_TOTAL_LINES;
}

// ============================================================================
// Public Interface
// ============================================================================

// ACR N (HDMI spec Table 7-1/7-2 values); CTS = f_TMDS * N / (128 * fs)
static void get_acr_params(uint32_t sample_rate, uint32_t *n, uint32_t *cts)
{
    switch (sample_rate) {
        case 32000: *n = 4096; break;
        case 44100: *n = 6272; break;
        case 88200: *n = 12544; break;
        case 176400: *n = 25088; break;
        case 96000: *n = 12288; break;
        case 192000: *n = 24576; break;
        default: *n = 6144; sample_rate = 48000; break;   // 48 kHz
    }
    *cts = (uint32_t)((uint64_t)MODE_PIXEL_HZ * *n / (128u * sample_rate));
}

static uint32_t audio_rate = 44100;

static void configure_audio_packets(uint32_t sample_rate)
{
    audio_rate = sample_rate;
    hstx_di_queue_set_v_total(MODE_V_TOTAL_LINES);
    // samples per line from the line rate (the queue itself assumes 60 Hz)
    hstx_di_queue_set_samples_per_line_fp(
        (uint32_t)((uint64_t)sample_rate * MODE_H_TOTAL_PIXELS * 65536u / MODE_PIXEL_HZ));

    hstx_packet_t packet;
    hstx_data_island_t island;

    uint32_t acr_n;
    uint32_t acr_cts;
    get_acr_params(sample_rate, &acr_n, &acr_cts);
    hstx_packet_set_acr(&packet, acr_n, acr_cts);
    hstx_encode_data_island(&island, &packet, true, true);
    vblank_acr_vsync_on_len = build_line_with_di(vblank_acr_vsync_on, island.words, true, false);
    hstx_encode_data_island(&island, &packet, false, true);
    vblank_acr_vsync_off_len = build_line_with_di(vblank_acr_vsync_off, island.words, false, false);

    hstx_packet_set_audio_infoframe(&packet, sample_rate, 2, 16);
    hstx_encode_data_island(&island, &packet, true, true);
    vblank_infoframe_vsync_on_len = build_line_with_di(vblank_infoframe_vsync_on, island.words, true, false);
    hstx_encode_data_island(&island, &packet, false, true);
    vblank_infoframe_vsync_off_len = build_line_with_di(vblank_infoframe_vsync_off, island.words, false, false);
}

// everything that follows from the mode: command lists, packets, clk_hstx
static void apply_mode(void)
{
    cur_mode = *md_sel;
    mode_resync_us = (uint32_t)((uint64_t)MODE_H_TOTAL_PIXELS * MODE_V_TOTAL_LINES * 800000u / MODE_PIXEL_HZ);
    build_cmdlists();

    // the data islands carry the syncs too: encoded again for the polarity
    hstx_set_sync_positive(MODE_SYNC_POSITIVE);
    hstx_audio_source_t src = hstx_di_queue_get_audio_source();
    hstx_di_queue_init();
    hstx_di_queue_set_audio_source(src);

    // clk_hstx straight from PLL_SYS (sys clock is a 126 MHz multiple)
    uint32_t sys_freq = clock_get_hz(clk_sys);
    clock_configure(clk_hstx, 0, CLOCKS_CLK_HSTX_CTRL_AUXSRC_VALUE_CLKSRC_PLL_SYS, sys_freq,
                    MODE_PIXEL_HZ * MODE_HSTX_CSR_CLKDIV);

    configure_audio_packets(audio_rate);

    hstx_packet_t packet;
    hstx_data_island_t island;

    // VIC=1 for standard 640x480, VIC=0 for the others (800x600 has none)
    uint8_t vic = (MODE_V_ACTIVE_LINES == 480) ? 1 : 0;
    hstx_packet_set_avi_infoframe(&packet, vic, 0);
    hstx_encode_data_island(&island, &packet, false, true);
    vblank_avi_infoframe_len = build_line_with_di(vblank_avi_infoframe, island.words, false, false);

    vblank_di_null_len = build_line_with_di(vblank_di_null, hstx_get_null_data_island(false, true), false, false);
    vactive_di_null_len = build_line_with_di(vactive_di_null, hstx_get_null_data_island(false, true), false, true);
    vactive_di_null_lm_len =
        build_line_with_di_lm(vactive_di_null_lm, hstx_get_null_data_island(false, true), false, true, true);

    vblank_di_len = build_line_with_di(vblank_di_ping, hstx_get_null_data_island(false, true), false, false);
    memcpy(vblank_di_pong, vblank_di_ping, sizeof(vblank_di_ping));
}

void psxv_init(void)
{
    // Claim DMA channels for HSTX (channels 0 and 1)
    dma_channel_claim(DMACH_PING);
    dma_channel_claim(DMACH_PONG);

    // HDMI audio packets: 44.1 kHz (apply_mode also initialises the
    // data-island scheduler, which the dispatcher pulls from every line)
    apply_mode();
}

bool psxv_set_mode(int w, int h)
{
    const psxv_mode_t *m = NULL;
    for (unsigned i = 0; i < count_of(modes); i++)
        if (modes[i].h_active == w && modes[i].v_active == h)
            m = &modes[i];
    if (!m)
        return false;
    if (m == md_sel)
        return true;
    if (started) {
        // the IRQ (on the other core) stops the output at its next line
        stopped = false;
        stop_req = true;
        absolute_time_t until = make_timeout_time_ms(100);
        while (!stopped && !time_reached(until))
            tight_loop_contents();
        if (!stopped) {
            stop_req = false;
            return false;
        }
    }
    md_sel = m;
    apply_mode();
    if (started) {
        last_vsync_us = 0;
        hstx_resync();      // from the top of the frame, HSTX on again
    }
    return true;
}

int __not_in_flash_func(psxv_out_w)(void) { return MODE_H_ACTIVE_PIXELS; }
int __not_in_flash_func(psxv_out_h)(void) { return MODE_V_ACTIVE_LINES; }

void psxv_set_dvi_mode(bool enabled)
{
    dvi_mode = enabled;
}

bool psxv_get_dvi_mode(void)
{
    return dvi_mode;
}

void psxv_set_line_callback(psxv_line_cb_t cb)
{
    line_callback = cb;
}

void psxv_set_vsync_callback(psxv_vsync_cb_t cb)
{
    vsync_callback = cb;
}

void psxv_start(void)
{
    // HSTX Hardware Setup
    hstx_ctrl_hw->expand_tmds = 4 << HSTX_CTRL_EXPAND_TMDS_L2_NBITS_LSB | PSXV_ROT_R << HSTX_CTRL_EXPAND_TMDS_L2_ROT_LSB |
                                4 << HSTX_CTRL_EXPAND_TMDS_L1_NBITS_LSB | PSXV_ROT_G << HSTX_CTRL_EXPAND_TMDS_L1_ROT_LSB |
                                4 << HSTX_CTRL_EXPAND_TMDS_L0_NBITS_LSB | PSXV_ROT_B << HSTX_CTRL_EXPAND_TMDS_L0_ROT_LSB;

    hstx_ctrl_hw->expand_shift =
        2 << HSTX_CTRL_EXPAND_SHIFT_ENC_N_SHIFTS_LSB | 16 << HSTX_CTRL_EXPAND_SHIFT_ENC_SHIFT_LSB |
        1 << HSTX_CTRL_EXPAND_SHIFT_RAW_N_SHIFTS_LSB | 0 << HSTX_CTRL_EXPAND_SHIFT_RAW_SHIFT_LSB;

    hstx_ctrl_hw->csr = 0;
    hstx_ctrl_hw->csr = HSTX_CTRL_CSR_EXPAND_EN_BITS | (uint32_t)MODE_HSTX_CSR_CLKDIV << HSTX_CTRL_CSR_CLKDIV_LSB |
                        5U << HSTX_CTRL_CSR_N_SHIFTS_LSB | 2U << HSTX_CTRL_CSR_SHIFT_LSB | HSTX_CTRL_CSR_EN_BITS;

    hstx_ctrl_hw->bit[0] = HSTX_CTRL_BIT0_CLK_BITS | HSTX_CTRL_BIT0_INV_BITS;
    hstx_ctrl_hw->bit[1] = HSTX_CTRL_BIT0_CLK_BITS;
    for (uint pair = 0; pair < 3; ++pair) {
        int bit = 2 + (pair * 2);
        /* the board's colour order: lane 0 (blue) first after the clock, or
         * red first (rgb_not_bgr false: the Adafruit Fruit Jam) */
#if defined(FRANK_VIDEO_HDMI_RGB_NOT_BGR) && !FRANK_VIDEO_HDMI_RGB_NOT_BGR
        const uint lane = 2 - pair;
#else
        const uint lane = pair;
#endif
        uint32_t lane_data_sel_bits = (lane * 10) << HSTX_CTRL_BIT0_SEL_P_LSB | (lane * 10 + 1)
                                                                                    << HSTX_CTRL_BIT0_SEL_N_LSB;
        hstx_ctrl_hw->bit[bit] = lane_data_sel_bits | HSTX_CTRL_BIT0_INV_BITS;
        hstx_ctrl_hw->bit[bit + 1] = lane_data_sel_bits;
    }

    // Set GPIO 12-19 to HSTX function (function 0 on RP2350)
    for (int i = PSXV_PIN_BASE; i < PSXV_PIN_BASE + 8; ++i)
        gpio_set_function(i, 0);

    // DMA Setup
    dma_channel_config c = dma_channel_get_default_config(DMACH_PING);
    channel_config_set_chain_to(&c, DMACH_PONG);
    channel_config_set_dreq(&c, DREQ_HSTX);
    dma_channel_configure(DMACH_PING, &c, &hstx_fifo_hw->fifo, vblank_line_vsync_off, count_of(vblank_line_vsync_off),
                          false);

    c = dma_channel_get_default_config(DMACH_PONG);
    channel_config_set_chain_to(&c, DMACH_PING);
    channel_config_set_dreq(&c, DREQ_HSTX);
    dma_channel_configure(DMACH_PONG, &c, &hstx_fifo_hw->fifo, vblank_line_vsync_off, count_of(vblank_line_vsync_off),
                          false);

    dma_hw->ints0 = (1U << DMACH_PING) | (1U << DMACH_PONG);
    dma_hw->inte0 = (1U << DMACH_PING) | (1U << DMACH_PONG);
    irq_set_exclusive_handler(DMA_IRQ_0, dma_irq_handler);
    irq_set_priority(DMA_IRQ_0, 0);
    irq_set_enabled(DMA_IRQ_0, true);

    bus_ctrl_hw->priority = BUSCTRL_BUS_PRIORITY_DMA_W_BITS | BUSCTRL_BUS_PRIORITY_DMA_R_BITS;
    started = true;
    dma_channel_start(DMACH_PING);
}

void psxv_set_audio_sample_rate(uint32_t sample_rate)
{
    configure_audio_packets(sample_rate);
}
