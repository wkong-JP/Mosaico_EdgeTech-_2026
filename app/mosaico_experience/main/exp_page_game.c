// SPDX-License-Identifier: Apache-2.0
// Game (C): Tilt Maze menu, play field, countdown and result screens.
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "exp_internal.h"

#define PF_Y 62          /* playfield top */
#define PF_SIZE 400
#define HUD_EXIT_X 412

bool app_game_fullscreen(const exp_app_t *app)
{
    return app->page == EXP_PAGE_GAME && app->maze.phase != MZ_MENU;
}

/* ---- Layout / buttons ---------------------------------------------------------- */
int page_game_layout(const exp_app_t *app, exp_btn_t *out)
{
    int n = 0;
    const exp_maze_t *m = &app->maze;
    if (m->phase == MZ_MENU) {
        for (int i = 0; i < 3; ++i) {
            out[n++] = (exp_btn_t){BTN_DIFF_0 + i, {16 + i * 151, 244, 146, 44}, true, app->sel_diff == i, UI_BTN_SEGMENT};
        }
        out[n++] = (exp_btn_t){BTN_IN_TILT, {16, 314, 78, 44}, app->imu_ok, page_game_input(app) == INPUT_TILT, UI_BTN_CHIP};
        out[n++] = (exp_btn_t){BTN_IN_JOY, {100, 314, 174, 44}, app_joystick_present(app), page_game_input(app) == INPUT_JOY, UI_BTN_CHIP};
        out[n++] = (exp_btn_t){BTN_IN_FACE, {280, 314, 88, 44}, app_camera_present(app), page_game_input(app) == INPUT_FACE, UI_BTN_CHIP};
        out[n++] = (exp_btn_t){BTN_IN_TOUCH, {374, 314, 90, 44}, true, page_game_input(app) == INPUT_TOUCH, UI_BTN_CHIP};
        out[n++] = (exp_btn_t){BTN_START, {16, 364, 448, 44}, true, false, UI_BTN_PRIMARY};
    } else {
        out[n++] = (exp_btn_t){BTN_EXIT_GAME, {HUD_EXIT_X, 6, 48, 44}, true, false, UI_BTN_SECONDARY};
        if ((m->phase == MZ_SUCCESS || m->phase == MZ_TIMEUP) && !m->demo) {
            out[n++] = (exp_btn_t){BTN_RETRY, {76, 306, 160, 44}, true, false, UI_BTN_PRIMARY};
            out[n++] = (exp_btn_t){BTN_TO_MENU, {244, 306, 160, 44}, true, false, UI_BTN_SECONDARY};
        }
    }
    return n;
}

#define GESTURE_TRAIL_LEN 16
static float s_trail_x[GESTURE_TRAIL_LEN];
static float s_trail_y[GESTURE_TRAIL_LEN];
static int s_trail_n = 0;
static float s_filt_x = 0.0f, s_filt_y = 0.0f;

static void game_to_menu(exp_app_t *app)
{
    s_trail_n = 0;
    exp_maze_to_menu(&app->maze);
    exp_maze_preview(&app->maze, (maze_diff_t)app->sel_diff, 0);
    app->result_age_s = 0.0f;
    app->go_banner_s = 0.0f;
}

/* The selected input falls back when its hardware is missing, never silently
 * stays on a dead source: tilt -> touch when the IMU is down, joystick/camera -> tilt/touch. */
game_input_t page_game_input(const exp_app_t *app)
{
    game_input_t m = app->maze.phase == MZ_MENU ? app->input_mode : app->play_input;
    if (m == INPUT_FACE && !app_camera_present(app)) m = app->imu_ok ? INPUT_TILT : INPUT_TOUCH;
    if (m == INPUT_JOY && !app_joystick_present(app)) m = app->imu_ok ? INPUT_TILT : INPUT_TOUCH;
    if (m == INPUT_TILT && !app->imu_ok) m = INPUT_TOUCH;
    return m;
}

void page_game_enter(exp_app_t *app)
{
    app->result_age_s = 0.0f;
    app->go_banner_s = 0.0f;
    app->touch_ax = app->touch_ay = 0.0f;
    if (app->maze.phase == MZ_MENU) exp_maze_preview(&app->maze, (maze_diff_t)app->sel_diff, 0);
}

void page_game_leave(exp_app_t *app)
{
    /* Leaving the game returns every piece of game state to the menu; the maze
     * lives in static storage so nothing needs freeing. */
    if (app->maze.phase != MZ_MENU || app->maze.demo) game_to_menu(app);
    app->touch_ax = app->touch_ay = 0.0f;
    app->go_banner_s = 0.0f;
}

void page_game_button(exp_app_t *app, int id)
{
    switch (id) {
    case BTN_DIFF_0: case BTN_DIFF_1: case BTN_DIFF_2:
        app->sel_diff = id - BTN_DIFF_0;
        exp_maze_preview(&app->maze, (maze_diff_t)app->sel_diff, 0);
        app_sound(app, EXP_SND_TAP);
        break;
    case BTN_IN_TILT: app->input_mode = INPUT_TILT; app_sound(app, EXP_SND_TAP); break;
    case BTN_IN_JOY: app->input_mode = INPUT_JOY; app_sound(app, EXP_SND_TAP); break;
    case BTN_IN_FACE: app->input_mode = INPUT_FACE; app_sound(app, EXP_SND_TAP); break;
    case BTN_IN_TOUCH: app->input_mode = INPUT_TOUCH; app_sound(app, EXP_SND_TAP); break;
    case BTN_START:
        app->play_input = page_game_input(app);
        exp_maze_start(&app->maze, (maze_diff_t)app->sel_diff, 0, false);
        app->result_age_s = 0.0f;
        app_sound(app, EXP_SND_COUNTDOWN);
        break;
    case BTN_RETRY:
        app->play_input = page_game_input(app);
        exp_maze_start(&app->maze, (maze_diff_t)app->sel_diff, 0, false);
        app->result_age_s = 0.0f;
        app_sound(app, EXP_SND_COUNTDOWN);
        break;
    case BTN_TO_MENU:
    case BTN_EXIT_GAME:
        game_to_menu(app);
        app_sound(app, EXP_SND_NAV);
        break;
    default: break;
    }
}

void page_game_pointer(exp_app_t *app, int x, int y, bool down)
{
    if (app->maze.phase != MZ_PLAYING || page_game_input(app) != INPUT_TOUCH || app->maze.demo) return;
    if (!down) { app->touch_ax = app->touch_ay = 0.0f; return; }
    const exp_maze_t *m = &app->maze;
    const float ox = (float)((EXP_W - m->n * m->cell) / 2), oy = (float)(PF_Y + (PF_SIZE - m->n * m->cell) / 2);
    const float dx = (float)x - (ox + m->bx), dy = (float)y - (oy + m->by);
    app->touch_ax = exp_clampf(dx / 120.0f, -0.6f, 0.6f);
    app->touch_ay = exp_clampf(dy / 120.0f, -0.6f, 0.6f);
}

/* ---- Update ----------------------------------------------------------------------- */
void page_game_update(exp_app_t *app, float dt)
{
    exp_maze_t *m = &app->maze;
    if (m->phase == MZ_MENU) return;
    const game_input_t active_in = page_game_input(app);
    float ax = 0.0f, ay = 0.0f;
    if (m->demo) {
        exp_maze_bot(m, &ax, &ay);
    } else if (active_in == INPUT_TILT && app->imu_ok) {
        ax = -app->s.acc[0];
        ay = app->s.acc[1];
        if (fabsf(ax) < 0.03f) ax = 0.0f;
        if (fabsf(ay) < 0.03f) ay = 0.0f;
    } else if (active_in == INPUT_JOY && (app->s.flags & EXP_SENS_JOY)) {
        ax = app->s.joy[0] * 0.6f;
        ay = -app->s.joy[1] * 0.6f;
    } else if (active_in == INPUT_FACE && (app->s.flags & EXP_SENS_FACE) && app->s.face_found) {
        exp_face_box_to_tilt(app->s.face_x, app->s.face_y, app->s.face_w, app->s.face_h, &ax, &ay);

        const int cell = m->cell, n = m->n;
        const int ox = (EXP_W - n * cell) / 2, oy = PF_Y + (PF_SIZE - n * cell) / 2;
        const float fcx = (float)(app->s.face_x + app->s.face_w / 2);
        const float fcy = (float)(app->s.face_y + app->s.face_h / 2);
        const float raw_tx = exp_clampf(fcx, (float)(ox + 10), (float)(ox + n * cell - 10));
        const float raw_ty = exp_clampf(fcy, (float)(oy + 10), (float)(oy + n * cell - 10));

        if (s_trail_n == 0) {
            s_filt_x = raw_tx;
            s_filt_y = raw_ty;
            s_trail_x[0] = s_filt_x;
            s_trail_y[0] = s_filt_y;
            s_trail_n = 1;
        } else {
            /* Smooth exponential convergence across 30fps frames (bridges 4.5fps camera detections) */
            const float alpha = 1.0f - expf(-14.0f * dt);
            s_filt_x += (raw_tx - s_filt_x) * alpha;
            s_filt_y += (raw_ty - s_filt_y) * alpha;

            /* Push trail point only on displacement >= 3px to maintain a dynamic comet trail */
            const float dx = s_filt_x - s_trail_x[0];
            const float dy = s_filt_y - s_trail_y[0];
            if (dx * dx + dy * dy >= 9.0f) {
                for (int i = GESTURE_TRAIL_LEN - 1; i > 0; --i) {
                    s_trail_x[i] = s_trail_x[i - 1];
                    s_trail_y[i] = s_trail_y[i - 1];
                }
                s_trail_x[0] = s_filt_x;
                s_trail_y[0] = s_filt_y;
                if (s_trail_n < GESTURE_TRAIL_LEN) s_trail_n++;
            }
        }
    } else if (active_in == INPUT_TOUCH) {
        ax = app->touch_ax;
        ay = app->touch_ay;
    }
    if (active_in != INPUT_FACE || !(app->s.flags & EXP_SENS_FACE) || !app->s.face_found) {
        s_trail_n = 0;
    }
    /* An active round is never cut short by the 45 s idle return. */
    if (!m->demo) app_activity(app);

    const maze_phase_t before = m->phase;
    const maze_events_t ev = exp_maze_step(m, ax, ay, dt);
    if (ev.countdown_tick) { app_sound(app, EXP_SND_COUNTDOWN); app_haptic(app, 8); }
    if (ev.go) { app_sound(app, EXP_SND_GO); app_haptic(app, 40); app->go_banner_s = 0.7f; }
    if (ev.hit) { app_sound(app, EXP_SND_HIT); app_haptic(app, (uint16_t)(10 + ev.hit_strength * 30)); }
    if (ev.fell) { app_sound(app, EXP_SND_FAIL); app_haptic(app, 70); }
    if (ev.goal) { app_sound(app, EXP_SND_SUCCESS); app_haptic(app, 90); }
    if (ev.timeup) { app_sound(app, EXP_SND_FAIL); app_haptic(app, 60); }
    if (before != m->phase && (m->phase == MZ_SUCCESS || m->phase == MZ_TIMEUP)) app->result_age_s = 0.0f;
    if (app->go_banner_s > 0.0f) app->go_banner_s -= dt;
    if ((m->phase == MZ_SUCCESS || m->phase == MZ_TIMEUP) && !m->demo) {
        app->result_age_s += dt;
        if (app->result_age_s * 1000.0f > (float)EXP_RESULT_AUTO_MS) {
            game_to_menu(app);
        }
    }
}

/* ---- Rendering helpers --------------------------------------------------------- */
static void draw_maze(exp_app_t *app, int ox, int oy, int cell, bool preview)
{
    exp_maze_t *m = &app->maze;
    const int n = m->n;
    const Color wall = preview ? EXP_RGB(86, 126, 184) : EXP_RGB(98, 140, 200);
    const int t = preview ? 2 : 4;
    /* start + goal markers */
    exp_rect(ox + 1, oy + 1, cell - 1, cell - 1, EXP_RGBA(63, 208, 230, 36));
    const float gcx = (float)(ox + (n - 1) * cell) + (float)cell * 0.5f, gcy = (float)(oy + (n - 1) * cell) + (float)cell * 0.5f;
    const float pulse = 0.5f + 0.5f * sinf((float)(app->now_ms % 1600U) / 1600.0f * 6.2831853f);
    exp_glow(gcx, gcy, (float)cell * (0.55f + 0.12f * pulse), C_GOLD, 0.7f);
    exp_ring(gcx, gcy, (float)cell * 0.30f, (float)cell * 0.30f - 2.0f, C_GOLD);
    exp_disc(gcx, gcy, (float)cell * 0.10f, C_GOLD);
    for (int i = 0; i < m->hole_count; ++i) {
        const float hx = (float)(ox + m->hole_x[i] * cell) + (float)cell * 0.5f, hy = (float)(oy + m->hole_y[i] * cell) + (float)cell * 0.5f;
        exp_disc(hx, hy, (float)cell * 0.27f, EXP_RGB(3, 6, 12));
        exp_ring(hx, hy, (float)cell * 0.27f, (float)cell * 0.27f - 2.0f, C_ERR);
    }
    for (int j = 0; j < n; ++j) {
        for (int i = 0; i < n; ++i) {
            const uint8_t w = m->walls[j][i];
            const int x0 = ox + i * cell, y0 = oy + j * cell;
            if (w & MW_N) exp_rect(x0 - t / 2, y0 - t / 2, cell + t, t, wall);
            if (w & MW_W) exp_rect(x0 - t / 2, y0 - t / 2, t, cell + t, wall);
            if (j == n - 1 && (w & MW_S)) exp_rect(x0 - t / 2, y0 + cell - t / 2, cell + t, t, wall);
            if (i == n - 1 && (w & MW_E)) exp_rect(x0 + cell - t / 2, y0 - t / 2, t, cell + t, wall);
        }
    }
}

static void draw_menu(exp_app_t *app)
{
    exp_maze_t *m = &app->maze;
    exp_text(H_MAZE, 16, 62, EXP_FONT_HEAD, C_TEXT, EXP_ALIGN_LEFT);
    const exp_rect_t sub = {16, 108, 268, 48};
    exp_text_box(sub, T_MAZE_SUB, EXP_FONT_CAP, C_TEXT2, EXP_ALIGN_LEFT, 2);
    char buf[40];
    if (m->best[app->sel_diff] > 0) snprintf(buf, sizeof(buf), "%s  %ld", T_BEST, (long)m->best[app->sel_diff]);
    else snprintf(buf, sizeof(buf), "%s  --", T_BEST);
    exp_text(buf, 16, 168, EXP_FONT_BODY, C_GOLD, EXP_ALIGN_LEFT);
    char tm[40];
    snprintf(tm, sizeof(tm), "%s  %d%s", T_TIME, (int)MAZE_TIME_LIMIT_S, T_SEC);
    exp_text(tm, 16, 196, EXP_FONT_CAP, C_TEXT2, EXP_ALIGN_LEFT);

    /* Maze preview panel (the maze that will actually be played). */
    exp_panel(300, 56, 164, 160, 14, C_PANEL, C_BORDER);
    const int pc = 140 / m->n;
    const int ox = 300 + (164 - pc * m->n) / 2, oy = 56 + (160 - pc * m->n) / 2;
    draw_maze(app, ox, oy, pc, true);

    exp_text(T_DIFF, 16, 224, EXP_FONT_CAP, C_TEXT2, EXP_ALIGN_LEFT);
    exp_text(T_INPUT, 16, 292, EXP_FONT_CAP, C_TEXT2, EXP_ALIGN_LEFT);
    if (page_game_input(app) == INPUT_FACE) {
        char nb[64];
        if ((app->s.flags & EXP_SENS_FACE) && app->s.face_found) {
            snprintf(nb, sizeof(nb), "%s: %s %u%%", T_MOD_CAMERA, T_STATUS_OK, (unsigned)app->s.face_conf_pct);
            exp_text(nb, 464, 292, EXP_FONT_CAP, C_OK, EXP_ALIGN_RIGHT);
        } else if (app->s.flags & EXP_SENS_FACE) {
            snprintf(nb, sizeof(nb), "%s: %s", T_MOD_CAMERA, T_NO_MAG);
            exp_text(nb, 464, 292, EXP_FONT_CAP, C_WARN, EXP_ALIGN_RIGHT);
        } else {
            snprintf(nb, sizeof(nb), "%s: %s", T_MOD_CAMERA, T_STATUS_WAIT);
            exp_text(nb, 464, 292, EXP_FONT_CAP, C_WARN, EXP_ALIGN_RIGHT);
        }
    } else if (!app->imu_ok) {
        char nb[64];
        snprintf(nb, sizeof(nb), "%s: %s", T_INPUT_TILT, T_TILT_NA);
        exp_text(nb, 464, 292, EXP_FONT_CAP, C_WARN, EXP_ALIGN_RIGHT);
    } else if (!app_joystick_present(app)) {
        char nb[64];
        snprintf(nb, sizeof(nb), "%s: %s", T_INPUT_JOY, T_JOY_NA);
        exp_text(nb, 464, 292, EXP_FONT_CAP, C_TEXT3, EXP_ALIGN_RIGHT);
    }
    exp_btn_t btns[EXP_MAX_BTN];
    const int n = page_game_layout(app, btns);
    static const char *const names[] = {T_DIFF_EASY, T_DIFF_NORMAL, T_DIFF_HARD, T_INPUT_TILT, T_INPUT_JOY, T_MOD_CAMERA, T_INPUT_TOUCH, T_START};
    for (int i = 0; i < n; ++i) {
        const bool pressed = app->ptr_down && app->press_id == btns[i].id && btns[i].enabled;
        ui_draw_button(&btns[i], names[i], pressed, EXP_ICON_PLAY, btns[i].id == BTN_START);
    }
}

static void fmt_time(char *buf, size_t n, float s)
{
    if (s < 0.0f) s = 0.0f;
    snprintf(buf, n, "%.1f", s);
}

static void draw_play(exp_app_t *app)
{
    exp_maze_t *m = &app->maze;
    const int cell = m->cell, n = m->n;
    const int ox = (EXP_W - n * cell) / 2, oy = PF_Y + (PF_SIZE - n * cell) / 2;

    /* HUD */
    char buf[96];
    exp_text(T_TIME, 28, 6, EXP_FONT_CAP, C_TEXT2, EXP_ALIGN_LEFT);
    fmt_time(buf, sizeof(buf), m->time_left_s);
    const Color tc = m->time_left_s < 10.0f ? C_ERR : C_TEXT;
    int w = exp_text(buf, 28, 22, EXP_FONT_TITLE, tc, EXP_ALIGN_LEFT);
    exp_text(T_SEC, 28 + w + 4, 30, EXP_FONT_CAP, C_TEXT2, EXP_ALIGN_LEFT);
    if (m->demo) {
        int cw = 0;
        ui_chip(EXP_W / 2 - 52, 12, T_DEMO_PLAY, C_GOLD, C_TEXT, &cw);
    } else {
        snprintf(buf, sizeof(buf), "%s %u   %s %u", T_HITS, (unsigned)m->hits, T_FALLS, (unsigned)m->falls);
        exp_text(buf, 276, 22, EXP_FONT_CAP, C_TEXT2, EXP_ALIGN_CENTER);
    }
    exp_btn_t btns[EXP_MAX_BTN];
    const int nb = page_game_layout(app, btns);
    ui_draw_button(&btns[0], NULL, app->ptr_down && app->press_id == BTN_EXIT_GAME, EXP_ICON_CLOSE, false);
    exp_icon(EXP_ICON_CLOSE, (float)(HUD_EXIT_X + 24), 28.0f, 20.0f, C_TEXT);
    exp_layout_keepout((exp_rect_t){HUD_EXIT_X + 14, 18, 20, 20});

    /* Play field */
    exp_panel(ox - 6, oy - 6, n * cell + 12, n * cell + 12, 12, EXP_RGB(8, 16, 30), C_BORDER);
    draw_maze(app, ox, oy, cell, false);
    if (!m->demo && page_game_input(app) == INPUT_FACE && (app->s.flags & EXP_SENS_FACE) && app->s.face_found && s_trail_n > 0) {
        const float tx = s_filt_x;
        const float ty = s_filt_y;

        /* Render glowing motion trail with decaying radius and alpha */
        for (int i = s_trail_n - 1; i > 0; --i) {
            const float t = 1.0f - (float)i / (float)GESTURE_TRAIL_LEN;
            const float r = 2.0f + 5.0f * t;
            const float a = 0.6f * t * t;
            exp_disc(s_trail_x[i], s_trail_y[i], r, exp_with_alpha(C_CYAN, a));
            exp_line(s_trail_x[i], s_trail_y[i], s_trail_x[i - 1], s_trail_y[i - 1], 1.5f * t, exp_with_alpha(C_CYAN, a * 0.7f));
        }

        /* High-performance glowing cursor reticle (neon halo + ring + crosshair) */
        const float pulse = 0.5f + 0.5f * sinf((float)(app->now_ms % 1000U) * 0.006283f);
        exp_disc(tx, ty, 16.0f + 2.0f * pulse, exp_with_alpha(C_CYAN, 0.12f));
        exp_ring(tx, ty, 11.0f + 2.0f * pulse, 9.0f, exp_with_alpha(C_CYAN, 0.85f));
        exp_disc(tx, ty, 3.5f, C_GOLD);
        exp_line(tx - 15.0f, ty, tx - 6.0f, ty, 1.0f, exp_with_alpha(C_CYAN, 0.6f));
        exp_line(tx + 6.0f, ty, tx + 15.0f, ty, 1.0f, exp_with_alpha(C_CYAN, 0.6f));
        exp_line(tx, ty - 15.0f, tx, ty - 6.0f, 1.0f, exp_with_alpha(C_CYAN, 0.6f));
        exp_line(tx, ty + 6.0f, tx, ty + 15.0f, 1.0f, exp_with_alpha(C_CYAN, 0.6f));
    }
    /* Ball */
    const float bx = (float)ox + m->bx, by = (float)oy + m->by;
    exp_disc(bx + 2.0f, by + 3.0f, m->radius + 1.0f, EXP_RGBA(0, 0, 0, 110));
    exp_glow(bx, by, m->radius * 2.4f, C_GOLD, 0.6f);
    exp_disc(bx, by, m->radius, C_GOLD);
    exp_disc(bx - m->radius * 0.3f, by - m->radius * 0.3f, m->radius * 0.35f, EXP_RGBA(255, 255, 255, 160));

    /* Countdown / GO */
    if (m->phase == MZ_COUNTDOWN) {
        const float t = fmodf(m->phase_s, 0.8f) / 0.8f;
        exp_panel(EXP_W / 2 - 110, 176, 220, 150, 18, EXP_RGBA(4, 10, 20, 225), C_BORDER);
        exp_ring(EXP_W / 2, 234, 46, 42, C_BORDER);
        exp_arc(EXP_W / 2, 234, 46, 42, 0.0f, 360.0f * (1.0f - t), C_CYAN, true);
        char d[16];
        snprintf(d, sizeof(d), "%d", m->countdown);
        exp_text(d, EXP_W / 2, 208, EXP_FONT_NUM, C_TEXT, EXP_ALIGN_CENTER);
        if (!m->demo) exp_text(T_HINT_PLAY, EXP_W / 2, 292, EXP_FONT_CAP, C_TEXT2, EXP_ALIGN_CENTER);
    } else if (m->phase == MZ_PLAYING && app->go_banner_s > 0.0f) {
        exp_text(H_GAME_GO, EXP_W / 2, 220, EXP_FONT_HEAD, exp_with_alpha(C_GOLD, exp_clampf(app->go_banner_s / 0.4f, 0.0f, 1.0f)), EXP_ALIGN_CENTER);
    }

    /* Result */
    if (m->phase == MZ_SUCCESS || m->phase == MZ_TIMEUP) {
        const float a = exp_ease_out(m->phase_s / 0.35f);
        exp_rect(0, 0, EXP_W, EXP_H, EXP_RGBA(2, 6, 14, (uint8_t)(150.0f * a)));
        const int py = 118 + (int)((1.0f - a) * 24.0f);
        exp_panel(60, py, 360, 244, 18, C_PANEL, m->phase == MZ_SUCCESS ? C_GOLD : C_BORDER);
        exp_text(m->phase == MZ_SUCCESS ? H_GOAL : H_TIMEUP, EXP_W / 2, py + 16, EXP_FONT_HEAD,
                 m->phase == MZ_SUCCESS ? C_GOLD : C_ERR, EXP_ALIGN_CENTER);
        if (m->phase == MZ_SUCCESS) {
            snprintf(buf, sizeof(buf), "%ld", (long)m->score);
            exp_text(buf, EXP_W / 2, py + 68, EXP_FONT_NUM, C_TEXT, EXP_ALIGN_CENTER);
            snprintf(buf, sizeof(buf), "%s %.1f%s   %s %u%s   %s %u%s", T_TIME, MAZE_TIME_LIMIT_S - m->time_left_s, T_SEC,
                     T_HITS, (unsigned)m->hits, T_TIMES, T_FALLS, (unsigned)m->falls, T_TIMES);
            exp_text_box((exp_rect_t){72, py + 132, 336, 24}, buf, EXP_FONT_CAP, C_TEXT2, EXP_ALIGN_CENTER, 1);
            if (m->new_best) exp_text(T_NEW_BEST, EXP_W / 2, py + 160, EXP_FONT_CAP, C_GOLD, EXP_ALIGN_CENTER);
        } else {
            snprintf(buf, sizeof(buf), "%s %u%s", T_HITS, (unsigned)m->hits, T_TIMES);
            exp_text(buf, EXP_W / 2, py + 86, EXP_FONT_BODY, C_TEXT2, EXP_ALIGN_CENTER);
        }
        if (m->demo) {
            exp_text(T_DEMO_PLAY, EXP_W / 2, py + 196, EXP_FONT_CAP, C_TEXT3, EXP_ALIGN_CENTER);
        } else {
            for (int i = 1; i < nb; ++i) {
                ui_draw_button(&btns[i], btns[i].id == BTN_RETRY ? T_RETRY : T_MENU,
                               app->ptr_down && app->press_id == btns[i].id, EXP_ICON_PLAY, false);
            }
        }
    }
}

void page_game_render(exp_app_t *app)
{
    if (app->maze.phase == MZ_MENU) draw_menu(app);
    else draw_play(app);
}
