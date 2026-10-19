// SPDX-License-Identifier: Apache-2.0
// Camera preview task, stage 4a. Pipeline (the one of the official BSP example camera_lcd_preview, plus a byte swap for the
// panel): sensor UYVY frame -> PPA (centre square crop, rotate, mirror, scale to 480x480 RGB565) -> byte swap -> published.
// Every stage is timed with esp_timer and reported; nothing here is assumed. No radio, no network.
#include "dev_camera.h"
#include "dev_face_detect.h"

#include <inttypes.h>
#include <string.h>

#include "dev_camera_core.h"
#include "driver/ppa.h"
#include "esp_cache.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "mosaico_module_camera.h"

static const char *TAG = "dev_cam";

#define CAM_TASK_CORE 1               /* the drawing thread and the panel task are on core 0 */
#define CAM_TASK_PRIO 4
#define CAM_TASK_STACK 8192
#define BUF_ALIGN 128                 /* cache line of the PSRAM; the PPA output buffer must be aligned to it */
#define CAPTURE_FAIL_LIMIT 3
#define VIEW_BYTES ((size_t)DEV_CAM_VIEW_W * DEV_CAM_VIEW_H * sizeof(uint16_t))

static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static dev_cam_status_t s_st;                 /* guarded by s_lock */
static volatile bool s_want;
static volatile bool s_want_unid;
static volatile int s_orient;
static volatile uint32_t s_off_gen;           /* counts "view left" requests: tells the task that an error has been seen and left */
static bool s_stuck;                          /* a camera release failed: the slot cannot be reused before a reset */

static SemaphoreHandle_t s_view_lock;
static uint16_t *s_view[2];                   /* published pictures, 480x480 panel byte order */
static int s_pub = -1;                        /* index of the newest published picture, -1 = none (guarded by s_view_lock) */
static ppa_client_handle_t s_ppa;
static mosaico_camera_handle_t s_cam;

static void st_set(dev_cam_phase_t ph, const char *detail, int err)
{
    portENTER_CRITICAL(&s_lock);
    s_st.phase = ph;
    s_st.detail = detail;
    s_st.last_err = err;
    portEXIT_CRITICAL(&s_lock);
}

static ppa_srm_color_mode_t ppa_in_mode(dev_cam_yuv_t k)
{
    switch (k) {
    case DEV_CAM_YUV_YUYV: return PPA_SRM_COLOR_MODE_YUV422_YUYV;
    case DEV_CAM_YUV_VYUY: return PPA_SRM_COLOR_MODE_YUV422_VYUY;
    case DEV_CAM_YUV_YVYU: return PPA_SRM_COLOR_MODE_YUV422_YVYU;
    case DEV_CAM_YUV_UYVY:
    default: return PPA_SRM_COLOR_MODE_YUV422_UYVY;
    }
}

static ppa_srm_rotation_angle_t ppa_angle(int deg)
{
    switch (deg) {
    case 90: return PPA_SRM_ROTATION_ANGLE_90;
    case 180: return PPA_SRM_ROTATION_ANGLE_180;
    case 270: return PPA_SRM_ROTATION_ANGLE_270;
    default: return PPA_SRM_ROTATION_ANGLE_0;
    }
}

static esp_err_t alloc_once(void)
{
    if (s_view[0] && s_view[1] && s_ppa) return ESP_OK;
    const uint32_t caps = MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA | MALLOC_CAP_8BIT;
    for (int i = 0; i < 2; ++i) {
        if (!s_view[i]) s_view[i] = heap_caps_aligned_calloc(BUF_ALIGN, 1, VIEW_BYTES, caps);
        if (!s_view[i]) return ESP_ERR_NO_MEM;
    }
    if (!s_ppa) {
        const ppa_client_config_t pc = {.oper_type = PPA_OPERATION_SRM, .max_pending_trans_num = 1};
        const esp_err_t e = ppa_register_client(&pc, &s_ppa);
        if (e != ESP_OK) { s_ppa = NULL; return e; }
    }
    return ESP_OK;
}

/* Releases whatever the session holds. Returns false if the BSP could not release the slot (then it must not be reused). */
static bool session_close(bool streaming, bool opened)
{
    if (!s_cam) return true;
    if (streaming) {
        const esp_err_t e = mosaico_camera_stop_stream(s_cam);
        if (e != ESP_OK) ESP_LOGW(TAG, "stop_stream: %s", esp_err_to_name(e));
    }
    if (opened) {
        const esp_err_t e = mosaico_camera_close(s_cam);
        if (e != ESP_OK) ESP_LOGW(TAG, "close: %s", esp_err_to_name(e));
    }
    const esp_err_t e = mosaico_camera_del(s_cam);
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "camera release failed (%s): the left slot stays claimed until the next reset", esp_err_to_name(e));
        return false;
    }
    s_cam = NULL;
    return true;
}

/* One converted frame. Returns ESP_OK and publishes, or the failing step in *what. */
static esp_err_t convert_and_publish(mosaico_camera_handle_t cam, mosaico_camera_frame_t *fr, const dev_cam_crop_t *crop, dev_cam_yuv_t yuv, int *w,
                                     int64_t *ppa_us, int64_t *swap_us, const char **what)
{
    uint16_t *out = s_view[*w];
    const dev_cam_orient_t o = dev_cam_orient(s_orient);
    bool exact = false;
    const float scale = dev_cam_scale_for(DEV_CAM_VIEW_W, crop->size, &exact);

    esp_err_t e = esp_cache_msync((void *)fr->data, fr->size, ESP_CACHE_MSYNC_FLAG_DIR_M2C | ESP_CACHE_MSYNC_FLAG_INVALIDATE);
    if (e != ESP_OK) { *what = "cache sync (camera frame)"; return e; }
    e = esp_cache_msync(out, VIEW_BYTES, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
    if (e != ESP_OK) { *what = "cache sync (PPA output)"; return e; }

    const ppa_srm_oper_config_t op = {
        .in = {
            .buffer = fr->data,
            .pic_w = crop->stride_px,
            .pic_h = fr->height,
            .block_w = crop->size,
            .block_h = crop->size,
            .block_offset_x = crop->off_x,
            .block_offset_y = crop->off_y,
            .srm_cm = ppa_in_mode(yuv),
            .yuv_range = PPA_COLOR_RANGE_LIMIT,
            .yuv_std = PPA_COLOR_CONV_STD_RGB_YUV_BT601,
        },
        .out = {
            .buffer = out,
            .buffer_size = VIEW_BYTES,
            .pic_w = DEV_CAM_VIEW_W,
            .pic_h = DEV_CAM_VIEW_H,
            .srm_cm = PPA_SRM_COLOR_MODE_RGB565,
        },
        .rotation_angle = ppa_angle(o.rot_deg),
        .scale_x = scale,
        .scale_y = scale,
        .mirror_x = o.mirror_x,
        .mirror_y = o.mirror_y,
        .mode = PPA_TRANS_MODE_BLOCKING,
    };
    const int64_t t0 = esp_timer_get_time();
    e = ppa_do_scale_rotate_mirror(s_ppa, &op);
    *ppa_us = esp_timer_get_time() - t0;
    /* Return the sensor buffer to the DVP DMA queue immediately after PPA finishes reading it */
    if (cam && fr && fr->data) {
        (void)mosaico_camera_return_frame(cam, fr);
        fr->data = NULL;
    }
    if (e != ESP_OK) { *what = "PPA scale/rotate"; return e; }

    e = esp_cache_msync(out, VIEW_BYTES, ESP_CACHE_MSYNC_FLAG_DIR_M2C | ESP_CACHE_MSYNC_FLAG_INVALIDATE);
    if (e != ESP_OK) { *what = "cache sync (PPA result)"; return e; }
    const int64_t t1 = esp_timer_get_time();
    dev_cam_swap_pixels((uint32_t *)out, (const uint32_t *)out, (size_t)DEV_CAM_VIEW_W * DEV_CAM_VIEW_H);
    e = esp_cache_msync(out, VIEW_BYTES, ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_UNALIGNED);   /* the other core reads it */
    *swap_us = esp_timer_get_time() - t1;
    if (e != ESP_OK) { *what = "cache sync (published picture)"; return e; }

    if (xSemaphoreTake(s_view_lock, pdMS_TO_TICKS(200)) != pdTRUE) { *what = "view lock"; return ESP_ERR_TIMEOUT; }
    s_pub = *w;
    xSemaphoreGive(s_view_lock);
    dev_face_detect_submit_frame(out);
    *w ^= 1;
    portENTER_CRITICAL(&s_lock);
    s_st.scale_exact = exact;
    portEXIT_CRITICAL(&s_lock);
    return ESP_OK;
}

static void run_session(bool allow_unid)
{
    const int64_t t_req = esp_timer_get_time();
    st_set(DEV_CAM_STARTING, "claiming the left slot", 0);
    portENTER_CRITICAL(&s_lock);
    const dev_cam_status_t keep = s_st;
    memset(&s_st, 0, sizeof(s_st));
    s_st.phase = keep.phase;
    s_st.detail = keep.detail;
    s_st.orient = s_orient;
    portEXIT_CRITICAL(&s_lock);

    esp_err_t e = alloc_once();
    if (e != ESP_OK) { st_set(DEV_CAM_ERROR, "no memory for the preview buffers / PPA client", (int)e); return; }
    if (xSemaphoreTake(s_view_lock, pdMS_TO_TICKS(200)) == pdTRUE) {      /* never show the last picture of an earlier session */
        s_pub = -1;
        xSemaphoreGive(s_view_lock);
    }

    mosaico_camera_config_t cfg = MOSAICO_CAMERA_DEFAULT_CONFIG();
    cfg.allow_unidentified = allow_unid;
    cfg.buffer_count = 2;   /* 2 x 1.84MB (1280x720 YUV422) = 3.68MB, fits in 5.09MB free PSRAM */
    if (allow_unid) ESP_LOGW(TAG, "opening the left slot WITHOUT a valid camera descriptor (bring-up exception)");
    e = mosaico_camera_new(&cfg, &s_cam);
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "mosaico_camera_new: %s", esp_err_to_name(e));
        if (!session_close(false, false)) s_stuck = true;
        st_set(DEV_CAM_ERROR, s_stuck ? "camera_new failed and the slot could not be released: reset needed"
                                      : "camera_new failed (slot claim / hardware / video init)", (int)e);
        return;
    }
    e = mosaico_camera_open(s_cam);
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "mosaico_camera_open: %s", esp_err_to_name(e));
        if (!session_close(false, false)) s_stuck = true;
        st_set(DEV_CAM_ERROR, "camera_open failed (sensor probe / format)", (int)e);
        return;
    }
    e = mosaico_camera_start_stream(s_cam);
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "mosaico_camera_start_stream: %s", esp_err_to_name(e));
        if (!session_close(false, true)) s_stuck = true;
        st_set(DEV_CAM_ERROR, "start_stream failed (no frames from the sensor)", (int)e);
        return;
    }
    mosaico_camera_info_t info = {0};
    (void)mosaico_camera_get_info(s_cam, &info);
    const dev_cam_crop_t crop = dev_cam_square_crop(info.width, info.height, info.bytes_per_line);
    ESP_LOGI(TAG, "stream started: %" PRIu32 "x%" PRIu32 " fourcc 0x%08" PRIx32 " bpl %" PRIu32 " buffers %u fps(reported) %" PRIu32 " crop %" PRIu32,
             info.width, info.height, info.pixel_format, info.bytes_per_line, (unsigned)info.buffer_count, info.frame_rate, crop.size);
    portENTER_CRITICAL(&s_lock);
    s_st.src_w = info.width; s_st.src_h = info.height; s_st.src_bpl = info.bytes_per_line; s_st.src_fmt = info.pixel_format;
    s_st.crop = crop.size;
    s_st.open_ms = (int)((esp_timer_get_time() - t_req) / 1000);
    portEXIT_CRITICAL(&s_lock);

    const dev_cam_yuv_t yuv = dev_cam_yuv422_kind(info.pixel_format);
    if (yuv == DEV_CAM_YUV_NONE || !crop.valid) {
        ESP_LOGE(TAG, "unsupported sensor frame: fourcc 0x%08" PRIx32 " %" PRIu32 "x%" PRIu32 " bpl %" PRIu32, info.pixel_format, info.width,
                 info.height, info.bytes_per_line);
        if (!session_close(true, true)) s_stuck = true;
        st_set(DEV_CAM_ERROR, "sensor format is not a packed YUV 4:2:2 order or the frame geometry is invalid", 0);
        return;
    }

    st_set(DEV_CAM_RUNNING, "streaming", 0);
    dev_cam_avg_t a_wait = {0}, a_ppa = {0}, a_swap = {0}, a_tot = {0};
    dev_cam_fps_t fps;
    dev_cam_fps_reset(&fps);
    uint32_t frames = 0, fails = 0, restarts = 0, consecutive = 0;
    bool restart_tried = false;
    int w = 0;
    const char *end_detail = NULL;
    int end_err = 0;

    while (s_want) {
        const int64_t t_loop = esp_timer_get_time();
        mosaico_camera_frame_t fr = {0};
        e = mosaico_camera_get_frame(s_cam, &fr);
        const int64_t t_got = esp_timer_get_time();
        if (e != ESP_OK) {
            ++fails;
            ++consecutive;
            ESP_LOGW(TAG, "capture failed (%" PRIu32 "/%d): %s", consecutive, CAPTURE_FAIL_LIMIT, esp_err_to_name(e));
            if (consecutive >= CAPTURE_FAIL_LIMIT) {
                if (restart_tried) { end_detail = "no frames after a stream restart"; end_err = (int)e; break; }
                const esp_err_t r = mosaico_camera_restart(s_cam);
                if (r != ESP_OK) { end_detail = "stream restart failed"; end_err = (int)r; break; }
                ESP_LOGI(TAG, "stream restarted");
                restart_tried = true;
                ++restarts;
                consecutive = 0;
            }
            portENTER_CRITICAL(&s_lock);
            s_st.capture_fail = fails;
            s_st.restarts = restarts;
            portEXIT_CRITICAL(&s_lock);
            continue;
        }
        consecutive = 0;
        restart_tried = false;

        int64_t ppa_us = 0, swap_us = 0;
        const char *what = NULL;
        const esp_err_t ce = convert_and_publish(s_cam, &fr, &crop, yuv, &w, &ppa_us, &swap_us, &what);
        if (fr.data) {
            const esp_err_t re = mosaico_camera_return_frame(s_cam, &fr);
            if (re != ESP_OK) { end_detail = "return_frame failed"; end_err = (int)re; break; }
        }
        if (ce != ESP_OK) {
            ESP_LOGE(TAG, "%s failed: %s", what ? what : "convert", esp_err_to_name(ce));
            end_detail = what ? what : "frame conversion failed";
            end_err = (int)ce;
            break;
        }
        ++frames;
        const int64_t now = esp_timer_get_time();
        dev_cam_fps_frame(&fps, now);
        dev_cam_avg_add(&a_wait, t_got - t_loop);
        dev_cam_avg_add(&a_ppa, ppa_us);
        dev_cam_avg_add(&a_swap, swap_us);
        dev_cam_avg_add(&a_tot, now - t_loop);
        if (frames == 1) ESP_LOGI(TAG, "first frame published (open took %d ms)", s_st.open_ms);
        portENTER_CRITICAL(&s_lock);
        s_st.frames = frames;
        s_st.capture_fail = fails;
        s_st.restarts = restarts;
        s_st.fps = fps.fps;
        s_st.wait_us = (int)a_wait.avg_us;
        s_st.ppa_us = (int)a_ppa.avg_us;
        s_st.swap_us = (int)a_swap.avg_us;
        s_st.total_us = (int)a_tot.avg_us;
        s_st.orient = s_orient;
        portEXIT_CRITICAL(&s_lock);
    }

    if (!end_detail) st_set(DEV_CAM_STOPPING, "releasing the camera", 0);
    if (!session_close(true, true)) { s_stuck = true; end_detail = "camera release failed: reset needed"; }
    if (end_detail) st_set(DEV_CAM_ERROR, end_detail, end_err);
    else st_set(DEV_CAM_OFF, "off", 0);
    ESP_LOGI(TAG, "session ended: %" PRIu32 " frames, %" PRIu32 " capture errors, %" PRIu32 " restarts", frames, fails, restarts);
}

static void cam_task(void *arg)
{
    (void)arg;
    uint32_t err_gen = 0;      /* value of s_off_gen when the current error state was entered */
    for (;;) {
        if (s_st.phase == DEV_CAM_ERROR && (!s_want || s_off_gen != err_gen)) st_set(DEV_CAM_OFF, "off", 0);   /* view left: ready for a new try */
        if (!s_want) { vTaskDelay(pdMS_TO_TICKS(50)); continue; }
        if (s_stuck) { st_set(DEV_CAM_ERROR, "camera release failed earlier: reset needed", s_st.last_err); vTaskDelay(pdMS_TO_TICKS(200)); continue; }
        if (s_st.phase == DEV_CAM_ERROR) { vTaskDelay(pdMS_TO_TICKS(100)); continue; }   /* shown until the view is left */
        err_gen = s_off_gen;
        run_session(s_want_unid);
        if (s_want && s_st.phase == DEV_CAM_OFF) vTaskDelay(pdMS_TO_TICKS(50));
    }
}

void dev_cam_init(void)
{
    if (s_view_lock) return;
    s_view_lock = xSemaphoreCreateMutex();
    if (!s_view_lock || xTaskCreatePinnedToCore(cam_task, "cam", CAM_TASK_STACK, NULL, CAM_TASK_PRIO, NULL, CAM_TASK_CORE) != pdPASS) {
        ESP_LOGE(TAG, "camera task could not be created");
        st_set(DEV_CAM_ERROR, "camera task could not be created", ESP_ERR_NO_MEM);
        return;
    }
    st_set(DEV_CAM_OFF, "off", 0);
    dev_face_detect_init();
    ESP_LOGI(TAG, "camera task ready (idle: nothing is started until the camera view is opened)");
}

void dev_cam_request(bool on, bool allow_unidentified)
{
    if (on) {
        s_want_unid = allow_unidentified;
        s_want = true;
    } else {
        s_want = false;
        ++s_off_gen;
    }
}

void dev_cam_set_orient(int preset) { s_orient = ((preset % DEV_CAM_ORIENT_N) + DEV_CAM_ORIENT_N) % DEV_CAM_ORIENT_N; }

void dev_cam_take(dev_cam_status_t *out)
{
    portENTER_CRITICAL(&s_lock);
    *out = s_st;
    portEXIT_CRITICAL(&s_lock);
    out->psram_free = (uint32_t)heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
}

bool dev_cam_copy_view(uint16_t *dst)
{
    if (!s_view_lock || !dst) return false;
    if (xSemaphoreTake(s_view_lock, pdMS_TO_TICKS(50)) != pdTRUE) return false;
    const int p = s_pub;
    bool ok = false;
    if (p >= 0 && s_view[p]) {
        /* the camera task filled this buffer on the other core: drop any stale lines before reading */
        if (esp_cache_msync(s_view[p], VIEW_BYTES, ESP_CACHE_MSYNC_FLAG_DIR_M2C | ESP_CACHE_MSYNC_FLAG_INVALIDATE) == ESP_OK) {
            memcpy(dst, s_view[p], VIEW_BYTES);
            ok = true;
        }
    }
    xSemaphoreGive(s_view_lock);
    return ok;
}

bool dev_cam_copy_pip(uint16_t *fb480, int dst_x, int dst_y, int dst_w, int dst_h)
{
    if (!s_view_lock || !fb480 || dst_w <= 0 || dst_h <= 0) return false;
    if (dst_x < 0 || dst_y < 0 || dst_x + dst_w > DEV_CAM_VIEW_W || dst_y + dst_h > DEV_CAM_VIEW_H) return false;
    if (xSemaphoreTake(s_view_lock, pdMS_TO_TICKS(15)) != pdTRUE) return false;
    const int p = s_pub;
    bool ok = false;
    if (p >= 0 && s_view[p]) {
        if (esp_cache_msync(s_view[p], VIEW_BYTES, ESP_CACHE_MSYNC_FLAG_DIR_M2C | ESP_CACHE_MSYNC_FLAG_INVALIDATE) == ESP_OK) {
            const uint16_t *src = s_view[p];
            for (int y = 0; y < dst_h; ++y) {
                const int sy = (y * DEV_CAM_VIEW_H) / dst_h;
                const uint16_t *srow = src + sy * DEV_CAM_VIEW_W;
                uint16_t *drow = fb480 + (dst_y + y) * DEV_CAM_VIEW_W + dst_x;
                for (int x = 0; x < dst_w; ++x) {
                    const int sx = (x * DEV_CAM_VIEW_W) / dst_w;
                    drow[x] = srow[sx];
                }
            }
            ok = true;
        }
    }
    xSemaphoreGive(s_view_lock);
    return ok;
}

