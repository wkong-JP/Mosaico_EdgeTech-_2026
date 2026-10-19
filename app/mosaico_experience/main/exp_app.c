// SPDX-License-Identifier: Apache-2.0
// Application core: sensor ingest, derived state, navigation chrome, page
// transitions, exhibition idle/attract loop. Hardware free (see exp_types.h).
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "exp_internal.h"

#define TRANS_S 0.28f
#define ATTRACT_STEPS 4
#define ATTRACT_GAME_MAX_MS 50000U
#define ATTRACT_RESULT_MS 3500U
#define ATTRACT_HOME_MS 6000U

/* ---- Small helpers ------------------------------------------------------------ */
bool app_joystick_present(const exp_app_t *app) { return (app->s.flags & EXP_SENS_JOY) != 0; }
bool app_camera_present(const exp_app_t *app) { return app->s.slot_left == EXP_MOD_CAMERA || (app->s.flags & EXP_SENS_FACE) != 0; }

const char *app_module_name(exp_module_t m)
{
    switch (m) {
    case EXP_MOD_NONE: return T_NONE;
    case EXP_MOD_JOYSTICK: return T_MOD_JOY;
    case EXP_MOD_INTERACT: return T_MOD_INTERACT;
    case EXP_MOD_BUTTON_LED: return T_MOD_BTNLED;
    case EXP_MOD_CAMERA: return T_MOD_CAMERA;
    case EXP_MOD_NOT_PROBED: return T_MOD_NOT_PROBED;
    default: return T_MOD_UNKNOWN;
    }
}

void app_activity(exp_app_t *app) { app->last_activity_ms = app->now_ms; }

void app_effect(exp_app_t *app, exp_effect_type_t type, uint16_t arg)
{
    if (app->attract) return;  /* the show floor stays quiet while nobody is interacting */
    if (app->fx_n >= EXP_EFFECT_QUEUE) return;
    app->fx[app->fx_n++] = (exp_effect_t){type, arg, app->now_ms};
}

void app_haptic(exp_app_t *app, uint16_t ms)
{
    if (ms < 5) ms = 5;
    if (ms > 100) ms = 100;  /* motor thermal / battery protection */
    if (app->last_haptic_ms && app->now_ms - app->last_haptic_ms < 30U) return;
    app->last_haptic_ms = app->now_ms ? app->now_ms : 1U;
    app_effect(app, EXP_EFFECT_HAPTIC, ms);
}

void app_sound(exp_app_t *app, exp_sound_t cue) { app_effect(app, EXP_EFFECT_SOUND, (uint16_t)cue); }

static const char *page_title(exp_page_t p)
{
    switch (p) {
    case EXP_PAGE_HOME: return T_PG_HOME;
    case EXP_PAGE_MOTION: return T_PG_MOTION;
    case EXP_PAGE_AUDIO: return T_PG_AUDIO;
    case EXP_PAGE_GAME: return T_PG_GAME;
    default: return T_PG_DEVICE;
    }
}

static const char *tab_label(int i)
{
    static const char *const l[EXP_PAGE_COUNT] = {T_TAB_HOME, T_TAB_MOTION, T_TAB_AUDIO, T_TAB_GAME, T_TAB_DEVICE};
    return l[i];
}

static const exp_icon_t k_tab_icon[EXP_PAGE_COUNT] = {EXP_ICON_HOME, EXP_ICON_MOTION, EXP_ICON_AUDIO, EXP_ICON_GAME, EXP_ICON_DEVICE};

/* ---- Navigation ------------------------------------------------------------------ */
static void page_enter(exp_app_t *app, exp_page_t p)
{
    app->page_enter_ms = app->now_ms;
    if (p == EXP_PAGE_GAME) page_game_enter(app);
}

void app_goto(exp_app_t *app, exp_page_t page, bool user)
{
    if (page == app->page || page >= EXP_PAGE_COUNT) return;
    if (app->page == EXP_PAGE_GAME) page_game_leave(app);
    app->prev_page = app->page;
    app->trans_dir = page > app->page ? 1 : -1;
    app->page = page;
    app->trans = 0.0f;
    page_enter(app, page);
    if (user) {
        app_activity(app);
        app_sound(app, EXP_SND_NAV);
    }
}

bool exp_app_attract(const exp_app_t *app) { return app->attract; }
exp_page_t exp_app_page(const exp_app_t *app) { return app->page; }
bool exp_app_want_camera(const exp_app_t *app)
{
    if (!app || app->attract || app->page != EXP_PAGE_GAME || !app_camera_present(app)) return false;
    if (app->maze.phase == MZ_MENU) return app->input_mode == INPUT_FACE;
    return !app->maze.demo && app->play_input == INPUT_FACE;
}
const exp_layout_report_t *exp_app_layout(const exp_app_t *app) { return &app->layout; }

/* ---- Lifecycle ---------------------------------------------------------------------- */
exp_app_t *exp_app_create(const exp_app_config_t *config)
{
    exp_app_t *a = (exp_app_t *)calloc(1, sizeof(*a));
    if (!a) return NULL;
    if (config) a->cfg = *config;
    a->debug_flags = a->cfg.debug_flags;
    exp_fusion_init(&a->fusion);
    exp_magcal_init(&a->magcal);
    exp_motion_init(&a->motion);
    exp_audio_init(&a->audio);
    exp_maze_init(&a->maze);
    a->page = a->prev_page = EXP_PAGE_HOME;
    a->trans = 1.0f;
    a->input_mode = INPUT_TILT;
    a->sel_diff = MAZE_EASY;
    a->attract_step = -1;
    a->ripple_age = 9.0f;
    exp_layout_enable((a->debug_flags & EXP_DBG_LAYOUT) != 0);
    exp_maze_preview(&a->maze, MAZE_EASY, 0);
    return a;
}

void exp_app_destroy(exp_app_t *app) { free(app); }

exp_magcal_status_t exp_app_magcal_status(const exp_app_t *app)
{
    exp_magcal_status_t st = {0};
    st.running = app->magcal.running;
    st.done = app->magcal.done;
    for (int k = 0; k < 3; ++k) st.offset[k] = app->magcal.offset[0][k];
    st.radius = app->magcal.fit_radius[0];
    st.rms = app->magcal.fit_rms[0];
    st.rejects = app->magcal.fit_rejects;
    for (int k = 0; k < 3; ++k) st.field[k] = app->mag_cal_v[k];
    st.head_mag = app->mag_head_deg;
    st.head_ok = app->mag_head_ok;
    st.yaw_fused = app->yaw;
    return st;
}

void exp_app_set_sensors(exp_app_t *app, const exp_sensors_t *sensors)
{
    if (!app || !sensors) return;
    app->s = *sensors;
    app->have_sensors = true;
}

void exp_app_pointer(exp_app_t *app, int x, int y, bool down)
{
    if (!app) return;
    uint8_t kind;
    if (down) kind = app->ptr_down ? 1 : 0;
    else if (app->ptr_down) kind = 2;
    else return;
    /* The producer state advances immediately so held/released stay consistent
     * even if several events arrive before the next update. */
    app->ptr_down = down;
    if (app->pq_n >= EXP_PTR_QUEUE) {
        /* Drop a move event if possible; never drop down/up. */
        if (kind == 1) return;
        for (int i = 0; i < app->pq_n; ++i) {
            if (app->pq[i].kind == 1) {
                memmove(&app->pq[i], &app->pq[i + 1], (size_t)(app->pq_n - i - 1) * sizeof(app->pq[0]));
                --app->pq_n;
                break;
            }
        }
        if (app->pq_n >= EXP_PTR_QUEUE) return;
    }
    app->pq[app->pq_n++] = (ptr_event_t){x, y, kind};
}

/* ---- Buttons ------------------------------------------------------------------------ */
static int collect_buttons(const exp_app_t *app, exp_btn_t *out)
{
    int n = 0;
    switch (app->page) {
    case EXP_PAGE_HOME: n = page_home_layout(app, out); break;
    case EXP_PAGE_MOTION: n = page_motion_layout(app, out); break;
    case EXP_PAGE_AUDIO: n = page_audio_layout(app, out); break;
    case EXP_PAGE_GAME: n = page_game_layout(app, out); break;
    default: n = page_device_layout(app, out); break;
    }
    if (!app_game_fullscreen(app) && !app->attract) {
        for (int i = 0; i < EXP_PAGE_COUNT && n < EXP_MAX_BTN; ++i) {
            out[n++] = (exp_btn_t){TAB_BASE + i, {16 + i * 90, EXP_TAB_Y, 88, EXP_TAB_H}, true, i == (int)app->page, UI_BTN_ROW};
        }
    }
    return n;
}

static void dispatch(exp_app_t *app, int id)
{
    if (id >= TAB_BASE && id < TAB_BASE + EXP_PAGE_COUNT) {
        app_goto(app, (exp_page_t)(id - TAB_BASE), true);
        return;
    }
    switch (app->page) {
    case EXP_PAGE_HOME: page_home_button(app, id); break;
    case EXP_PAGE_MOTION: page_motion_button(app, id); break;
    case EXP_PAGE_AUDIO: page_audio_button(app, id); break;
    case EXP_PAGE_GAME: page_game_button(app, id); break;
    default: break;
    }
}

static void handle_pointer(exp_app_t *app, const ptr_event_t *e)
{
    if (e->kind == 0) {
        app_activity(app);
        if (app->attract) {
            /* First touch only wakes the unit: it ends the showcase and returns Home. */
            app->attract = false;
            app->attract_step = -1;
            if (app->page != EXP_PAGE_HOME) app_goto(app, EXP_PAGE_HOME, false);
            app->press_id = 0;
            app->ripple_x = (float)e->x; app->ripple_y = (float)e->y; app->ripple_age = 0.0f;
            return;
        }
        exp_btn_t b[EXP_MAX_BTN];
        const int n = collect_buttons(app, b);
        const int i = ui_hit(b, n, e->x, e->y);
        app->press_id = (i >= 0 && b[i].enabled) ? b[i].id : 0;
        app->ripple_x = (float)e->x; app->ripple_y = (float)e->y; app->ripple_age = 0.0f;
        if (app->press_id) app_sound(app, EXP_SND_TAP);
        if (app->page == EXP_PAGE_GAME) page_game_pointer(app, e->x, e->y, true);
    } else if (e->kind == 1) {
        app_activity(app);
        if (app->press_id) {
            exp_btn_t b[EXP_MAX_BTN];
            const int n = collect_buttons(app, b);
            const int i = ui_hit(b, n, e->x, e->y);
            if (i < 0 || b[i].id != app->press_id) app->press_id = 0;  /* finger slid off: cancel */
        }
        if (app->page == EXP_PAGE_GAME) page_game_pointer(app, e->x, e->y, true);
    } else {
        app_activity(app);
        const int id = app->press_id;
        app->press_id = 0;
        if (app->page == EXP_PAGE_GAME) page_game_pointer(app, e->x, e->y, false);
        if (id) dispatch(app, id);
    }
}

/* ---- Voice control ---------------------------------------------------------------------- */
static bool voice_target(exp_voice_cmd_t c, exp_page_t *out)
{
    switch (c) {
    case EXP_VCMD_HOME: *out = EXP_PAGE_HOME; return true;
    case EXP_VCMD_MOTION: *out = EXP_PAGE_MOTION; return true;
    case EXP_VCMD_AUDIO: *out = EXP_PAGE_AUDIO; return true;
    case EXP_VCMD_GAME: *out = EXP_PAGE_GAME; return true;
    case EXP_VCMD_DEVICE: *out = EXP_PAGE_DEVICE; return true;
    default: return false;
    }
}

/* A voice event wakes the unit exactly like the first touch does: the showcase ends and Home is shown. */
static void voice_wake_unit(exp_app_t *app)
{
    app_activity(app);
    if (app->attract) {
        app->attract = false;
        app->attract_step = -1;
        if (app->page != EXP_PAGE_HOME) app_goto(app, EXP_PAGE_HOME, false);
    }
}

static void voice_update(exp_app_t *app)
{
    const exp_sensors_t *s = &app->s;
    app->voice_listening = app->have_sensors && s->ai == EXP_AI_READY && s->voice_listening;
    if (app->voice_flash_s > 0.0f) app->voice_flash_s -= app->dt;
    if (!app->have_sensors) return;
    if (!app->voice_seq_init) {                 /* events that happened before the app looked are not replayed */
        app->voice_seq_seen = s->voice_seq;
        app->voice_seq_init = true;
        return;
    }
    if (s->voice_seq == app->voice_seq_seen) return;
    app->voice_seq_seen = s->voice_seq;
    if (s->ai != EXP_AI_READY) return;          /* a stale event of an engine that is no longer ready counts for nothing */
    if (s->voice_event == EXP_VEV_WAKE) {
        voice_wake_unit(app);
        app->voice_flash_s = 1.0f;
        app_sound(app, EXP_SND_TAP);
        app_haptic(app, 15);
    } else if (s->voice_event == EXP_VEV_COMMAND) {
        exp_page_t target;
        if (!voice_target(s->voice_cmd, &target)) return;
        voice_wake_unit(app);
        app->voice_last_cmd = s->voice_cmd;
        app->voice_last_prob = s->voice_prob_pct;
        app->voice_flash_s = 2.0f;
        if (app_game_fullscreen(app)) {          /* a round is running: voice must not end it */
            app->voice_last_ignored = true;
            return;
        }
        app->voice_last_ignored = false;
        if (target != app->page) app_goto(app, target, true);
        else app_sound(app, EXP_SND_TAP);
        app_haptic(app, 20);
    }
}

/* ---- Attract / idle ------------------------------------------------------------------- */
static void attract_enter(exp_app_t *app, int step)
{
    static const exp_page_t pages[ATTRACT_STEPS] = {EXP_PAGE_MOTION, EXP_PAGE_AUDIO, EXP_PAGE_GAME, EXP_PAGE_HOME};
    step %= ATTRACT_STEPS;
    app->attract_step = step;
    app->attract_step_ms = app->now_ms ? app->now_ms : 1U;
    app_goto(app, pages[step], false);
    if (step == 0 && app->imu_ok && !app->yaw_absolute) exp_fusion_reset_yaw(&app->fusion);
    if (step == 2) exp_maze_start(&app->maze, MAZE_NORMAL, 0, true);
}

static void update_attract(exp_app_t *app)
{
    if (!app->attract) {
        const uint32_t idle = app->now_ms - app->last_activity_ms;
        if (app->page != EXP_PAGE_HOME) {
            if (idle >= EXP_IDLE_RETURN_MS) {
                app_goto(app, EXP_PAGE_HOME, false);
                app->last_activity_ms = app->now_ms;  /* the showcase starts 30 s after reaching Home */
            }
        } else if (idle >= EXP_ATTRACT_AFTER_MS && app->pq_n == 0 && !app->ptr_down) {
            app->attract = true;
            app->attract_step = -1;
        }
        return;
    }
    if (app->attract_step < 0) { attract_enter(app, 0); return; }
    const uint32_t el = app->now_ms - app->attract_step_ms;
    bool next = false;
    switch (app->attract_step) {
    case 2:
        if (app->maze.demo && (app->maze.phase == MZ_SUCCESS || app->maze.phase == MZ_TIMEUP))
            next = app->maze.phase_s * 1000.0f >= (float)ATTRACT_RESULT_MS;
        next = next || el >= ATTRACT_GAME_MAX_MS;
        break;
    case 3: next = el >= ATTRACT_HOME_MS; break;
    default: next = el >= EXP_ATTRACT_PAGE_MS; break;
    }
    if (next) attract_enter(app, app->attract_step + 1);
}

/* ---- Update -------------------------------------------------------------------------------- */
void exp_app_update(exp_app_t *app, float dt)
{
    if (!app) return;
    if (dt <= 0.0f) dt = 1.0f / EXP_TICK_HZ;
    if (dt > 0.1f) dt = 0.1f;
    app->dt = dt;
    app->now_ms += (uint32_t)(dt * 1000.0f + 0.5f);
    ++app->tick;
    if (app->now_ms == 0) app->now_ms = 1;
    const exp_sensors_t *s = &app->s;

    app->imu_ok = app->have_sensors && (s->flags & EXP_SENS_IMU);
    app->mag_ok = app->have_sensors && (s->flags & EXP_SENS_MAG);
    app->mic_ok = app->have_sensors && (s->flags & EXP_SENS_MIC) && s->pcm_count >= 256;

    /* Orientation. */
    if (app->imu_ok) {
        float mv[3];
        const float *mag = NULL;
        if (app->mag_ok && app->magcal.done) {
            exp_magcal_apply(&app->magcal, s->mag, mv);
            mag = mv;
            /* A magnet, a motor or a steel table next to the board changes the field strength. After the sphere fit the
             * strength should stay at the fit radius; far from it the direction cannot be trusted, so the magnetometer is
             * left out (yaw falls back to the gyro and is shown as relative) until the field is plausible again. */
            const float r = 0.5f * (app->magcal.fit_radius[0] + app->magcal.fit_radius[1]);
            if (r > 5.0f) {
                const float mn = sqrtf(mv[0] * mv[0] + mv[1] * mv[1] + mv[2] * mv[2]);
                if (!app->mag_disturbed && (mn < 0.6f * r || mn > 1.5f * r)) app->mag_disturbed = true;
                else if (app->mag_disturbed && mn > 0.7f * r && mn < 1.4f * r) app->mag_disturbed = false;
            }
            if (app->mag_disturbed) mag = NULL;
        } else {
            app->mag_disturbed = false;
        }
        const bool first = !app->fusion.initialized;
        /* Gyro zero-rate offset: learned only while the board is clearly at rest
         * (low rotation, |acc| ~ 1 g), so the relative yaw drifts much less. */
        const float an = sqrtf(s->acc[0] * s->acc[0] + s->acc[1] * s->acc[1] + s->acc[2] * s->acc[2]);
        const bool at_rest = fabsf(s->gyr[0] - app->gyro_bias[0]) < 2.0f && fabsf(s->gyr[1] - app->gyro_bias[1]) < 2.0f &&
                             fabsf(s->gyr[2] - app->gyro_bias[2]) < 2.0f && fabsf(an - 1.0f) < 0.06f;
        app->still_s = at_rest ? app->still_s + dt : 0.0f;
        if (app->still_s > 0.5f)
            for (int k = 0; k < 3; ++k) app->gyro_bias[k] += 0.03f * (s->gyr[k] - app->gyro_bias[k]);
        float gyr[3] = {s->gyr[0] - app->gyro_bias[0], s->gyr[1] - app->gyro_bias[1], s->gyr[2] - app->gyro_bias[2]};
        exp_fusion_update(&app->fusion, s->acc, gyr, mag, dt);
        if (first) exp_fusion_reset_yaw(&app->fusion);
        exp_fusion_euler(&app->fusion, &app->pitch, &app->roll, &app->yaw);
        app->yaw_absolute = app->fusion.mag_in_use;
        const exp_motion_events_t ev = exp_motion_update(&app->motion, s->acc, app->now_ms);
        if (ev.tilt) app->event_glow[0] = 1.0f;
        if (ev.flip) app->event_glow[1] = 1.0f;
        if (ev.shake) app->event_glow[2] = 1.0f;
        if (app->page == EXP_PAGE_MOTION && (ev.flip || ev.shake)) app_haptic(app, 25);
        /* Moving the board keeps the unit awake, but never ends the attract loop
         * (a vibrating table must not defeat the showcase). */
        const float gm = sqrtf(s->gyr[0] * s->gyr[0] + s->gyr[1] * s->gyr[1] + s->gyr[2] * s->gyr[2]);
        if (!app->attract && gm > 40.0f) {
            app->motion_active_ms = app->now_ms;
            app_activity(app);
        }
    } else {
        app->event_glow[0] = app->event_glow[1] = app->event_glow[2] = 0.0f;
    }
    if (app->mag_ok) {
        float v[3] = {0, 0, 0};
        if (app->magcal.done) exp_magcal_apply(&app->magcal, s->mag, v);   /* offset removed: the field itself */
        else for (int k = 0; k < 3; ++k) v[k] = 0.5f * (s->mag[0][k] + s->mag[1][k]);
        app->mag_field_ut = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
        /* Diagnostics: tilt-compensated heading without the fusion filter, to tell a map/offset problem from a
         * filter problem. up = accelerometer direction; heading = clockwise angle from magnetic north to the top edge. */
        for (int k = 0; k < 3; ++k) app->mag_cal_v[k] = v[k];
        const float an = sqrtf(s->acc[0] * s->acc[0] + s->acc[1] * s->acc[1] + s->acc[2] * s->acc[2]);
        app->mag_head_ok = app->magcal.done && app->imu_ok && an > 0.5f && app->mag_field_ut > 5.0f;
        if (app->mag_head_ok) {
            const float u[3] = {s->acc[0] / an, s->acc[1] / an, s->acc[2] / an};
            const float bu = v[0] * u[0] + v[1] * u[1] + v[2] * u[2];
            float n[3] = {v[0] - bu * u[0], v[1] - bu * u[1], v[2] - bu * u[2]};   /* horizontal field = north */
            const float nl = sqrtf(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
            if (nl > 1.0f) {
                for (int k = 0; k < 3; ++k) n[k] /= nl;
                const float e[3] = {n[1] * u[2] - n[2] * u[1], n[2] * u[0] - n[0] * u[2], n[0] * u[1] - n[1] * u[0]};  /* n x u = east */
                const float yu = u[1];                                   /* top edge (0,1,0) minus its vertical part */
                const float yh[3] = {-yu * u[0], 1.0f - yu * u[1], -yu * u[2]};
                float h = atan2f(yh[0] * e[0] + yh[1] * e[1] + yh[2] * e[2], yh[0] * n[0] + yh[1] * n[1] + yh[2] * n[2]) *
                          57.29578f;
                if (h < 0.0f) h += 360.0f;
                app->mag_head_deg = h;
            } else {
                app->mag_head_ok = false;
            }
        }
    } else {
        app->mag_field_ut = 0.0f;
        app->mag_head_ok = false;
    }

    /* Audio DSP with timing. */
    {
        uint32_t t0 = 0;
        if (app->cfg.now_us) t0 = app->cfg.now_us();
        exp_audio_update(&app->audio, app->mic_ok ? s->pcm : NULL, app->mic_ok ? s->pcm_count : 0);
        if (app->cfg.now_us && app->mic_ok) {
            const float us = (float)(uint32_t)(app->cfg.now_us() - t0);
            app->dsp_us = app->dsp_us > 0.0f ? app->dsp_us * 0.9f + us * 0.1f : us;
        } else if (!app->cfg.now_us && s->dsp_time_us) {
            app->dsp_us = (float)s->dsp_time_us;
        } else if (!app->mic_ok) {
            app->dsp_us = 0.0f;
        }
    }
    if (app->mic_ok && app->audio.vad && !app->attract) app_activity(app);
    if (app->spk_playing_s > 0.0f) app->spk_playing_s -= dt;

    /* Pointer. */
    for (int i = 0; i < app->pq_n; ++i) handle_pointer(app, &app->pq[i]);
    app->pq_n = 0;
    if (app->ripple_age < 1.0f) app->ripple_age += dt;
    voice_update(app);

    /* Pages. */
    page_motion_update(app, dt);
    if (app->page == EXP_PAGE_GAME) page_game_update(app, dt);
    if (app->trans < 1.0f) {
        app->trans += dt / TRANS_S;
        if (app->trans > 1.0f) app->trans = 1.0f;
    }
    update_attract(app);
}

/* ---- Rendering --------------------------------------------------------------------------- */
static void render_page(exp_app_t *app, exp_page_t p)
{
    switch (p) {
    case EXP_PAGE_HOME: page_home_render(app); break;
    case EXP_PAGE_MOTION: page_motion_render(app); break;
    case EXP_PAGE_AUDIO: page_audio_render(app); break;
    case EXP_PAGE_GAME: page_game_render(app); break;
    default: page_device_render(app); break;
    }
}

static void draw_logo_mark(int x, int y, int size)
{
    const int c = size / 2 - 1;
    exp_rrect(x, y, c, c, 3, C_CYAN);
    exp_rrect(x + c + 2, y, c, c, 3, EXP_RGB(40, 140, 170));
    exp_rrect(x, y + c + 2, c, c, 3, EXP_RGB(26, 96, 112));
    exp_rrect(x + c + 2, y + c + 2, c, c, 3, C_GOLD);
}

static void draw_top_bar(exp_app_t *app)
{
    const int corner_inset = 28;
    if (app->page == EXP_PAGE_HOME) {
        draw_logo_mark(corner_inset, 10, 24);
    } else {
        exp_text(page_title(app->page), corner_inset, 7, EXP_FONT_TITLE, C_TEXT, EXP_ALIGN_LEFT);
    }
    int right = EXP_W - corner_inset;
    if (app->s.flags & EXP_SENS_BATT) {
        ui_battery(app, right, 22);
        right -= 96;
    }
    if (app->s.flags & EXP_SENS_SIMULATED) {
        const int tw = exp_text_width(T_SIM, EXP_FONT_CAP);
        const int w = tw + 20;
        exp_panel(right - w, 9, w, 26, 13, EXP_RGB(52, 40, 14), C_WARN);
        exp_text_in((exp_rect_t){right - w, 9, w, 26}, T_SIM, EXP_FONT_CAP, C_WARN, EXP_ALIGN_CENTER);
    }
}

static void draw_tab_bar(exp_app_t *app)
{
    exp_btn_t b[EXP_MAX_BTN];
    const int n = collect_buttons(app, b);
    for (int i = 0; i < n; ++i) {
        if (b[i].id < TAB_BASE) continue;
        const int idx = b[i].id - TAB_BASE;
        const bool sel = b[i].selected;
        const bool pressed = app->ptr_down && app->press_id == b[i].id;
        const exp_rect_t r = b[i].r;
        const int rad = (idx == 0 || idx == EXP_PAGE_COUNT - 1) ? 18 : 14;
        exp_panel(r.x, r.y, r.w, r.h, rad, sel ? EXP_RGB(18, 52, 74) : (pressed ? EXP_RGB(26, 44, 76) : C_PANEL),
                  sel ? C_CYAN_DIM : C_BORDER);
        const Color c = sel ? C_CYAN : C_TEXT2;
        exp_icon(k_tab_icon[idx], (float)(r.x + r.w / 2), (float)(r.y + 21), 24.0f, c);
        exp_layout_keepout((exp_rect_t){r.x + r.w / 2 - 12, r.y + 9, 24, 24});
        exp_text(tab_label(idx), r.x + r.w / 2, r.y + 36, EXP_FONT_CAP, sel ? C_TEXT : C_TEXT2, EXP_ALIGN_CENTER);
        exp_layout_target(r);
    }
}

static void draw_attract_bar(exp_app_t *app)
{
    const exp_rect_t r = {24, 420, 432, 44};
    exp_panel(r.x, r.y, r.w, r.h, 16, EXP_RGB(18, 52, 74), C_CYAN_DIM);
    const float ph = (float)(app->now_ms % 1600U) / 1600.0f;
    const float pulse = 0.5f + 0.5f * sinf(ph * 6.2831853f);
    exp_disc((float)(r.x + 20), (float)(r.y + 22), 5.0f, exp_lerp(C_CYAN_DIM, C_CYAN, pulse));
    exp_text_in((exp_rect_t){r.x + 34, r.y, 160, r.h}, T_MODE_AUTO, EXP_FONT_CAP, C_TEXT2, EXP_ALIGN_LEFT);
    exp_text_in((exp_rect_t){r.x + 200, r.y, 220, r.h}, T_TOUCH_START, EXP_FONT_BODY,
                exp_lerp(C_TEXT2, C_TEXT, pulse), EXP_ALIGN_RIGHT);
    exp_layout_target(r);
}

void exp_app_render(exp_app_t *app)
{
    if (!app) return;
    if (app->debug_flags & EXP_DBG_LAYOUT) exp_layout_begin_frame();
    exp_gfx_set_origin(0, 0);
    exp_gfx_set_alpha(255);
    exp_background(app->now_ms);

    const bool fs = app_game_fullscreen(app);
    if (app->trans < 1.0f && app->prev_page != app->page) {
        const float e = exp_ease_in_out(app->trans);
        const int off = 48;
        exp_scissor(0, fs ? 0 : 46, EXP_W, fs ? EXP_H : 364);
        exp_gfx_set_origin((int)(-app->trans_dir * e * (float)off), 0);
        exp_gfx_set_alpha((uint8_t)(255.0f * (1.0f - e)));
        render_page(app, app->prev_page);
        exp_gfx_set_origin((int)(app->trans_dir * (1.0f - e) * (float)off), 0);
        exp_gfx_set_alpha((uint8_t)(255.0f * e));
        render_page(app, app->page);
        exp_gfx_set_origin(0, 0);
        exp_gfx_set_alpha(255);
        exp_scissor_end();
    } else {
        render_page(app, app->page);
    }

    if (!fs) {
        draw_top_bar(app);
        if (app->attract) draw_attract_bar(app);
        else draw_tab_bar(app);
    }

    /* Touch ripple (not while steering the ball by touch). */
    if (app->ripple_age < 0.5f && !fs) {
        const float t = app->ripple_age / 0.5f;
        exp_ring(app->ripple_x, app->ripple_y, 8.0f + 30.0f * exp_ease_out(t), 6.0f + 30.0f * exp_ease_out(t),
                 exp_with_alpha(C_CYAN, 0.55f * (1.0f - t)));
    }

    /* The top bar is hidden during play: keep the synthetic-data marker visible in the HUD. */
    if (fs && (app->s.flags & EXP_SENS_SIMULATED)) {
        const int tw = exp_text_width(T_SIM, EXP_FONT_CAP);
        const int w = tw + 20;
        exp_panel(EXP_W - 72 - w - 8, 15, w, 26, 13, EXP_RGB(52, 40, 14), C_WARN);
        exp_text_in((exp_rect_t){EXP_W - 72 - w - 8, 15, w, 26}, T_SIM, EXP_FONT_CAP, C_WARN, EXP_ALIGN_CENTER);
    }

    if (app->debug_flags & EXP_DBG_LAYOUT) {
        exp_btn_t b[EXP_MAX_BTN];
        const int n = collect_buttons(app, b);
        for (int i = 0; i < n; ++i)
            if (b[i].id < TAB_BASE && b[i].enabled) exp_layout_target(b[i].r);
        exp_layout_finish(&app->layout);
    }
}

size_t exp_app_pop_effects(exp_app_t *app, exp_effect_t *out, size_t capacity)
{
    if (!app || !out) return 0;
    size_t n = 0;
    while (n < capacity && (int)n < app->fx_n) { out[n] = app->fx[n]; ++n; }
    const int rest = app->fx_n - (int)n;
    if (rest > 0) memmove(app->fx, app->fx + n, (size_t)rest * sizeof(app->fx[0]));
    app->fx_n = rest;
    return n;
}

/* ---- Introspection / test hooks ----------------------------------------------------------------- */
int exp_app_state_json(const exp_app_t *a, char *out, size_t cap)
{
    const exp_maze_t *m = &a->maze;
    char first[sizeof(a->layout.first_problem)];
    memcpy(first, a->layout.first_problem, sizeof(first));
    for (size_t i = 0; i < sizeof(first) && first[i]; ++i)
        if (first[i] == '"' || first[i] == '\\' || (unsigned char)first[i] < 0x20) first[i] = '?';
    return snprintf(out, cap,
        "{\"page\":%d,\"attract\":%s,\"attract_step\":%d,\"imu_ok\":%s,\"mic_ok\":%s,\"mag_ok\":%s,"
        "\"pitch\":%.1f,\"roll\":%.1f,\"yaw\":%.1f,\"yaw_absolute\":%s,\"vad\":%s,\"level_db\":%.1f,"
        "\"maze_phase\":%d,\"maze_diff\":%d,\"score\":%ld,\"hits\":%u,\"falls\":%u,\"time_left\":%.1f,\"demo\":%s,"
        "\"input\":%d,\"simulated\":%s,\"rf\":%d,\"ai\":%d,\"voice_listening\":%s,\"voice_cmd\":%d,\"voice_prob\":%d,\"voice_ignored\":%s,\"dsp_us\":%.0f,"
        "\"face_ok\":%s,\"face_found\":%s,\"face_x\":%d,\"face_y\":%d,\"face_conf\":%d,\"face_ms\":%d,"
        "\"ball_x\":%.1f,\"ball_y\":%.1f,\"maze_n\":%u,\"best\":[%ld,%ld,%ld],\"tilt_count\":%lu,\"flip_count\":%lu,"
        "\"shake_count\":%lu,\"magcal_running\":%s,\"magcal_done\":%s,\"mag_ut\":%.1f,\"mag_head\":%.1f,\"mag_head_ok\":%s,\"mag_disturbed\":%s,\"vad_voice_ratio\":%.2f,\"fx_pending\":%d,"
        "\"layout\":{\"texts\":%d,\"text_overlaps\":%d,\"shape_overlaps\":%d,\"off_screen\":%d,\"truncated\":%d,"
        "\"small_targets\":%d,\"missing_glyphs\":%d,\"first\":\"%s\"}}",
        (int)a->page, a->attract ? "true" : "false", a->attract_step, a->imu_ok ? "true" : "false",
        a->mic_ok ? "true" : "false", a->mag_ok ? "true" : "false", (double)a->pitch, (double)a->roll, (double)a->yaw,
        a->yaw_absolute ? "true" : "false", a->audio.vad ? "true" : "false", (double)a->audio.level_db,
        (int)m->phase, (int)m->diff, (long)m->score, (unsigned)m->hits, (unsigned)m->falls, (double)m->time_left_s,
        m->demo ? "true" : "false", (int)page_game_input(a), (a->s.flags & EXP_SENS_SIMULATED) ? "true" : "false",
        (int)a->s.rf, (int)a->s.ai, a->voice_listening ? "true" : "false", (int)a->voice_last_cmd, (int)a->voice_last_prob,
        a->voice_last_ignored ? "true" : "false", (double)a->dsp_us,
        (a->s.flags & EXP_SENS_FACE) ? "true" : "false", a->s.face_found ? "true" : "false",
        (int)a->s.face_x, (int)a->s.face_y, (int)a->s.face_conf_pct, (int)a->s.face_infer_ms,
        (double)m->bx, (double)m->by, (unsigned)m->n,
        (long)m->best[0], (long)m->best[1], (long)m->best[2], (unsigned long)a->motion.tilt_count,
        (unsigned long)a->motion.flip_count, (unsigned long)a->motion.shake_count, a->magcal.running ? "true" : "false",
        a->magcal.done ? "true" : "false", (double)a->mag_field_ut, (double)a->mag_head_deg, a->mag_head_ok ? "true" : "false", a->mag_disturbed ? "true" : "false", (double)a->audio.voice_ratio, a->fx_n, a->layout.texts, a->layout.text_overlaps,
        a->layout.shape_overlaps, a->layout.off_screen, a->layout.truncated, a->layout.small_targets,
        a->layout.missing_glyphs, first);
}

uint32_t exp_app_state_hash(const exp_app_t *a)
{
    uint32_t h = 2166136261u;
#define MIX(v) do { h ^= (uint32_t)(v); h *= 16777619u; } while (0)
    MIX(a->page); MIX(a->attract); MIX(a->attract_step); MIX(a->maze.phase); MIX(a->maze.hits); MIX(a->maze.falls);
    MIX((int)lroundf(a->maze.bx)); MIX((int)lroundf(a->maze.by)); MIX(a->maze.score);
    MIX((int)lroundf(a->pitch)); MIX((int)lroundf(a->roll)); MIX(a->audio.vad);
    MIX(a->sel_diff); MIX(page_game_input(a));
#undef MIX
    return h;
}

void exp_app_debug_goto(exp_app_t *app, exp_page_t page)
{
    if (!app || page >= EXP_PAGE_COUNT) return;
    if (app->page == EXP_PAGE_GAME && page != EXP_PAGE_GAME) page_game_leave(app);
    app->attract = false;
    app->attract_step = -1;
    app->prev_page = app->page = page;
    app->trans = 1.0f;
    page_enter(app, page);
    app_activity(app);
}

void exp_app_debug_flags(exp_app_t *app, uint32_t flags)
{
    if (!app) return;
    app->debug_flags = flags;
    exp_layout_enable((flags & EXP_DBG_LAYOUT) != 0);
}

void exp_app_debug_game_start(exp_app_t *app, int diff, bool demo)
{
    if (!app) return;
    if (diff < 0 || diff >= MAZE_DIFF_COUNT) diff = 0;
    exp_app_debug_goto(app, EXP_PAGE_GAME);
    app->sel_diff = diff;
    exp_maze_start(&app->maze, (maze_diff_t)diff, 0, demo);
}

int exp_app_debug_maze_phase(const exp_app_t *app) { return app ? (int)app->maze.phase : -1; }
