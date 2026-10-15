// SPDX-License-Identifier: Apache-2.0
// ESP-Mosaico Experience - device entry, STAGE 3a: display + touch + UI only.
//
// What this stage does NOT do (and the UI therefore reports honestly):
//   * no IMU / magnetometer / microphone / battery / expansion-slot access   -> sensors show "未接続"
//   * no sound output, no AI
//   * no radio of any kind is initialised or requested
// Axis mapping, touch orientation and every hardware claim must be verified on the board.
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bsp/esp_mosaico.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_touch.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "exp_app.h"
#include <math.h>
#include "dev_audio.h"
#include "dev_io_core.h"
#include "dev_joystick.h"
#include "dev_rf_probe.h"
#include "dev_sensors.h"
#include "dev_slots.h"
#include "dev_watchdog.h"
#if EXP_VOICE
#include "dev_voice.h"
static dev_voice_status_t s_voice;     /* snapshot of the speech engine, refreshed by fill_sensors() and the I/O log page */
#endif
#if EXP_VISION
#include "dev_camera.h"
#include "dev_camera_gate.h"
#include "dev_face_detect.h"
#endif
#include "dev_sensors_core.h"
#include "dev_magnet.h"
#include "assets/m5stack_logo.h"
#include "esp_iris.h"
#include "exp_dbglog.h"
#include "exp_gfx.h"
#include "exp_text.h"
#include "exp_surface.h"
#include "mosaico_raylib_fast.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char *TAG = "exp_main";
/* EXP_RELEASE=1 (idf.py -DEXP_RELEASE=1 build) is the exhibition profile: no log view, no timing overlay, no boot benchmark.
 * The version string says which profile the image is; a development image must never be put on a stand. */
#ifndef EXP_RELEASE
#define EXP_RELEASE 0
#endif
#if EXP_RELEASE
#define FW_PROFILE "-rel"
#else
#define FW_PROFILE "-dev"
#endif
#if EXP_VOICE
#define FW_VOICE_TAG "-voice"
#else
#define FW_VOICE_TAG "-novoice"
#endif
#if EXP_VISION
#define FW_VISION_TAG "-vision"
#define FW_BASE_VERSION "0.9.3-stage4c"
#else
#define FW_VISION_TAG ""
#define FW_BASE_VERSION "0.8.1-stage3e"
#endif
#define FW_VERSION FW_BASE_VERSION FW_VOICE_TAG FW_VISION_TAG FW_PROFILE
#define TICK_MS 33

/* Development aid (stage 3a): the USB-C port has no console while the app runs (the S31 board's USB-C is a
 * High-Speed USB port, not the USB-Serial-JTAG), so ESP_LOG output is kept in RAM and shown on the panel.
 * Triple-tap any screen corner to open or close the log view; tap the upper/lower half to scroll.
 * Set EXP_DBGLOG to 0 before the exhibition (this also removes the corner gesture). */
#ifndef EXP_DBGLOG
#define EXP_DBGLOG 1
#endif
#if EXP_DBGLOG
#define DBG_STAGE(name) exp_dbglog_stage(name)
#else
#define DBG_STAGE(name) ((void)0)
#endif

/* Measured on the board (0.3.8): the panel transfer takes 23.5 ms and is already hidden behind drawing, but the call
 * esp_lcd_panel_draw_bitmap() itself blocks the drawing thread for 8.4 ms every frame. With EXP_PRESENT_TASK=1 that call is
 * made by a separate task, so the drawing thread can start the next frame meanwhile. The task is pinned to
 * EXP_PRESENT_CORE; core 0 is the drawing thread's core, which avoids any question of the two cores' caches
 * disagreeing about the frame buffer. If the task cannot be created the code falls back to the direct call. */
#ifndef EXP_PRESENT_TASK
#define EXP_PRESENT_TASK 1
#endif
#ifndef EXP_PRESENT_CORE
#define EXP_PRESENT_CORE 0
#endif

/* Stage 3b-1: real accelerometer/gyroscope data. Set to 0 to run the display-only 3a behaviour. */
#ifndef EXP_USE_SENSORS
#define EXP_USE_SENSORS 1
#endif
static dev_audio_t s_aud;      /* latest microphone / speaker / motor state (stage 3c) */
static dev_joystick_t s_joy;   /* joystick module (stage 3d) */
static dev_slots_t s_slots;    /* latest expansion-slot state (stage 3c) */
static dev_rf_probe_t s_rfp;   /* runtime RF entry-point probe, run once at start-up */
#if EXP_USE_SENSORS
static dev_sensors_t s_diag;   /* latest snapshot, for the live lines of the log view */
#endif

static esp_lcd_panel_handle_t s_panel;
static SemaphoreHandle_t s_flush_done;
static uint16_t *s_fb[2];      /* double buffer: draw into one while the panel reads the other */
static int s_cur;
static bool s_inflight;        /* a panel transfer is still running on the other buffer */
static int64_t s_present_us;   /* time spent inside present(): panel transfer + wait for completion */
/* Where does the present() time go? Running averages (1/16 weight), microseconds:
 *   sem  = waiting for the previous transfer to finish
 *   call = esp_lcd_panel_draw_bitmap() itself (does it really return at once?)
 *   xfer = from the start of that call until the transfer-done callback ran */
static volatile int64_t s_isr_done_us;
static int64_t s_inflight_start_us;
static int64_t s_avg_sem_us, s_avg_call_us, s_avg_xfer_us;
/* Where does the rest of the frame go? running averages, microseconds */
static int64_t s_av_sens_us, s_av_touch_us, s_av_upd_us;

#if EXP_PRESENT_TASK
static QueueHandle_t s_present_q;          /* finished frames handed to the present task (buffer pointers) */
static SemaphoreHandle_t s_buf_free;       /* counting: one give per frame that has been completely sent */
static int s_handed;                       /* frames handed over so far */

static volatile int s_brightness_req = -1;   /* set by the main loop, applied here: the panel IO is idle between two frames */

static void present_task(void *arg)
{
    (void)arg;
    for (;;) {
        const uint16_t *px = NULL;
        if (xQueueReceive(s_present_q, &px, portMAX_DELAY) != pdTRUE || !px) continue;
        const int br = s_brightness_req;
        if (br >= 0) {
            s_brightness_req = -1;
            /* The previous transfer has completed (this task waits for it every frame), so the register write cannot
             * collide with colour data on the shared QSPI panel IO. */
            if (bsp_display_brightness_set(br) != ESP_OK) ESP_LOGW(TAG, "brightness change to %d %% failed", br);
        }
        const int64_t tc0 = esp_timer_get_time();
        if (esp_lcd_panel_draw_bitmap(s_panel, 0, 0, BSP_LCD_H_RES, BSP_LCD_V_RES, px) != ESP_OK) {
            ESP_LOGE(TAG, "draw_bitmap failed");
        } else {
            s_avg_call_us += ((esp_timer_get_time() - tc0) - s_avg_call_us) / 16;
            if (xSemaphoreTake(s_flush_done, pdMS_TO_TICKS(200)) != pdTRUE)
                ESP_LOGW(TAG, "panel transfer did not report completion within 200 ms");
            else
                s_avg_xfer_us += ((s_isr_done_us - tc0) - s_avg_xfer_us) / 16;
        }
        xSemaphoreGive(s_buf_free);        /* this buffer may be drawn into again */
    }
}

/* Called by the drawing thread: hand the finished frame over, then make sure the OTHER buffer (handed over one
 * frame ago) has been completely sent before it is used for the next frame. */
static void present_async(const uint16_t *pixels)
{
    const int64_t t0 = esp_timer_get_time();
    if (xQueueSend(s_present_q, &pixels, pdMS_TO_TICKS(200)) != pdTRUE)
        ESP_LOGW(TAG, "present queue full");
    if (s_handed > 0 && xSemaphoreTake(s_buf_free, pdMS_TO_TICKS(200)) != pdTRUE)
        ESP_LOGW(TAG, "previous frame not sent within 200 ms");
    ++s_handed;
    s_cur ^= 1;
    exp_surface_set_target(s_fb[s_cur]);
    const int64_t w = esp_timer_get_time() - t0;
    s_present_us = w;
    s_avg_sem_us += (w - s_avg_sem_us) / 16;   /* here "sem" = time the drawing thread was held up */
}
#endif

static bool on_color_done(esp_lcd_panel_io_handle_t io, esp_lcd_panel_io_event_data_t *ev, void *ctx)
{
    (void)io; (void)ev; (void)ctx;
    s_isr_done_us = esp_timer_get_time();
    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR(s_flush_done, &woken);
    return woken == pdTRUE;
}

static void present(const uint16_t *pixels, size_t stride, void *ctx)
{
    (void)ctx;
    (void)stride; /* the frame buffers are tightly packed: stride == width */
#if EXP_PRESENT_TASK
    if (s_present_q) { present_async(pixels); return; }
#endif
    const int64_t t0 = esp_timer_get_time();
    /* The previous frame may still be on its way to the panel (it was started during the last call and
     * overlapped with this frame's drawing). Wait for it before starting the next transfer. */
    if (s_inflight) {
        if (xSemaphoreTake(s_flush_done, pdMS_TO_TICKS(200)) != pdTRUE)
            ESP_LOGW(TAG, "panel transfer did not report completion within 200 ms");
        else
            s_avg_xfer_us += ((s_isr_done_us - s_inflight_start_us) - s_avg_xfer_us) / 16;
        s_inflight = false;
    }
    const int64_t tc0 = esp_timer_get_time();
    s_avg_sem_us += ((tc0 - t0) - s_avg_sem_us) / 16;
    if (esp_lcd_panel_draw_bitmap(s_panel, 0, 0, BSP_LCD_H_RES, BSP_LCD_V_RES, pixels) != ESP_OK) {
        ESP_LOGE(TAG, "draw_bitmap failed");
    } else {
        s_inflight = true;                  /* completion comes from the callback */
        s_inflight_start_us = tc0;
    }
    s_avg_call_us += ((esp_timer_get_time() - tc0) - s_avg_call_us) / 16;
    s_cur ^= 1;                             /* next frame is drawn into the other buffer */
    exp_surface_set_target(s_fb[s_cur]);
    s_present_us = esp_timer_get_time() - t0;   /* now: only the time spent WAITING for the panel */
}

/* One-off boot benchmark (development only): where do the ~43 ms of drawing go? Everything is measured on this
 * board; nothing is assumed. Memory tests are scaled to "ms per full 480x480 frame" so they can be compared with
 * the draw time. Pipeline tests call the real drawing code. Shown on the panel (the serial console is not
 * reachable while the app runs). Turn off with EXP_BOOT_BENCH=0. */
#ifndef EXP_BOOT_BENCH
#define EXP_BOOT_BENCH 1
#endif
#if EXP_BOOT_BENCH
typedef struct {
    int p_fill, p_rmw, p_copy;   /* PSRAM frame buffer: write, read-modify-write, memcpy      (us / frame) */
    int i_fill, i_rmw;           /* internal RAM strip, scaled to a whole frame               (us / frame) */
    int p_fill32, i_fill32;      /* the same fills with 32-bit stores (what the shim now uses for solid rects) */
    int bg, txt, pnl, full;      /* exp_background, one exp_text, one exp_panel, whole render (us / call)  */
    bool done;
} bench_t;
static bench_t s_bench;

static int64_t bench_fill(volatile uint16_t *d, size_t n)
{
    const int64_t t = esp_timer_get_time();
    for (size_t i = 0; i < n; ++i) d[i] = (uint16_t)i;
    return esp_timer_get_time() - t;
}
static int64_t bench_fill32(volatile uint32_t *d, size_t n32)
{
    const int64_t t = esp_timer_get_time();
    for (size_t i = 0; i < n32; ++i) d[i] = (uint32_t)i * 0x00010001u;
    return esp_timer_get_time() - t;
}
static int64_t bench_rmw(volatile uint16_t *d, size_t n)
{
    const int64_t t = esp_timer_get_time();
    for (size_t i = 0; i < n; ++i) d[i] = (uint16_t)((d[i] >> 1) + (uint16_t)i);
    return esp_timer_get_time() - t;
}

static void run_boot_bench(exp_app_t *app)
{
    const size_t n = (size_t)BSP_LCD_H_RES * BSP_LCD_V_RES;
    const size_t strip_rows = 40, strip_n = (size_t)BSP_LCD_H_RES * strip_rows;
    uint16_t *strip = heap_caps_malloc(strip_n * sizeof(uint16_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    int64_t pf = 0, pr = 0, pc = 0, ifl = 0, ir = 0, pf32 = 0, if32 = 0;
    for (int k = 0; k < 3; ++k) {
        pf += bench_fill(s_fb[1], n);
        pf32 += bench_fill32((volatile uint32_t *)s_fb[1], n / 2);
        pr += bench_rmw(s_fb[1], n);
        const int64_t t = esp_timer_get_time();
        memcpy(s_fb[1], s_fb[0], n * sizeof(uint16_t));
        pc += esp_timer_get_time() - t;
        if (strip) {
            ifl += bench_fill(strip, strip_n);
            if32 += bench_fill32((volatile uint32_t *)strip, strip_n / 2);
            ir += bench_rmw(strip, strip_n);
        }
    }
    s_bench.p_fill = (int)(pf / 3);
    s_bench.p_rmw = (int)(pr / 3);
    s_bench.p_fill32 = (int)(pf32 / 3);
    s_bench.i_fill32 = (int)(if32 / 3 * (BSP_LCD_V_RES / strip_rows));
    s_bench.p_copy = (int)(pc / 3);
    s_bench.i_fill = (int)(ifl / 3 * (BSP_LCD_V_RES / strip_rows));
    s_bench.i_rmw = (int)(ir / 3 * (BSP_LCD_V_RES / strip_rows));
    free(strip);

    const uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);
    const Color white = {255, 255, 255, 255};
    BeginDrawing();
    if (MosaicoFastFrameAvailable()) {
        int64_t t = esp_timer_get_time();
        for (int k = 0; k < 5; ++k) exp_background(now_ms);
        s_bench.bg = (int)((esp_timer_get_time() - t) / 5);

        t = esp_timer_get_time();
        for (int k = 0; k < 40; ++k) exp_text("Advanced HMI & Motion Sensing", 20, 100 + (k % 8) * 20, EXP_FONT_BODY, white, EXP_ALIGN_LEFT);
        s_bench.txt = (int)((esp_timer_get_time() - t) / 40);

        t = esp_timer_get_time();
        for (int k = 0; k < 20; ++k) exp_panel(20, 100 + (k % 5) * 50, 400, 44, 12, (Color){20, 40, 90, 200}, (Color){60, 120, 220, 255});
        s_bench.pnl = (int)((esp_timer_get_time() - t) / 20);

        t = esp_timer_get_time();
        for (int k = 0; k < 5; ++k) exp_app_render(app);
        s_bench.full = (int)((esp_timer_get_time() - t) / 5);
    }
    EndDrawing();
    s_bench.done = true;
}
#endif

/* Development aid (stage 3a): a one-line frame-cost readout on the panel.
 * Disabled now since ESP-Iris over USB CDC0 provides live real-time logging without covering the screen. */
#ifndef EXP_TIMING_OVERLAY
#define EXP_TIMING_OVERLAY 0
#endif
#if EXP_RELEASE && (EXP_TIMING_OVERLAY || EXP_BOOT_BENCH || EXP_DBGLOG)
#error "EXP_RELEASE must not include the timing overlay, the boot benchmark or the screen log"
#endif
#if EXP_TIMING_OVERLAY
static void draw_timing_overlay(int64_t now_us_v, int64_t max_us, int64_t draw_us, int64_t present_us)
{
    char line[64];
    snprintf(line, sizeof(line), "now %d.%d max %d.%d d %d.%d wait %d.%d ms",
             (int)(now_us_v / 1000), (int)((now_us_v % 1000) / 100),
             (int)(max_us / 1000), (int)((max_us % 1000) / 100),
             (int)(draw_us / 1000), (int)((draw_us % 1000) / 100),
             (int)(present_us / 1000), (int)((present_us % 1000) / 100));
#if EXP_BOOT_BENCH
    const int rows = (s_bench.done ? 5 : 1) + 2;
#else
    const int rows = 1 + 2;
#endif
    const Color yel = {255, 255, 0, 255};
    exp_rect(70, 2, 340, 18 * rows + 2, (Color){0, 0, 0, 200});
    exp_text(line, 240, 4, EXP_FONT_CAP, yel, EXP_ALIGN_CENTER);
    {
        char lp[64];
        snprintf(lp, sizeof(lp), "present sem %d.%d call %d.%d xfer %d.%d ms",
                 (int)(s_avg_sem_us / 1000), (int)((s_avg_sem_us % 1000) / 100),
                 (int)(s_avg_call_us / 1000), (int)((s_avg_call_us % 1000) / 100),
                 (int)(s_avg_xfer_us / 1000), (int)((s_avg_xfer_us % 1000) / 100));
        exp_text(lp, 240, 4 + 18 * (rows - 2), EXP_FONT_CAP, yel, EXP_ALIGN_CENTER);
        snprintf(lp, sizeof(lp), "sens %d.%d touch %d.%d upd %d.%d ms",
                 (int)(s_av_sens_us / 1000), (int)((s_av_sens_us % 1000) / 100),
                 (int)(s_av_touch_us / 1000), (int)((s_av_touch_us % 1000) / 100),
                 (int)(s_av_upd_us / 1000), (int)((s_av_upd_us % 1000) / 100));
        exp_text(lp, 240, 4 + 18 * (rows - 1), EXP_FONT_CAP, yel, EXP_ALIGN_CENTER);
    }
#if EXP_BOOT_BENCH
    if (s_bench.done) {
        char l2[64], l3[64], l4[64], l5[64];
        snprintf(l2, sizeof(l2), "PSRAM/frame fill %d rmw %d cpy %d us", s_bench.p_fill, s_bench.p_rmw, s_bench.p_copy);
        snprintf(l3, sizeof(l3), "IRAM/frame fill %d rmw %d us", s_bench.i_fill, s_bench.i_rmw);
        snprintf(l4, sizeof(l4), "bg %d txt %d pnl %d full %d us", s_bench.bg, s_bench.txt, s_bench.pnl, s_bench.full);
        exp_text(l2, 240, 22, EXP_FONT_CAP, yel, EXP_ALIGN_CENTER);
        exp_text(l3, 240, 40, EXP_FONT_CAP, yel, EXP_ALIGN_CENTER);
        exp_text(l4, 240, 58, EXP_FONT_CAP, yel, EXP_ALIGN_CENTER);
        snprintf(l5, sizeof(l5), "fill32 PSRAM %d IRAM %d us", s_bench.p_fill32, s_bench.i_fill32);
        exp_text(l5, 240, 76, EXP_FONT_CAP, yel, EXP_ALIGN_CENTER);
    }
#endif
}
#endif

#if EXP_DBGLOG
#define DBGLOG_VIEW_MAX 128
static bool s_log_view;
static int s_log_scroll;      /* lines hidden below the bottom of the view (0 = newest) */
static int s_log_page;        /* 0 = sensors, 1 = audio / motor / slots / RF (tap the two header lines to switch) */

typedef struct {
    char t[72];
    bool cont;     /* continuation of a wrapped line: drawn indented */
    Color c;
} vrow_t;

/* Appends the visual rows of one log line to rows[] in reverse order (bottom row first), wrapping to max_w. */
static void log_wrap_push(vrow_t *rows, int *nv, int cap, const char *ln, int max_w, Color c)
{
    int start[4], len[4], ns = 0;
    const int L = (int)strlen(ln);
    int pos = 0;
    while (pos < L && ns < 4) {
        char tmp[72];
        int n = 0;
        const int w_lim = ns ? max_w - 14 : max_w;
        while (pos + n < L && n < (int)sizeof(tmp) - 1) {
            memcpy(tmp, ln + pos, (size_t)n + 1);
            tmp[n + 1] = '\0';
            if (exp_text_width(tmp, EXP_FONT_CAP) > w_lim) break;
            ++n;
        }
        if (n == 0) n = 1;
        start[ns] = pos; len[ns] = n; ++ns;
        pos += n;
    }
    if (ns == 0) { start[0] = 0; len[0] = 0; ns = 1; }
    for (int k = ns - 1; k >= 0 && *nv < cap; --k) {
        vrow_t *r = &rows[(*nv)++];
        memcpy(r->t, ln + start[k], (size_t)len[k]);
        r->t[len[k]] = '\0';
        r->cont = k > 0;
        r->c = c;
    }
}

static dev_sweep_t s_sweep __attribute__((unused));   /* compass rotation-consistency check (log view) */
#if EXP_USE_MAG && EXP_USE_SENSORS
/* one-line compass calibration status for the log view (filled by the main loop) */
static char s_cal_line[72] = "cal: --";
static char s_head_line[80] = "B -- ";
static char s_sweep_l1[80] = "sweep: flat, turn once around";
static char s_sweep_l2[100] = "";
#endif
#if EXP_USE_AUDIO || EXP_USE_MOTOR || EXP_USE_SLOTS
#define IO_PAGE_ROWS 10
static const char *slot_class_name(dev_slot_class_t c)
{
    switch (c) {
    case DEV_SLOT_NOT_PROBED: return "not probed";
    case DEV_SLOT_NONE: return "none";
    case DEV_SLOT_JOYSTICK: return "joystick";
    case DEV_SLOT_INTERACT: return "interact";
    case DEV_SLOT_BUTTON_LED: return "button+led";
    case DEV_SLOT_CAMERA: return "camera";
    default: return "UNKNOWN module";
    }
}

/* Page 2 of the log view: the stage 3c peripherals. Everything shown is a measured or reported state; "starting" means
 * the driver has not reported yet. Tap the rows to play the test tone and a 60 ms motor pulse. */
static void draw_io_rows(int top, int lh, Color col)
{
    char h[100];
    dev_audio_take(&s_aud);
    dev_slots_take(&s_slots);
    snprintf(h, sizeof(h), "mic %s  reads %u err %u", s_aud.mic_state, (unsigned)s_aud.mic_reads, (unsigned)s_aud.mic_errors);
    exp_text(h, 240, top + 2 * lh, EXP_FONT_CAP, col, EXP_ALIGN_CENTER);
    snprintf(h, sizeof(h), "peak L %d  R %d  slot %s  fresh %s", s_aud.peak_l, s_aud.peak_r, s_aud.mic_slot ? "R" : "L",
             s_aud.mic_ok ? "yes" : "no");
    exp_text(h, 240, top + 3 * lh, EXP_FONT_CAP, col, EXP_ALIGN_CENTER);
    snprintf(h, sizeof(h), "spk %s  cues %u drop %u err %u  last %d", s_aud.spk_state, (unsigned)s_aud.spk_cues,
             (unsigned)s_aud.spk_dropped, (unsigned)s_aud.spk_errors, s_aud.last_cue);
    exp_text(h, 240, top + 4 * lh, EXP_FONT_CAP, col, EXP_ALIGN_CENTER);
    snprintf(h, sizeof(h), "motor %s  pulses %u denied %u err %u", s_aud.motor_state, (unsigned)s_aud.haptic_pulses,
             (unsigned)s_aud.haptic_denied, (unsigned)s_aud.motor_errors);
    exp_text(h, 240, top + 5 * lh, EXP_FONT_CAP, col, EXP_ALIGN_CENTER);
    snprintf(h, sizeof(h), "slots %s  polls %u", s_slots.state, (unsigned)s_slots.polls);
    exp_text(h, 240, top + 6 * lh, EXP_FONT_CAP, col, EXP_ALIGN_CENTER);
    for (int i = 0; i < 2; ++i) {
        snprintf(h, sizeof(h), "%s: %s  pres %u  desc %s  type 0x%02X  err %d", i ? "right" : "left",
                 slot_class_name(s_slots.cls[i]), (unsigned)s_slots.presence[i], s_slots.descriptor_valid[i] ? "ok" : "--",
                 (unsigned)s_slots.board_type[i], s_slots.last_error[i]);
        exp_text(h, 240, top + (7 + i) * lh, EXP_FONT_CAP, col, EXP_ALIGN_CENTER);
    }
#if EXP_USE_JOYSTICK
    dev_joystick_take(&s_joy);
    snprintf(h, sizeof(h), "joy %s  slot %d  n %u err %u claim %u/%u", s_joy.state, (int)s_joy.slot, (unsigned)s_joy.reads,
             (unsigned)s_joy.errors, (unsigned)s_joy.claim_errors, (unsigned)s_joy.claims);
    exp_text(h, 240, top + 11 * lh, EXP_FONT_CAP, col, EXP_ALIGN_CENTER);
    snprintf(h, sizeof(h), "joy raw %d %d  drv %+.2f %+.2f  app %+.2f %+.2f  btn %02X", s_joy.raw_x, s_joy.raw_y,
             (double)s_joy.drv_x, (double)s_joy.drv_y, (double)s_joy.x, (double)s_joy.y, (unsigned)s_joy.buttons);
    exp_text(h, 240, top + 12 * lh, EXP_FONT_CAP, col, EXP_ALIGN_CENTER);
#endif
    snprintf(h, sizeof(h), "rf probe: %d of %d radio entry points in image%s%s", s_rfp.present, s_rfp.checked,
             s_rfp.first ? " first " : "", s_rfp.first ? s_rfp.first : "");
    exp_text(h, 240, top + 9 * lh, EXP_FONT_CAP, s_rfp.present ? (Color){255, 110, 110, 255} : col, EXP_ALIGN_CENTER);
    exp_text("software state only - not an RF measurement", 240, top + 10 * lh, EXP_FONT_CAP, col, EXP_ALIGN_CENTER);
#if EXP_VOICE
    dev_voice_take(&s_voice);
    static const char *const ph[] = {"off", "loading", "ready", "listening", "ERROR", "skipped"};
    snprintf(h, sizeof(h), "voice %s  %s  wake2 %s  load %d ms", ph[s_voice.vs.phase <= DEV_VPH_SKIPPED ? s_voice.vs.phase : 0],
             s_voice.detail ? s_voice.detail : "", s_voice.wake2_loaded ? "yes" : "no", (int)s_voice.load_ms);
    exp_text(h, 240, top + 13 * lh, EXP_FONT_CAP, s_voice.vs.phase == DEV_VPH_ERROR ? (Color){255, 110, 110, 255} : col, EXP_ALIGN_CENTER);
    snprintf(h, sizeof(h), "voice wake %u cmd %u none %u tmo %u  last %d/%u%%  fetch %u drop %u", (unsigned)s_voice.vs.n_wake,
             (unsigned)s_voice.vs.n_cmd, (unsigned)s_voice.vs.n_reject, (unsigned)s_voice.vs.n_timeout, (int)s_voice.vs.cmd,
             (unsigned)s_voice.vs.prob_pct, (unsigned)s_voice.fetch_loops, (unsigned)s_voice.ring_dropped);
    exp_text(h, 240, top + 14 * lh, EXP_FONT_CAP, col, EXP_ALIGN_CENTER);
#else
    exp_text("voice: not in this build (EXP_VOICE=0)", 240, top + 13 * lh, EXP_FONT_CAP, col, EXP_ALIGN_CENTER);
#endif
}
#endif

static void draw_log_view(void)
{
    const Color bg = {0, 0, 0, 255}, yel = {255, 255, 0, 255}, red = {255, 110, 110, 255};
    const Color warn = {255, 200, 80, 255}, txt = {210, 210, 210, 255};
    const Color cyan __attribute__((unused)) = {120, 220, 255, 255};
    const int lh = exp_text_line_height(EXP_FONT_CAP);
    const int top = 30, bottom = 444, max_w = 400;
#if EXP_USE_AUDIO || EXP_USE_MOTOR || EXP_USE_SLOTS
    const bool io_page = s_log_page == 1;
#else
    const bool io_page = false;
#endif
#if EXP_USE_SENSORS
#if EXP_USE_MAG
    const int head_rows = io_page ? 15 : 13;
#else
    const int head_rows = io_page ? 15 : 7;
#endif
#else
    const int head_rows = io_page ? 15 : 2;
#endif
    const int rows = (bottom - top - head_rows * lh) / lh;
    const int total = (int)exp_dbglog_lines();
    const int max_scroll = total > 1 ? total - 1 : 0;
    if (s_log_scroll > max_scroll) s_log_scroll = max_scroll;
    if (s_log_scroll < 0) s_log_scroll = 0;

    exp_rect(0, 0, 480, 480, bg);
    char h[96];
    snprintf(h, sizeof(h), "boot #%u  reset: %s  [%d/%d]", (unsigned)exp_dbglog_boot_count(), exp_dbglog_reset_name(),
             total - s_log_scroll, total);
    exp_text(h, 240, top, EXP_FONT_CAP, yel, EXP_ALIGN_CENTER);
    snprintf(h, sizeof(h), "prev stage '%s'  fw %s", exp_dbglog_prev_stage(), FW_VERSION);
    exp_text(h, 240, top + lh, EXP_FONT_CAP, yel, EXP_ALIGN_CENTER);
#if EXP_USE_AUDIO || EXP_USE_MOTOR || EXP_USE_SLOTS
    if (io_page) draw_io_rows(top, lh, cyan);
#endif
    if (!io_page) {
#if EXP_USE_SENSORS
    snprintf(h, sizeof(h), "imu %s  age %d ms  n %u  err %u", s_diag.imu_state ? s_diag.imu_state : "?",
             s_diag.imu_age_ms == UINT32_MAX ? -1 : (int)s_diag.imu_age_ms, (unsigned)s_diag.imu_samples,
             (unsigned)s_diag.imu_errors);
    exp_text(h, 240, top + 2 * lh, EXP_FONT_CAP, cyan, EXP_ALIGN_CENTER);
    snprintf(h, sizeof(h), "acc %+.2f %+.2f %+.2f g (chip axes)", (double)s_diag.acc_raw[0], (double)s_diag.acc_raw[1],
             (double)s_diag.acc_raw[2]);
    exp_text(h, 240, top + 3 * lh, EXP_FONT_CAP, cyan, EXP_ALIGN_CENTER);
    snprintf(h, sizeof(h), "gyr %+.1f %+.1f %+.1f dps (chip axes)", (double)s_diag.gyr_raw[0], (double)s_diag.gyr_raw[1],
             (double)s_diag.gyr_raw[2]);
    exp_text(h, 240, top + 4 * lh, EXP_FONT_CAP, cyan, EXP_ALIGN_CENTER);
    snprintf(h, sizeof(h), "turned x %+.0f  y %+.0f  z %+.0f deg (tap here: zero)", (double)s_diag.gyr_angle[0],
             (double)s_diag.gyr_angle[1], (double)s_diag.gyr_angle[2]);
    exp_text(h, 240, top + 5 * lh, EXP_FONT_CAP, cyan, EXP_ALIGN_CENTER);
    snprintf(h, sizeof(h), "batt %s  %d mV  %d mA  %u %%  n %u err %u", s_diag.batt_state ? s_diag.batt_state : "?",
             s_diag.batt_mv, s_diag.batt_ma, (unsigned)s_diag.batt_pct, (unsigned)s_diag.batt_reads,
             (unsigned)s_diag.batt_errors);
    exp_text(h, 240, top + 6 * lh, EXP_FONT_CAP, cyan, EXP_ALIGN_CENTER);
#if EXP_USE_MAG
    for (int id = 0; id < DEV_MAG_N; ++id) {
        const float b = sqrtf(s_diag.mag_ut[id][0] * s_diag.mag_ut[id][0] + s_diag.mag_ut[id][1] * s_diag.mag_ut[id][1] +
                              s_diag.mag_ut[id][2] * s_diag.mag_ut[id][2]);
        snprintf(h, sizeof(h), "mag%d %s  %+d %+d %+d  |B| %.0f uT  n%u e%u", id,
                 s_diag.mag_state[id] ? s_diag.mag_state[id] : "?", s_diag.mag_raw[id][0], s_diag.mag_raw[id][1],
                 s_diag.mag_raw[id][2], (double)b, (unsigned)s_diag.mag_reads[id], (unsigned)s_diag.mag_errors[id]);
        exp_text(h, 240, top + (7 + id) * lh, EXP_FONT_CAP, cyan, EXP_ALIGN_CENTER);
    }
    exp_text(s_cal_line, 240, top + (7 + DEV_MAG_N) * lh, EXP_FONT_CAP, cyan, EXP_ALIGN_CENTER);
    exp_text(s_head_line, 240, top + (8 + DEV_MAG_N) * lh, EXP_FONT_CAP, cyan, EXP_ALIGN_CENTER);
    exp_text(s_sweep_l1, 240, top + (9 + DEV_MAG_N) * lh, EXP_FONT_CAP, cyan, EXP_ALIGN_CENTER);
    exp_text(s_sweep_l2, 240, top + (10 + DEV_MAG_N) * lh, EXP_FONT_CAP, cyan, EXP_ALIGN_CENTER);
#endif
#endif
    }

    static vrow_t vr[32];
    int nv = 0;
    char ln[DBGLOG_VIEW_MAX];
    for (int back = s_log_scroll; nv < rows && nv < 32; ++back) {
        if (!exp_dbglog_line((size_t)back, ln, sizeof(ln))) break;
        log_wrap_push(vr, &nv, rows < 32 ? rows : 32, ln, max_w, ln[0] == 'E' ? red : (ln[0] == 'W' ? warn : txt));
    }
    for (int i = 0; i < nv; ++i)                                /* vr[0] is the bottom row */
        exp_text(vr[i].t, vr[i].cont ? 54 : 40, top + (head_rows + rows - 1 - i) * lh, EXP_FONT_CAP, vr[i].c,
                 EXP_ALIGN_LEFT);
}

/* Called on every new touch-down. Returns true when the tap belongs to the log view and must not reach the app. */
static bool log_view_tap(int x, int y)
{
    static int64_t first_us;
    static int taps;
    const bool corner = (x < 90 || x >= 390) && (y < 90 || y >= 390);
    if (corner) {
        const int64_t t = esp_timer_get_time();
        if (taps == 0 || t - first_us > 2000000) { taps = 0; first_us = t; }
        if (++taps >= 3) {
            taps = 0;
            s_log_view = !s_log_view;
            s_log_scroll = 0;
            s_log_page = 0;
#if EXP_USE_SENSORS
            if (s_log_view) { dev_sensors_reset_angles(); dev_sweep_reset(&s_sweep); }   /* angle line and sweep start when the view opens */
#endif
            ESP_LOGI(TAG, "log view %s", s_log_view ? "on" : "off");
            return true;
        }
        return s_log_view;                                     /* corner taps only count while the view is open */
    }
    if (s_log_view) {
#if EXP_USE_AUDIO || EXP_USE_MOTOR || EXP_USE_SLOTS
        if (y < 62 && x >= 90 && x < 390) {                    /* the two header lines: switch page */
            s_log_page ^= 1;
            s_log_scroll = 0;
            return true;
        }
        if (s_log_page == 1 && y < 190 && x >= 90 && x < 390) {   /* the I/O rows: play the test tone + a short pulse */
            dev_audio_play(DEV_CUE_TEST_TONE);
            dev_haptic_pulse(60);
            return true;
        }
#endif
#if EXP_USE_SENSORS
        if (y < 150 && x >= 90 && x < 390) {                   /* the sensor lines at the top: zero the turned angle */
            dev_sensors_reset_angles();
            dev_sweep_reset(&s_sweep);
            return true;
        }
#endif
        s_log_scroll += (y < 240) ? 6 : -6;                    /* upper half: older, lower half: newer */
        return true;
    }
    return false;
}
#endif

#if EXP_VISION
/* ---- Camera & Face Tracking (stage 4a/4b/4c) ------------------------------------------------------------------------
 * Opened on-demand when either:
 *   1. The visitor selects Camera (INPUT_FACE) on the Game page (exp_app_want_camera(app) == true), or
 *   2. The developer opens the camera test view (triple-tap bottom centre when EXP_DBGLOG=1).
 * Leaving the Game page / switching input / closing the test view stops the stream and releases the USB Serial/JTAG pad. */
static bool s_cam_view;
static bool s_cam_active;
static int s_cam_orient;
static const char *s_cam_refusal;          /* non-NULL: the slot check refused to start the camera (text shown on the panel) */

static void cam_sync(bool want)
{
    if (want && !s_cam_active) {
        dev_slots_take(&s_slots);
        const dev_cam_gate_t g = dev_cam_gate(s_slots.cls[0], s_slots.descriptor_valid[0]);
        s_cam_refusal = g.allow ? NULL : g.reason;
        if (g.allow) {
            if (g.allow_unidentified) ESP_LOGW(TAG, "camera start: %s", g.reason);
            else ESP_LOGI(TAG, "camera start: %s", g.reason);
            DBG_STAGE("camera");
            dev_cam_set_orient(s_cam_orient);
            dev_cam_request(true, g.allow_unidentified);
            dev_face_detect_start();
            s_cam_active = true;
        } else {
            ESP_LOGW(TAG, "camera refused: %s", g.reason);
        }
    } else if (!want && s_cam_active) {
        dev_face_detect_stop();
        dev_cam_request(false, false);
        s_cam_active = false;
        s_cam_refusal = NULL;
        DBG_STAGE("loop");
        ESP_LOGI(TAG, "camera stopped");
    }
}

static void cam_view_open(void)
{
    s_cam_view = true;
    cam_sync(true);
}

static void cam_view_close(void)
{
    s_cam_view = false;
    cam_sync(false);
}

/* Called on every new touch-down. Returns true when the tap belongs to the camera view and must not reach the app. */
static bool cam_view_tap(int x, int y)
{
#if !EXP_DBGLOG
    (void)x; (void)y;
    return false;
#else
    if (s_cam_view) {
        if (x >= 390 && y < 90) { cam_view_close(); return true; }
        s_cam_orient = dev_cam_orient_next(s_cam_orient);
        dev_cam_set_orient(s_cam_orient);
        return true;
    }
    if (s_log_view) return false;                              /* taps on the log view scroll it */
    static int64_t first_us;
    static int taps;
    if (y >= 400 && x >= 150 && x < 330) {
        const int64_t t = esp_timer_get_time();
        if (taps == 0 || t - first_us > 2000000) { taps = 0; first_us = t; }
        if (++taps >= 3) { taps = 0; cam_view_open(); return true; }
    }
    return false;
#endif
}

static void cam_text(const char *t, int line, Color c)
{
    exp_text(t, 240, 318 + line * 17, EXP_FONT_CAP, c, EXP_ALIGN_CENTER);
}

/* Fills the frame with the newest camera picture (or black) and writes the measured numbers over its lower part. */
static void draw_cam_view(void)
{
    dev_cam_status_t st;
    dev_cam_take(&st);
    dev_face_result_t face_res = {0};
    dev_face_detect_get_result(&face_res);
    const Color yel = {255, 255, 0, 255}, red = {255, 110, 110, 255}, wht = {235, 235, 235, 255}, grn = {80, 255, 120, 255};
    const bool have = s_cam_refusal == NULL && dev_cam_copy_view(s_fb[s_cur]);
    if (!have) exp_rect(0, 0, 480, 480, (Color){0, 0, 0, 255});

    if (face_res.found && have) {
        const dev_face_box_t b = dev_face_box_clamp(face_res.box, 480, 480);
        if (b.w > 4 && b.h > 4) {
            exp_rect(b.x, b.y, b.w, 2, yel);
            exp_rect(b.x, b.y + b.h - 2, b.w, 2, yel);
            exp_rect(b.x, b.y, 2, b.h, yel);
            exp_rect(b.x + b.w - 2, b.y, 2, b.h, yel);
        }
    }

    exp_rect(30, 310, 420, 150, (Color){0, 0, 0, 190});
    static const char *const ph[] = {"off", "starting", "RUNNING", "ERROR", "stopping"};
    char l[96];
    if (s_cam_refusal) {
        cam_text("camera NOT started", 0, red);
        cam_text(s_cam_refusal, 1, red);
        cam_text("top-right corner: leave", 6, wht);
        return;
    }
    snprintf(l, sizeof(l), "camera %s: %s", ph[st.phase <= DEV_CAM_STOPPING ? st.phase : 0], st.detail ? st.detail : "");
    cam_text(l, 0, st.phase == DEV_CAM_ERROR ? red : yel);

    if (face_res.found) {
        const dev_face_box_t b = face_res.box;
        snprintf(l, sizeof(l), "hand FOUND (%.0f%%)  infer %d.%d ms (%.1f fps)  box [%d,%d,%d,%d]",
                 (double)(face_res.score * 100.0f),
                 face_res.infer_us / 1000, (face_res.infer_us % 1000) / 100,
                 (double)face_res.fps, b.x, b.y, b.w, b.h);
        cam_text(l, 1, grn);
    } else {
        snprintf(l, sizeof(l), "hand NONE  infer %d.%d ms (%.1f fps)  runs %u",
                 face_res.infer_us / 1000, (face_res.infer_us % 1000) / 100,
                 (double)face_res.fps, (unsigned)face_res.total_runs);
        cam_text(l, 1, yel);
    }

    snprintf(l, sizeof(l), "cam %.1f fps  wait %d.%d ppa %d.%d swap %d.%d loop %d.%d ms",
             (double)st.fps,
             st.wait_us / 1000, (st.wait_us % 1000) / 100,
             st.ppa_us / 1000, (st.ppa_us % 1000) / 100,
             st.swap_us / 1000, (st.swap_us % 1000) / 100,
             st.total_us / 1000, (st.total_us % 1000) / 100);
    cam_text(l, 2, wht);

    const uint32_t f = st.src_fmt;
    snprintf(l, sizeof(l), "src %ux%u %c%c%c%c crop %u scale %s  open %d ms", (unsigned)st.src_w, (unsigned)st.src_h,
             f ? (char)(f & 255) : '-', f ? (char)((f >> 8) & 255) : '-', f ? (char)((f >> 16) & 255) : '-',
             f ? (char)((f >> 24) & 255) : '-', (unsigned)st.crop, st.scale_exact ? "exact" : "rounded", st.open_ms);
    cam_text(l, 3, wht);

    const dev_cam_orient_t o = dev_cam_orient(st.orient);
    snprintf(l, sizeof(l), "orient %d/%d: rot %d mirror x%d y%d  psram free %u KB", st.orient + 1, DEV_CAM_ORIENT_N, o.rot_deg,
             o.mirror_x, o.mirror_y, (unsigned)(st.psram_free / 1024));
    cam_text(l, 4, wht);

    cam_text("tap: next orientation   top-right corner: leave", 5, wht);
    cam_text("ESPDet Hand Benchmark - measured values only", 6, wht);
}

/* Draws a live Picture-in-Picture camera thumbnail with crosshair and hand tracking box when Camera input is active on the Game page. */
static void draw_cam_pip(const exp_app_t *app)
{
    if (!exp_app_want_camera(app)) return;
    const int phase = exp_app_debug_maze_phase(app);
    /* Menu preview panel: (300, 56, 164, 160) -> inner (306, 62, 152, 148)
     * Gameplay HUD PiP:   (360, 4, 52, 48)    -> inner (362, 6, 48, 44) right next to Exit button */
    int px = 306, py = 62, pw = 152, ph = 148;
    if (phase != 0) {
        px = 362; py = 6; pw = 48; ph = 44;
    }
    if (!dev_cam_copy_pip(s_fb[s_cur], px, py, pw, ph)) return;

    const Color cyn = {63, 208, 230, 200}, yel = {255, 255, 0, 255};
    /* Subtle center crosshair showing neutral (240, 240) hand reference */
    const int cx = px + pw / 2, cy = py + ph / 2;
    exp_rect(cx - 5, cy, 11, 1, cyn);
    exp_rect(cx, cy - 5, 1, 11, cyn);

    dev_face_result_t res = {0};
    dev_face_detect_get_result(&res);
    if (res.found) {
        const dev_face_box_t b = dev_face_box_clamp(res.box, 480, 480);
        const int bx = px + (b.x * pw) / 480;
        const int by = py + (b.y * ph) / 480;
        int bw = (b.w * pw) / 480;
        int bh = (b.h * ph) / 480;
        if (bw < 6) bw = 6;
        if (bh < 6) bh = 6;
        if (bx + bw > px + pw) bw = px + pw - bx;
        if (by + bh > py + ph) bh = py + ph - by;
        if (bw > 2 && bh > 2) {
            exp_rect(bx, by, bw, 2, yel);
            exp_rect(bx, by + bh - 2, bw, 2, yel);
            exp_rect(bx, by, 2, bh, yel);
            exp_rect(bx + bw - 2, by, 2, bh, yel);
        }
    }
}
#endif

static uint32_t now_us(void) { return (uint32_t)esp_timer_get_time(); }

#if EXP_USE_SLOTS
static exp_module_t slot_to_module(dev_slot_class_t c)
{
    switch (c) {
    case DEV_SLOT_NONE: return EXP_MOD_NONE;
    case DEV_SLOT_JOYSTICK: return EXP_MOD_JOYSTICK;
    case DEV_SLOT_INTERACT: return EXP_MOD_INTERACT;
    case DEV_SLOT_BUTTON_LED: return EXP_MOD_BUTTON_LED;
    case DEV_SLOT_CAMERA: return EXP_MOD_CAMERA;
    case DEV_SLOT_UNKNOWN: return EXP_MOD_UNKNOWN;
    default: return EXP_MOD_NOT_PROBED;
    }
}
#endif

static void fill_sensors(exp_sensors_t *s)
{
    memset(s, 0, sizeof(*s));
    const uint32_t ms = (uint32_t)(esp_timer_get_time() / 1000);
    s->now_ms = ms;
    s->uptime_s = ms / 1000;
    s->flags = 0;                         /* a flag is set only when real data of that kind is present */
#if EXP_USE_SENSORS
    dev_sensors_take(&s_diag);
    if (s_diag.imu_ok) {                  /* BMI270 sample younger than 300 ms */
        s->flags |= EXP_SENS_IMU;
        memcpy(s->acc, s_diag.acc, sizeof(s->acc));
        memcpy(s->gyr, s_diag.gyr, sizeof(s->gyr));
    }
#if EXP_USE_MAG && EXP_MAG_FEED_APP
#if EXP_MAG_SINGLE_SENSOR
    if (s_diag.mag_ok[0]) {                       /* mag1 clips next to its magnets; mag0 stands in for both slots */
        s->flags |= EXP_SENS_MAG;
        memcpy(s->mag[0], s_diag.mag_ut[0], sizeof(s->mag[0]));
        memcpy(s->mag[1], s_diag.mag_ut[0], sizeof(s->mag[1]));
    }
#else
    if (s_diag.mag_ok[0] && s_diag.mag_ok[1]) {
        s->flags |= EXP_SENS_MAG;
        memcpy(s->mag, s_diag.mag_ut, sizeof(s->mag));
    }
#endif
#endif
    if (s_diag.batt_ok) {
        s->flags |= EXP_SENS_BATT;
        s->battery_pct = s_diag.batt_pct;
        s->charging = s_diag.charging;
    }
#endif
    s->pcm_count = 0;
#if EXP_USE_AUDIO && EXP_USE_MIC
    dev_audio_take(&s_aud);
    if (s_aud.mic_ok && s_aud.pcm_count >= EXP_PCM_N) {      /* a chunk younger than DEV_MIC_FRESH_MS */
        s->flags |= EXP_SENS_MIC;
        memcpy(s->pcm, s_aud.pcm, sizeof(s->pcm));
        s->pcm_count = EXP_PCM_N;
    }
#endif
    /* Software-visible state only: "SOFT_OFF_OK" = none of the radio entry points exists in this image. It is neither an
     * RF measurement nor a compliance statement (the product status stays "RF Verification Pending"). */
    s->rf = s_rfp.checked == 0 ? EXP_RF_UNKNOWN : (s_rfp.present == 0 ? EXP_RF_SOFT_OFF_OK : EXP_RF_SOFT_CHECK_FAIL);
    s->ai = EXP_AI_NOT_INTEGRATED;
#if EXP_VOICE
    dev_voice_take(&s_voice);
    s->ai = (exp_ai_state_t)dev_vs_ai_state(s_voice.vs.phase);
    s->voice_listening = s_voice.vs.phase == DEV_VPH_LISTENING;
    s->voice_event = (exp_voice_event_t)s_voice.vs.event;
    s->voice_cmd = (exp_voice_cmd_t)s_voice.vs.cmd;
    s->voice_wake_idx = s_voice.vs.wake_idx;
    s->voice_prob_pct = s_voice.vs.prob_pct;
    s->voice_seq = s_voice.vs.seq;
    if (dev_magnet_is_logo_active()) {
        s->voice_event = EXP_VEV_NONE;
        s->voice_cmd = EXP_VCMD_NONE;
    }
#endif
    s->heap_internal_free = (uint32_t)heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    s->heap_internal_total = (uint32_t)heap_caps_get_total_size(MALLOC_CAP_INTERNAL);
    s->heap_psram_free = (uint32_t)heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    s->heap_psram_total = (uint32_t)heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
    s->heap_largest_free = (uint32_t)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
#if EXP_USE_SLOTS
    dev_slots_take(&s_slots);
    s->slot_left = slot_to_module(s_slots.cls[0]);
    s->slot_right = slot_to_module(s_slots.cls[1]);
#if EXP_VISION
    {
        dev_cam_status_t cam_st = {0};
        dev_cam_take(&cam_st);
        const dev_cam_gate_t cg = dev_cam_gate(s_slots.cls[0], s_slots.descriptor_valid[0]);
        if (cg.allow && cam_st.phase != DEV_CAM_ERROR) s->slot_left = EXP_MOD_CAMERA;
        if (cam_st.phase == DEV_CAM_RUNNING) {
            dev_face_result_t face_res = {0};
            dev_face_detect_get_result(&face_res);
            if (face_res.total_runs > 0) {
                s->flags |= EXP_SENS_FACE;
                s->face_found = face_res.found;
                s->face_x = (int16_t)face_res.box.x;
                s->face_y = (int16_t)face_res.box.y;
                s->face_w = (int16_t)face_res.box.w;
                s->face_h = (int16_t)face_res.box.h;
                s->face_conf_pct = (uint8_t)(face_res.score * 100.0f + 0.5f);
                s->face_infer_ms = (uint16_t)((face_res.infer_us + 500) / 1000);
            }
        }
    }
#endif
#if EXP_USE_JOYSTICK
    dev_joystick_take(&s_joy);
    if (s_joy.ready) {
        s->flags |= EXP_SENS_JOY;
        s->joy_state = EXP_JOY_READY;
        s->joy[0] = s_joy.x;
        s->joy[1] = s_joy.y;
        s->joy_buttons = s_joy.buttons;
    } else if (s_joy.calibrating) {
        s->joy_state = EXP_JOY_CALIBRATING;
    }
#endif
#else
    s->slot_left = EXP_MOD_NOT_PROBED;    /* nothing reads the slots in this build */
    s->slot_right = EXP_MOD_NOT_PROBED;
#endif
    s->fw_version = FW_VERSION;
}

void app_main(void)
{
    (void)esp_iris_boot_probe();
    (void)esp_iris_start();
#if EXP_DBGLOG
    exp_dbglog_init();
#endif
    DBG_STAGE("start");
    dev_wdt_start();
    ESP_LOGI(TAG, "ESP-Mosaico Experience %s (RF: no radio is initialised by this firmware)", FW_VERSION);
    s_rfp = dev_rf_probe_run();
    if (s_rfp.present) ESP_LOGW(TAG, "RF probe: %d of %d radio entry points exist in the running image (first: %s)", s_rfp.present, s_rfp.checked, s_rfp.first);
    else ESP_LOGI(TAG, "RF probe: none of %d radio entry points exist in the running image (software state, not a measurement)", s_rfp.checked);

    DBG_STAGE("alloc_fb");
    s_flush_done = xSemaphoreCreateBinary();
    for (int i = 0; i < 2; ++i)
        s_fb[i] = heap_caps_aligned_calloc(64, 1, BSP_LCD_H_RES * BSP_LCD_V_RES * sizeof(uint16_t),
                                           MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
    if (!s_flush_done || !s_fb[0] || !s_fb[1]) {
        ESP_LOGE(TAG, "out of memory for the frame buffer");
        return;
    }

    DBG_STAGE("display_init");
    const bsp_display_config_t dcfg = BSP_DISPLAY_DEFAULT_CONFIG();
    if (bsp_display_new(&dcfg, &s_panel) != ESP_OK) {
        ESP_LOGE(TAG, "display init failed");
        return;
    }
    const esp_lcd_panel_io_callbacks_t cbs = {.on_color_trans_done = on_color_done};
    if (esp_lcd_panel_io_register_event_callbacks(bsp_display_get_panel_io(), &cbs, NULL) != ESP_OK)
        ESP_LOGW(TAG, "could not register the transfer-done callback; frames are not paced");

#if EXP_PRESENT_TASK
    DBG_STAGE("present_task");
    s_present_q = xQueueCreate(2, sizeof(const uint16_t *));
    s_buf_free = xSemaphoreCreateCounting(2, 0);
    if (!s_present_q || !s_buf_free ||
        xTaskCreatePinnedToCore(present_task, "present", 4096, NULL, 5, NULL, EXP_PRESENT_CORE) != pdPASS) {
        ESP_LOGW(TAG, "present task not started; sending frames from the drawing thread");
        s_present_q = NULL;                /* present() falls back to the direct call */
    } else {
        ESP_LOGI(TAG, "present task on core %d", EXP_PRESENT_CORE);
    }
#endif
    DBG_STAGE("touch_init");
    esp_lcd_touch_handle_t touch = NULL;
    if (bsp_touch_new(BSP_LCD_ROTATION_DEFAULT, &touch) != ESP_OK) {
        ESP_LOGW(TAG, "touch init failed; the UI will run without touch");
        touch = NULL;
#if EXP_DBGLOG
        s_log_view = true;      /* no touch means no gesture: show the log so the reason can be read */
#endif
    }

#if EXP_USE_SENSORS
    /* Started only now: bsp_touch_new() and bsp_imu_init() both create the shared I2C bus on first use, and that
     * check-then-create is not thread safe. Run concurrently (0.4.0) the loser could fail, e.g. no touch. */
    DBG_STAGE("sensors");
    dev_sensors_start();
#endif

#if EXP_VISION
    DBG_STAGE("camera_task");
    dev_cam_init();               /* creates the task only; the camera is opened when the test view is */
#endif

    DBG_STAGE("app_create");
    exp_surface_attach(s_fb[0], BSP_LCD_H_RES, present, NULL);
    const exp_app_config_t acfg = {.now_us = now_us, .debug_flags = 0};
    exp_app_t *app = exp_app_create(&acfg);
    if (!app) {
        ESP_LOGE(TAG, "exp_app_create failed");
        return;
    }

#if EXP_BOOT_BENCH
    DBG_STAGE("boot_bench");
    run_boot_bench(app);
#endif

    DBG_STAGE("loop");
    ESP_LOGI(TAG, "main loop started");
    bool was_down __attribute__((unused)) = false;      /* a finger is on the panel (read only by the log gesture) */
    bool app_down = false;      /* the app has been told about a press that is not released yet */
#if EXP_DBGLOG
    bool swallow = false;       /* this touch was consumed by the log gesture / log view */
#endif
#if EXP_VISION
    bool cam_swallow = false;   /* this touch was consumed by the camera test view */
#endif
    int last_x = 0, last_y = 0;
    uint32_t last_touch_ms __attribute__((unused)) = (uint32_t)(esp_timer_get_time() / 1000);
#if EXP_DIM_ENABLE && EXP_PRESENT_TASK
    dev_dim_t dim = {false};
#endif
    TickType_t wake = xTaskGetTickCount();
    /* Measured, not assumed: every 60 frames report the real cost of one frame (microseconds). */
    int64_t acc_update = 0, acc_render = 0, acc_present = 0, worst_frame = 0;
    int nframes = 0, overruns = 0;
    int64_t last_loop_us = esp_timer_get_time();
    float time_debt_s = 0.0f;
    /* only read by the optional overlay; kept so the exhibition build (EXP_TIMING_OVERLAY=0) still compiles clean */
    int64_t ov_now __attribute__((unused)) = 0, ov_max __attribute__((unused)) = 0;
    int64_t ov_draw __attribute__((unused)) = 0, ov_present __attribute__((unused)) = 0;
    int64_t last_total __attribute__((unused)) = 0;
    dev_wdt_subscribe();         /* the drawing loop must come round at least every EXP_WDT_TIMEOUT_MS */
    for (;;) {
        dev_wdt_feed();
        const int64_t f0 = esp_timer_get_time();
        dev_mag_dock_status_t dock_st;
        dev_magnet_take_status(&dock_st);
        const bool logo_active = dock_st.logo_active;

        exp_sensors_t s;
        fill_sensors(&s);
        exp_app_set_sensors(app, &s);
        const int64_t f1 = esp_timer_get_time();
#if EXP_DIM_ENABLE && EXP_PRESENT_TASK
        if (s_present_q) {                      /* only the present task may touch the panel IO between frames */
            const int lvl = dev_dim_step(&dim, (uint32_t)(esp_timer_get_time() / 1000), last_touch_ms, EXP_DIM_AFTER_S,
                                         EXP_BRIGHT_LEVEL, EXP_DIM_LEVEL);
            if (lvl >= 0) {
                s_brightness_req = lvl;
                ESP_LOGI(TAG, "brightness -> %d %% (%s)", lvl, dim.dimmed ? "no touch for a long time: burn-in protection" : "touch");
            }
        }
#endif

        if (touch) {
            esp_lcd_touch_point_data_t pt[1];
            uint8_t n = 0;
            const bool have = esp_lcd_touch_read_data(touch) == ESP_OK &&
                              esp_lcd_touch_get_data(touch, pt, &n, 1) == ESP_OK && n > 0;
            if (have) { last_x = pt[0].x; last_y = pt[0].y; last_touch_ms = (uint32_t)(esp_timer_get_time() / 1000); }
            if (logo_active) {
                if (app_down) { exp_app_pointer(app, last_x, last_y, false); app_down = false; }
                was_down = false;
            } else {
#if EXP_VISION
                if (have && !was_down) cam_swallow = cam_view_tap(last_x, last_y);   /* decided once per touch */
                if (!have) cam_swallow = false;
                const bool cam_owns __attribute__((unused)) = s_cam_view || cam_swallow;
#else
                const bool cam_owns __attribute__((unused)) = false;
#endif
#if EXP_DBGLOG
                if (have && !was_down && !cam_owns) swallow = log_view_tap(last_x, last_y);   /* decided once per touch */
                if (!have) swallow = false;
                if (s_log_view || swallow || cam_owns) {
                    /* the log view owns the touch: the app sees nothing (a press it already got is released) */
                    if (app_down) { exp_app_pointer(app, last_x, last_y, false); app_down = false; }
                } else
#endif
                if (have) {
                    exp_app_pointer(app, last_x, last_y, true);
                    app_down = true;
                } else if (app_down) {
                    exp_app_pointer(app, last_x, last_y, false);
                    app_down = false;
                }
                was_down = have;
            }
        }

        const int64_t u0 = esp_timer_get_time();
        /* Advance the app by the time that really passed, in the same 1/30 s steps the tests use. The frame
         * costs more than 33 ms on this board, and a fixed step made every animation play in slow motion. */
        time_debt_s += (float)(f0 - last_loop_us) / 1e6f;
        last_loop_us = f0;
        if (time_debt_s > 0.1f) time_debt_s = 0.1f;               /* never catch up more than 100 ms */
        int steps = (int)(time_debt_s / (TICK_MS / 1000.0f));
        if (steps < 1) steps = 1;
        time_debt_s -= (float)steps * (TICK_MS / 1000.0f);
        if (time_debt_s < 0.0f) time_debt_s = 0.0f;
        if (!logo_active) {
            for (int i = 0; i < steps; ++i) exp_app_update(app, TICK_MS / 1000.0f);
        }
#if EXP_VISION
        cam_sync(!logo_active && (s_cam_view || exp_app_want_camera(app)));
#endif
#if EXP_USE_MAG && EXP_USE_SENSORS
        {
            /* calibration result / rejection is evidence for the compass work: serial log + log view */
            static bool cal_done_seen;
            static unsigned cal_rej_seen;
            const exp_magcal_status_t mcs = exp_app_magcal_status(app);
            const exp_magcal_status_t *mc = &mcs;
            if (mc->done && !cal_done_seen)
                ESP_LOGI(TAG, "mag calibration accepted: offset %+.1f %+.1f %+.1f uT, radius %.1f uT, rms %.3f (rejected fits before: %u)",
                         (double)mc->offset[0], (double)mc->offset[1], (double)mc->offset[2],
                         (double)mc->radius, (double)mc->rms, mc->rejects);
            if (mc->rejects != cal_rej_seen)
                ESP_LOGW(TAG, "mag calibration fit rejected (#%u): radius %.1f uT, rms %.3f - not an Earth-field sphere, collecting again",
                         mc->rejects, (double)mc->radius, (double)mc->rms);
            cal_done_seen = mc->done;
            cal_rej_seen = mc->rejects;
#if EXP_DBGLOG
            /* rotation-consistency sweep: only while flat (gyro z then is the turn about the vertical) */
            if (mc->head_ok && s_diag.acc[2] > 0.97f) dev_sweep_add(&s_sweep, s_diag.gyr_angle[2], mc->head_mag);
            {
                const int filled = dev_sweep_filled(&s_sweep, 3);
                snprintf(s_sweep_l1, sizeof(s_sweep_l1), "sweep %d/12  max dev %.0f deg  (tap the sensor lines = restart)", filled,
                         (double)dev_sweep_max_dev(&s_sweep));
                char *p = s_sweep_l2;
                size_t left = sizeof(s_sweep_l2);
                for (int i = 0; i < DEV_SWEEP_BINS && left > 6; ++i) {
                    const int w = s_sweep.n[i] >= 3 ? snprintf(p, left, "%+.0f ", (double)dev_sweep_mean(&s_sweep, i))
                                                    : snprintf(p, left, ". ");
                    p += w; left -= (size_t)w;
                }
            }
            if (mc->head_ok)
                snprintf(s_head_line, sizeof(s_head_line), "B %+.0f %+.0f %+.0f  mag-head %.0f  fused %.0f", (double)mc->field[0],
                         (double)mc->field[1], (double)mc->field[2], (double)mc->head_mag, (double)mc->yaw_fused);
            else
                snprintf(s_head_line, sizeof(s_head_line), "B -- (not calibrated)  fused %.0f", (double)mc->yaw_fused);
            if (mc->done)
                snprintf(s_cal_line, sizeof(s_cal_line), "cal ok  off %+.0f %+.0f %+.0f  r %.0f  rms %.2f",
                         (double)mc->offset[0], (double)mc->offset[1], (double)mc->offset[2],
                         (double)mc->radius, (double)mc->rms);
            else
                snprintf(s_cal_line, sizeof(s_cal_line), "cal %s  rej %u  r %.0f  rms %.2f",
                         mc->running ? "running" : "idle", mc->rejects, (double)mc->radius,
                         (double)mc->rms);
#endif
        }
#endif
        const int64_t r0 = esp_timer_get_time();
        s_av_sens_us += ((f1 - f0) - s_av_sens_us) / 16;
        s_av_touch_us += ((u0 - f1) - s_av_touch_us) / 16;
        s_av_upd_us += ((r0 - u0) - s_av_upd_us) / 16;
        BeginDrawing();
        if (MosaicoFastFrameAvailable()) {
            if (logo_active) {
                m5stack_logo_draw(s_fb[s_cur], BSP_LCD_H_RES);
                dev_magnet_notify_presented();
            } else {
#if EXP_VISION
                if (s_cam_view) {
                    draw_cam_view();
                } else {
                    exp_app_render(app);
                    draw_cam_pip(app);
                }
#else
                exp_app_render(app);
#endif
#if EXP_TIMING_OVERLAY
                ov_now = last_total;
                draw_timing_overlay(ov_now, ov_max, ov_draw, ov_present);
#endif
#if EXP_DBGLOG
                if (s_log_view) draw_log_view();
#endif
            }
        }
        EndDrawing();              /* hands the finished frame to present() */
        const int64_t r1 = esp_timer_get_time();
        acc_update += r0 - u0;
        acc_present += s_present_us;
        acc_render += (r1 - r0) - s_present_us;
        last_total = r1 - f0;
        if (r1 - f0 > worst_frame) worst_frame = r1 - f0;
        if ((r1 - f0) > TICK_MS * 1000) ++overruns;
        if (++nframes == 60) {
            /* Debug level on purpose: the same numbers are on the panel overlay, and an info line every 2 s would
             * push the boot lines out of the on-screen log. */
            ESP_LOGD(TAG, "timing/60 frames: update %lld us, draw %lld us, present %lld us (avg), worst frame %lld us, "
                          "frames over %d ms: %d, heap internal free %u, psram free %u",
                     (long long)(acc_update / 60), (long long)(acc_render / 60), (long long)(acc_present / 60),
                     (long long)worst_frame, TICK_MS, overruns,
                     (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                     (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
            ov_max = worst_frame;
            ov_draw = acc_render / 60;
            ov_present = acc_present / 60;
            acc_update = acc_render = acc_present = worst_frame = 0;
            nframes = overruns = 0;
        }

        exp_effect_t fx[EXP_EFFECT_QUEUE];
        const size_t nfx = exp_app_pop_effects(app, fx, EXP_EFFECT_QUEUE);
        for (size_t i = 0; i < nfx; ++i) {
            if (fx[i].type == EXP_EFFECT_HAPTIC) dev_haptic_pulse(fx[i].arg);       /* limited again inside (dev_io_core.h) */
            else if (fx[i].type == EXP_EFFECT_SOUND) dev_audio_play((int)fx[i].arg);
        }

        vTaskDelayUntil(&wake, pdMS_TO_TICKS(TICK_MS));
    }
}
