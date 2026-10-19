// SPDX-License-Identifier: Apache-2.0
// Device: board identity, firmware, memory, expansion slots and RF safe-mode status.
// Every number is read from the platform snapshot; unknown values show "--".
#include <stdio.h>
#include <string.h>
#include "exp_internal.h"

int page_device_layout(const exp_app_t *app, exp_btn_t *out)
{
    (void)app;
    (void)out;
    return 0;
}

static void kv_row(int y, const char *key, const char *val, Color vc)
{
    exp_text(key, 28, y, EXP_FONT_CAP, C_TEXT2, EXP_ALIGN_LEFT);
    exp_text(val, 452, y, EXP_FONT_CAP, vc, EXP_ALIGN_RIGHT);
}

static void mem_row(int y, const char *label, uint32_t freeb, uint32_t total, Color fill)
{
    char buf[48];
    exp_text(label, 28, y, EXP_FONT_CAP, C_TEXT2, EXP_ALIGN_LEFT);
    if (total == 0) {
        exp_text("--", 452, y, EXP_FONT_CAP, C_TEXT3, EXP_ALIGN_RIGHT);
        ui_bar(28, y + 24, 424, 8, 0.0f, fill);
        return;
    }
    const uint32_t used = total > freeb ? total - freeb : 0;
    if (total >= 1024U * 1024U)
        snprintf(buf, sizeof(buf), "%.1f / %.1f MB", (double)used / 1048576.0, (double)total / 1048576.0);
    else
        snprintf(buf, sizeof(buf), "%u / %u KB", (unsigned)(used / 1024U), (unsigned)(total / 1024U));
    exp_text(buf, 452, y, EXP_FONT_CAP, C_TEXT, EXP_ALIGN_RIGHT);
    ui_bar(28, y + 24, 424, 8, (float)used / (float)total, fill);
}

void page_device_render(exp_app_t *app)
{
    const exp_sensors_t *s = &app->s;
    char buf[64];

    /* Identity panel. */
    exp_panel(16, 56, 448, 125, 14, C_PANEL, C_BORDER);
    kv_row(64, T_DEV_CHIP, T_DEV_CHIP_VAL, C_TEXT);
    kv_row(87, T_DEV_BOARD, T_DEV_BOARD_VAL, C_TEXT);
    kv_row(110, T_DEV_FW, s->fw_version ? s->fw_version : "--", C_TEXT);
    snprintf(buf, sizeof(buf), "%02u:%02u:%02u", (unsigned)(s->uptime_s / 3600U), (unsigned)((s->uptime_s / 60U) % 60U),
             (unsigned)(s->uptime_s % 60U));
    kv_row(133, T_DEV_UPTIME, buf, C_TEXT);
    const bool sim = (s->flags & EXP_SENS_SIMULATED) != 0;
    kv_row(156, T_DEV_DATA, sim ? T_DEV_SRC_SIM : T_DEV_SRC_HW, sim ? C_WARN : C_OK);

    /* Memory panel. */
    exp_panel(16, 189, 448, 100, 14, C_PANEL, C_BORDER);
    mem_row(196, T_DEV_INT, s->heap_internal_free, s->heap_internal_total, C_CYAN);
    mem_row(234, T_DEV_PSRAM, s->heap_psram_free, s->heap_psram_total, C_GOLD);
    if (s->heap_largest_free) snprintf(buf, sizeof(buf), "%u KB · 16MB Flash", (unsigned)(s->heap_largest_free / 1024U));
    else snprintf(buf, sizeof(buf), "-- · 16MB Flash");
    exp_text(T_DEV_LARGEST, 28, 268, EXP_FONT_CAP, C_TEXT3, EXP_ALIGN_LEFT);
    exp_text(buf, 452, 268, EXP_FONT_CAP, C_TEXT3, EXP_ALIGN_RIGHT);

    /* Expansion slots. */
    exp_panel(16, 297, 448, 56, 14, C_PANEL, C_BORDER);
    const exp_module_t mods[2] = {s->slot_left, s->slot_right};
    const char *const slot_names[2] = {T_SLOT_L, T_SLOT_R};
    for (int i = 0; i < 2; ++i) {
        const int x = 28 + i * 220;
        exp_text(slot_names[i], x, 301, EXP_FONT_CAP, C_TEXT2, EXP_ALIGN_LEFT);
        const char *name = app_module_name(mods[i]);
        const Color col = (mods[i] == EXP_MOD_NONE || mods[i] == EXP_MOD_NOT_PROBED) ? C_TEXT3 : C_TEXT;
        if (mods[i] == EXP_MOD_JOYSTICK && s->joy_state == EXP_JOY_CALIBRATING) name = T_MOD_JOY_CAL;
        if (i == 0 && (app->debug_flags & EXP_DBG_LONG_TEXT)) name = T_LONG_MODULE;
        exp_text_box((exp_rect_t){x, 323, 200, 24}, name, EXP_FONT_CAP, col, EXP_ALIGN_LEFT, 1);
        if (mods[i] != EXP_MOD_NONE && mods[i] != EXP_MOD_NOT_PROBED) exp_disc((float)(x + 196), 310.0f, 4.0f, C_OK);
    }

    /* RF safe-mode status. Software state only: never a compliance statement. */
    exp_panel(16, 361, 448, 47, 14, C_PANEL, C_BORDER);
    const char *rf_text;
    Color rf_col;
    switch (s->rf) {
    case EXP_RF_SOFT_OFF_OK: rf_text = T_RF_OK; rf_col = C_OK; break;
    case EXP_RF_SOFT_CHECK_FAIL: rf_text = T_RF_FAIL; rf_col = C_ERR; break;
    default: rf_text = T_RF_UNKNOWN; rf_col = C_WARN; break;
    }
    exp_text(T_DEV_RF, 28, 365, EXP_FONT_CAP, C_TEXT2, EXP_ALIGN_LEFT);
    const int tw = exp_text_width(rf_text, EXP_FONT_CAP);
    exp_text(rf_text, 452, 365, EXP_FONT_CAP, rf_col, EXP_ALIGN_RIGHT);
    exp_disc((float)(452 - tw - 12), 374.0f, 4.0f, rf_col);
    exp_text(T_RF_NOTE, 28, 385, EXP_FONT_CAP, C_TEXT3, EXP_ALIGN_LEFT);
}
