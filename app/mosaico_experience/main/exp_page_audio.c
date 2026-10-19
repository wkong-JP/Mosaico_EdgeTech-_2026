// SPDX-License-Identifier: Apache-2.0
// AI & Audio (B): microphone waveform/spectrum, level, voice activity, speaker test.
#include <math.h>
#include <stdio.h>
#include "exp_internal.h"

int page_audio_layout(const exp_app_t *app, exp_btn_t *out)
{
    (void)app;
    out[0] = (exp_btn_t){BTN_SPK_TEST, {372, 260, 92, 148}, true, false, UI_BTN_ROW};
    return 1;
}

void page_audio_button(exp_app_t *app, int id)
{
    if (id == BTN_SPK_TEST) {
        app->spk_playing_s = 0.9f;
        app_sound(app, EXP_SND_TEST_TONE);
        app_haptic(app, 15);
    }
}

static void fmt_db(char *buf, size_t n, float db)
{
    const int v = (int)lroundf(db);
    if (v <= -90) snprintf(buf, n, "--");
    else if (v < 0) snprintf(buf, n, "\xE2\x88\x92%d", -v);
    else snprintf(buf, n, "%d", v);
}

void page_audio_render(exp_app_t *app)
{
    const exp_audio_t *a = &app->audio;
    char buf[40];

    /* ---- Waveform ---- */
    ui_tile(16, 56, 448, 92, T_AUD_WAVE);
    {
        const char *st = !a->signal_present ? T_STATUS_NA : (a->vad ? T_VAD_ON : T_VAD_OFF);
        const Color c = a->signal_present && a->vad ? C_OK : C_TEXT2;
        const int tw = exp_text_width(st, EXP_FONT_CAP);
        exp_text(st, 452, 64, EXP_FONT_CAP, c, EXP_ALIGN_RIGHT);
        exp_disc((float)(452 - tw - 10), 72.0f, 4.0f, a->signal_present && a->vad ? C_OK : C_TEXT3);
        exp_layout_keepout((exp_rect_t){452 - tw - 15, 66, 10, 12});
    }
    const int wx = 28, wy = 82, ww = 424, wh = 60;
    if (a->signal_present) exp_rect(wx, wy + wh / 2, ww, 1, EXP_RGBA(120, 170, 255, 40));
    if (a->signal_present) exp_layout_keepout((exp_rect_t){wx, wy + 4, ww, wh - 8});
    if (a->signal_present) {
        float pts[EXP_WAVE_PTS * 2];
        for (int i = 0; i < EXP_WAVE_PTS; ++i) {
            float v = exp_clampf(a->wave[i] * 4.0f, -1.0f, 1.0f);
            pts[i * 2] = (float)wx + (float)i * (float)ww / (float)(EXP_WAVE_PTS - 1);
            pts[i * 2 + 1] = (float)(wy + wh / 2) - v * (float)(wh / 2 - 3);
        }
        exp_polyline(pts, EXP_WAVE_PTS, 1.8f, a->vad ? C_OK : C_CYAN);
    } else {
        const exp_rect_t eb = {wx + 40, wy + 10, ww - 80, 30};
        exp_text_box(eb, T_ERR_MIC, EXP_FONT_BODY, C_ERR, EXP_ALIGN_CENTER, 1);
        const exp_rect_t eb2 = {wx + 40, wy + 38, ww - 80, 22};
        exp_text_box(eb2, T_ERR_MIC2, EXP_FONT_CAP, C_TEXT2, EXP_ALIGN_CENTER, 1);
    }

    /* ---- Spectrum ---- */
    ui_tile(16, 156, 448, 96, T_AUD_SPEC);
    {
        const int x0 = 28, y0 = 184, bh = 44;
        const float pitch = 424.0f / (float)EXP_SPEC_BARS;
        if (a->signal_present) {
            exp_layout_keepout((exp_rect_t){x0, y0, 424, bh});
            for (int i = 0; i < EXP_SPEC_BARS; ++i) {
                if (a->bars[i] > 0.20f) {
                    const int bx = x0 + (int)lroundf((float)i * pitch);
                    const int h = (int)lroundf(a->bars[i] * (float)bh);
                    exp_glow((float)(bx + 6), (float)(y0 + bh - h / 2), (float)h * 0.6f, C_CYAN, a->bars[i] * 0.25f);
                }
            }
        }
        for (int i = 0; i < EXP_SPEC_BARS; ++i) {
            const int h = (int)lroundf(a->bars[i] * (float)bh);
            const int bx = x0 + (int)lroundf((float)i * pitch);
            if (h > 1) {
                exp_gradient_v(bx, y0 + bh - h, 13, h, exp_lerp(C_CYAN, C_GOLD, a->bars[i] * a->bars[i]), C_CYAN_DIM, 3);
            } else {
                exp_rect(bx, y0 + bh - 2, 13, 2, C_CYAN_DIM);
            }
            const int ph = (int)lroundf(a->bar_peak[i] * (float)bh);
            if (ph > 2) exp_rect(bx, y0 + bh - ph - 3, 13, 2, C_GOLD);
        }
        /* Frequency axis (log): 62 Hz, 1 kHz, 8 kHz. */
        const float span = logf(127.0f);
        const struct { const char *t; float bin; exp_align_t al; } ticks[3] = {
            {"62Hz", 1.0f, EXP_ALIGN_LEFT}, {"1kHz", 16.0f, EXP_ALIGN_CENTER}, {"8kHz", 127.0f, EXP_ALIGN_RIGHT}};
        for (int i = 0; i < 3; ++i) {
            const float f = logf(ticks[i].bin) / span;
            int tx = x0 + (int)(f * 424.0f);
            if (ticks[i].al == EXP_ALIGN_LEFT) tx = x0;
            if (ticks[i].al == EXP_ALIGN_RIGHT) tx = x0 + 424;
            exp_text(ticks[i].t, tx, 230, EXP_FONT_CAP, C_TEXT3, ticks[i].al);
        }
    }

    /* ---- Level ---- */
    ui_tile(16, 260, 150, 148, T_AUD_LEVEL);
    if (a->signal_present) {
        fmt_db(buf, sizeof(buf), a->level_db);
        const int w = exp_text(buf, 28, 292, EXP_FONT_TITLE, C_TEXT, EXP_ALIGN_LEFT);
        exp_text("dB", 28 + w + 6, 300, EXP_FONT_CAP, C_TEXT2, EXP_ALIGN_LEFT);
        const float frac = (a->level_db + 80.0f) / 80.0f;
        ui_bar(28, 344, 126, 10, frac, a->level_db > -12.0f ? C_WARN : C_CYAN);
        const int px = 28 + (int)(exp_clampf((a->peak_db + 80.0f) / 80.0f, 0.0f, 1.0f) * 122.0f);
        exp_rect(px, 340, 2, 18, C_GOLD);
        fmt_db(buf, sizeof(buf), a->peak_db);
        char line[64];
        snprintf(line, sizeof(line), "%s %s dB", T_PEAK, buf);
        exp_text(line, 28, 366, EXP_FONT_CAP, C_TEXT2, EXP_ALIGN_LEFT);
    } else {
        const exp_rect_t eb = {28, 292, 126, 66};
        exp_text_box(eb, T_ERR_MIC, EXP_FONT_CAP, C_ERR, EXP_ALIGN_LEFT, 3);
    }

    /* ---- Offline AI status ---- */
    ui_tile(174, 260, 190, 148, T_AI_TITLE);
    {
        const int x = 186, right = 352;
        exp_text(T_VAD, x, 294, EXP_FONT_CAP, C_TEXT2, EXP_ALIGN_LEFT);
        exp_text(a->signal_present ? T_VAD_RUN : T_STATUS_NA, right, 294, EXP_FONT_CAP, a->signal_present ? C_OK : C_TEXT3, EXP_ALIGN_RIGHT);
        exp_text(T_INFER, x, 320, EXP_FONT_CAP, C_TEXT2, EXP_ALIGN_LEFT);
        if (app->dsp_us > 0.0f) snprintf(buf, sizeof(buf), "%.2f ms", app->dsp_us / 1000.0f);
        else snprintf(buf, sizeof(buf), "--");
        exp_text(buf, right, 320, EXP_FONT_CAP, C_TEXT, EXP_ALIGN_RIGHT);
        exp_rect(x, 348, 166, 1, C_BORDER);
        /* Offline voice control: the state is whatever the platform reported (never "ready" before the models loaded). */
        const char *vs = T_VOICE_OFF;
        Color vc = C_TEXT3;
        switch (app->s.ai) {
        case EXP_AI_LOADING: vs = T_VOICE_LOADING; vc = C_WARN; break;
        case EXP_AI_READY: vs = app->voice_listening ? T_VOICE_LISTEN : T_VOICE_READY; vc = app->voice_listening ? C_GOLD : C_OK; break;
        case EXP_AI_ERROR: vs = T_VOICE_ERROR; vc = C_ERR; break;
        default: break;
        }
        exp_text(T_VOICE, x, 358, EXP_FONT_CAP, C_TEXT2, EXP_ALIGN_LEFT);
        exp_text(vs, right, 358, EXP_FONT_CAP, vc, EXP_ALIGN_RIGHT);
        const exp_rect_t vb = {x, 380, right - x, 22};
        if (app->s.ai == EXP_AI_READY && app->voice_listening) {
            exp_text_box(vb, T_VOICE_SPEAK, EXP_FONT_CAP, C_GOLD, EXP_ALIGN_LEFT, 1);
        } else if (app->s.ai == EXP_AI_READY && app->voice_last_cmd != EXP_VCMD_NONE) {
            static const char *const names[EXP_VCMD_COUNT] = {"", T_TAB_HOME, T_TAB_MOTION, T_TAB_AUDIO, T_TAB_GAME, T_TAB_DEVICE};
            char line[48];
            if (app->voice_last_ignored) snprintf(line, sizeof(line), "%s", T_VOICE_IGNORED);
            else snprintf(line, sizeof(line), "%s  %d%%", names[app->voice_last_cmd], (int)app->voice_last_prob);
            exp_text_box(vb, line, EXP_FONT_CAP, app->voice_flash_s > 0.0f ? C_GOLD : C_TEXT, EXP_ALIGN_LEFT, 1);
        } else if (app->s.ai == EXP_AI_READY) {
            exp_text_box(vb, T_VOICE_HINT, EXP_FONT_CAP, C_TEXT2, EXP_ALIGN_LEFT, 1);
        } else if (app->s.ai == EXP_AI_NOT_INTEGRATED) {
            exp_text_box(vb, T_WAKE_PENDING, EXP_FONT_CAP, C_TEXT3, EXP_ALIGN_LEFT, 1);
        }
    }

    /* ---- Speaker test ---- */
    {
        exp_btn_t b[EXP_MAX_BTN];
        page_audio_layout(app, b);
        const bool pressed = app->ptr_down && app->press_id == BTN_SPK_TEST;
        ui_draw_button(&b[0], NULL, pressed, EXP_ICON_SPEAKER, false);
        const bool playing = app->spk_playing_s > 0.0f;
        const float cx = 418.0f, cy = 312.0f;
        if (playing) {
            const float t = 1.0f - app->spk_playing_s / 0.9f;
            exp_ring(cx, cy, 24.0f + 14.0f * t, 22.0f + 14.0f * t, exp_with_alpha(C_GOLD, 1.0f - t));
        }
        exp_disc(cx, cy, 24.0f, playing ? EXP_RGB(70, 54, 24) : EXP_RGB(18, 62, 78));
        exp_ring(cx, cy, 24.0f, 22.5f, playing ? C_GOLD : C_CYAN);
        exp_icon(EXP_ICON_SPEAKER, cx - 2.0f, cy, 24.0f, playing ? C_GOLD : C_CYAN);
        exp_layout_keepout((exp_rect_t){(int)cx - 26, (int)cy - 26, 52, 52});
        const exp_rect_t cb = {378, 352, 80, 44};
        exp_text_box(cb, playing ? T_PLAYING : T_SPK_TEST, EXP_FONT_CAP, C_TEXT, EXP_ALIGN_CENTER, 2);
    }
}
