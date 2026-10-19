// SPDX-License-Identifier: Apache-2.0
// Offline voice control on ESP-SR (AFE + WakeNet + MultiNet). See dev_voice.h. No radio, no network, nothing is stored.
//
// Data flow:  mic_task (dev_audio.c) --48 kHz stereo--> dev_voice_feed_stereo(): slot choice + 63-tap low-pass + 3:1
//             decimation --16 kHz mono--> ring (PSRAM) --> feed task --> AFE ("M": one microphone, no echo reference)
//             --> fetch task: WakeNet (two wake words) opens a command window, MultiNet picks the command --> status.
//
// Follows the owner's Full Demo (voice_service.c): MultiNet7 English model, Japanese commands as phonetic English words with
// id + 100, thresholds 0.23 -> 0.26, VAD_MODE_3 (80 ms speech / 300 ms noise), MultiNet timeout 5 s, microphone gain 34 dB
// (set in dev_audio.c through EXP_MIC_GAIN_DB for EXP_VOICE builds). Differences, on purpose: one microphone and no AEC
// (the experience only plays short cues, which are masked instead), tasks on core 1 (the UI renders on core 0).
#include "dev_voice.h"

#include <stdio.h>
#include <string.h>

#include "esp_afe_config.h"
#include "esp_afe_sr_iface.h"
#include "esp_afe_sr_models.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_mn_iface.h"
#include "esp_mn_models.h"
#include "esp_mn_speech_commands.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "model_path.h"

static const char *TAG = "dev_voice";

#define MODEL_PARTITION "model"
/* Wake word choice (EXP_VOICE_WAKE). 2026-10-09 on the board: loading BOTH models crashed the fetch task with
 *   assert failed: model_detect_mfcc wakenet9_quantized.c:1008 (cq->n == model->layers[0]->n)
 * (wn9_hiesp and wn9l_ja_konnichihaesp_tts3 do not share one feature front end in esp-sr 2.5.3), so one model at a time:
 *   0 = "こんにちは ESP" only (default; what the owner's Full Demo uses)      1 = "Hi, ESP" only
 *   2 = both (kept only to reproduce the crash; do not use) */
#ifndef EXP_VOICE_WAKE
#define EXP_VOICE_WAKE 1
#endif
#if EXP_VOICE_WAKE == 1
#define WAKE_NAME_1 "wn9_hiesp"
#define WAKE_NAME_2 NULL
#elif EXP_VOICE_WAKE == 2
#define WAKE_NAME_1 "wn9_hiesp"
#define WAKE_NAME_2 "wn9l_ja_konnichihaesp_tts3"
#else
#define WAKE_NAME_1 "wn9l_ja_konnichihaesp_tts3"
#define WAKE_NAME_2 NULL
#endif
#define RING_SAMPLES 16384                         /* about 1 s of 16 kHz audio (power of two)                       */
#define MIC_WAIT_MS 8000
#define STABLE_MS 15000                            /* crash-guard window after "ready"                                */

static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static dev_vstate_t s_vs;
static const char *s_detail = "";
static char s_detail_buf[64];
static bool s_wake2;
static int32_t s_load_ms = -1;
static uint32_t s_heap_int, s_heap_psram;
static volatile uint32_t s_fetch_loops;

static dev_voice_hooks_t s_hooks;
static dev_vdec_t s_dec;
static dev_vslot_t s_slot;
static dev_vhold_t s_hold;
static dev_vring_t s_ring;
static int16_t *s_ring_buf;
static volatile bool s_feeding;

static srmodel_list_t *s_models;
static const esp_afe_sr_iface_t *s_afe;
static esp_afe_sr_data_t *s_afe_data;
static esp_mn_iface_t *s_mn;
static model_iface_data_t *s_mn_data;
static int16_t *s_frame;
static int s_chunk;

static uint32_t now_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }

static void set_phase(dev_vphase_t p, const char *detail)
{
    portENTER_CRITICAL(&s_lock);
    dev_vs_set_phase(&s_vs, p);
    if (detail) s_detail = detail;
    portEXIT_CRITICAL(&s_lock);
}

static void fail(const char *why)
{
    ESP_LOGE(TAG, "voice control unavailable: %s", why);
    s_feeding = false;
    set_phase(DEV_VPH_ERROR, why);
}

/* ---- Producer side (microphone task) ------------------------------------------------------------------------------------ */
void dev_voice_feed_stereo(const int16_t *st, int frames)
{
    if (!s_feeding || frames <= 0 || frames > DEV_VDEC_FACTOR * 512) return;
    static int16_t tmp[512 + 4];
    const int slot = dev_vslot_choose(&s_slot, st, frames);
    const int n = dev_vdec_push(&s_dec, st, frames, slot, tmp);       /* the filter always runs: its state stays continuous */
    portENTER_CRITICAL(&s_lock);
    const bool masked = dev_vhold_masked(&s_hold, now_ms());
    portEXIT_CRITICAL(&s_lock);
    (void)dev_vring_write(&s_ring, masked ? NULL : tmp, (uint32_t)n);
}

void dev_voice_speaker_active(bool active)
{
    portENTER_CRITICAL(&s_lock);
    dev_vhold_set(&s_hold, active, now_ms());
    portEXIT_CRITICAL(&s_lock);
}

/* ---- Consumer tasks ----------------------------------------------------------------------------------------------------- */
static void feed_task(void *arg)
{
    (void)arg;
    for (;;) {
        if (dev_vring_read(&s_ring, s_frame, (uint32_t)s_chunk) == 0) {
            vTaskDelay(pdMS_TO_TICKS(8));
            continue;
        }
        (void)s_afe->feed(s_afe_data, s_frame);
    }
}

static void fetch_task(void *arg)
{
    (void)arg;
    bool window = false;
    for (;;) {
        afe_fetch_result_t *r = s_afe->fetch_with_delay(s_afe_data, pdMS_TO_TICKS(200));
        const uint32_t t = now_ms();
        portENTER_CRITICAL(&s_lock);
        dev_vs_tick(&s_vs, t);
        if (s_vs.phase != DEV_VPH_LISTENING) window = false;
        portEXIT_CRITICAL(&s_lock);
        if (r == NULL || r->ret_value != ESP_OK) continue;
        ++s_fetch_loops;
        if (r->wakeup_state == WAKENET_DETECTED) {
            const int idx = r->wakenet_model_index > 0 ? r->wakenet_model_index : 1;
            ESP_LOGI(TAG, "[WAKE] wake word %d (%s), level %.1f dBFS", idx, idx == 2 ? "こんにちは ESP" : "Hi ESP", (double)r->data_volume);
            portENTER_CRITICAL(&s_lock);
            dev_vs_wake(&s_vs, t, idx);
            portEXIT_CRITICAL(&s_lock);
            s_mn->clean(s_mn_data);
            window = true;
        }
        if (!window || r->data == NULL) continue;
        const esp_mn_state_t st = s_mn->detect(s_mn_data, r->data);
        if (st == ESP_MN_STATE_DETECTED) {
            esp_mn_results_t *res = s_mn->get_results(s_mn_data);
            if (res != NULL && res->num > 0) {
                ESP_LOGI(TAG, "[HEARD] id=%d prob=%.3f matched='%s' raw='%s'", res->command_id[0], (double)res->prob[0], res->string, res->raw_string);
                portENTER_CRITICAL(&s_lock);
                const bool ok = dev_vs_command(&s_vs, res->command_id[0], res->prob[0]);
                portEXIT_CRITICAL(&s_lock);
                if (!ok) ESP_LOGW(TAG, "command id %d is not one of ours; ignored", res->command_id[0]);
            }
            s_mn->clean(s_mn_data);
            window = false;
        } else if (st == ESP_MN_STATE_TIMEOUT) {
            esp_mn_results_t *res = s_mn->get_results(s_mn_data);
            ESP_LOGW(TAG, "[TIMEOUT] no confident match; raw='%s'", (res && res->raw_string[0]) ? res->raw_string : "-");
            s_mn->clean(s_mn_data);
            portENTER_CRITICAL(&s_lock);
            dev_vs_timeout(&s_vs);
            portEXIT_CRITICAL(&s_lock);
            window = false;
        }
    }
}

/* ---- Loader ------------------------------------------------------------------------------------------------------------- */
static bool register_commands(void)
{
    if (esp_mn_commands_alloc(s_mn, s_mn_data) != ESP_OK) {
        fail("command list could not be allocated");
        return false;
    }
    int added = 0;
    for (int i = 0; i < DEV_VOICE_PHRASE_N; ++i) {
        const dev_vphrase_t *p = &DEV_VOICE_PHRASES[i];
        if (esp_mn_commands_add(dev_voice_phrase_id(p), p->phrase) == ESP_OK) ++added;
        else ESP_LOGW(TAG, "phrase '%s' (id %d) was not accepted", p->phrase, dev_voice_phrase_id(p));
    }
    esp_mn_error_t *err = esp_mn_commands_update();
    const int bad = err ? err->num : 0;
    if (err) {
        for (int i = 0; i < err->num; ++i)
            if (err->phrases && err->phrases[i]) ESP_LOGW(TAG, "MultiNet cannot parse '%s' (id %d)", err->phrases[i]->string, err->phrases[i]->command_id);
    }
    if (added == 0 || bad >= DEV_VOICE_PHRASE_N) {
        fail("no command phrase could be registered");
        return false;
    }
    if (bad > 0 || added < DEV_VOICE_PHRASE_N) {
        snprintf(s_detail_buf, sizeof(s_detail_buf), "%d of %d phrases rejected", bad + (DEV_VOICE_PHRASE_N - added), DEV_VOICE_PHRASE_N);
        set_phase(DEV_VPH_LOADING, s_detail_buf);
    }
    if (s_mn->set_det_threshold) (void)s_mn->set_det_threshold(s_mn_data, DEV_VOICE_MN_THRESH_ACTIVE);
    ESP_LOGI(TAG, "%d command phrases registered (%d rejected)", added - bad, bad + (DEV_VOICE_PHRASE_N - added));
    esp_mn_active_commands_print();
    return true;
}

static bool load_models(void)
{
    s_models = esp_srmodel_init(MODEL_PARTITION);
    if (s_models == NULL) {
        fail("model partition missing or unreadable");
        return false;
    }
    char *wn1 = esp_srmodel_filter(s_models, ESP_WN_PREFIX, WAKE_NAME_1);
    const char *const wake2_name = WAKE_NAME_2;
    char *wn2 = wake2_name ? esp_srmodel_filter(s_models, ESP_WN_PREFIX, wake2_name) : NULL;
    char *mn = esp_srmodel_filter(s_models, ESP_MN_PREFIX, ESP_MN_ENGLISH);
    if (mn == NULL) {
        fail("MultiNet English model not found");
        return false;
    }
    if (wn1 == NULL) {
        fail("wake word model not in the model partition");
        return false;
    }
    s_wake2 = wn2 != NULL;
    ESP_LOGI(TAG, "wake word 1: %s; wake word 2: %s; commands: %s", wn1, wn2 ? wn2 : "(none)", mn);

    afe_config_t *cfg = afe_config_init("M", s_models, AFE_TYPE_SR, AFE_MODE_HIGH_PERF);
    if (cfg == NULL) {
        fail("AFE configuration failed");
        return false;
    }
    cfg->wakenet_init = true;
    cfg->wakenet_model_name = wn1;
    if (wn2) cfg->wakenet_model_name_2 = wn2;
    cfg->aec_init = false;                                     /* no echo reference: cues are masked instead */
    cfg->memory_alloc_mode = AFE_MEMORY_ALLOC_MORE_PSRAM;
    cfg->afe_perferred_core = 1;                               /* the UI renders on core 0 */
    cfg->afe_perferred_priority = 5;
    cfg->vad_init = true;
    cfg->vad_mode = VAD_MODE_3;
    cfg->vad_min_speech_ms = DEV_VOICE_VAD_MIN_SPEECH_MS;
    cfg->vad_min_noise_ms = DEV_VOICE_VAD_MIN_NOISE_MS;
    s_afe = esp_afe_handle_from_config(cfg);
    s_afe_data = s_afe ? s_afe->create_from_config(cfg) : NULL;
    afe_config_free(cfg);
    if (s_afe_data == NULL) {
        fail("AFE could not be created (memory?)");
        return false;
    }
    if (s_afe->get_samp_rate(s_afe_data) != DEV_VOICE_RATE) {
        fail("AFE sample rate is not 16 kHz");
        return false;
    }
    s_chunk = s_afe->get_feed_chunksize(s_afe_data) * s_afe->get_feed_channel_num(s_afe_data);
    if (s_chunk <= 0 || s_chunk > 4096) {
        fail("unexpected AFE chunk size");
        return false;
    }
    s_mn = esp_mn_handle_from_name(mn);
    s_mn_data = s_mn ? s_mn->create(mn, DEV_VOICE_MN_TIMEOUT_MS) : NULL;
    if (s_mn_data == NULL) {
        fail("MultiNet could not be created (memory?)");
        return false;
    }
    if (s_mn->set_det_threshold) (void)s_mn->set_det_threshold(s_mn_data, DEV_VOICE_MN_THRESH_CREATE);
    return register_commands();
}

static void load_task(void *arg)
{
    (void)arg;
    const int64_t t0 = esp_timer_get_time();
    set_phase(DEV_VPH_LOADING, "waiting for the microphone");
    for (int waited = 0; s_hooks.mic_ready && !s_hooks.mic_ready(); waited += 100) {
        if (waited >= MIC_WAIT_MS) {
            fail("microphone is not delivering audio");
            vTaskDelete(NULL);
            return;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    set_phase(DEV_VPH_LOADING, "loading models");
    if (s_hooks.guard) s_hooks.guard(true);                 /* a crash inside the engine blocks this step until power-on */
    bool ok = false;
    s_ring_buf = heap_caps_calloc(RING_SAMPLES, sizeof(int16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s_frame = NULL;
    if (s_ring_buf == NULL) {
        fail("no memory for the audio ring");
    } else {
        dev_vring_init(&s_ring, s_ring_buf, RING_SAMPLES);
        dev_vdec_init(&s_dec);
        memset(&s_slot, 0, sizeof(s_slot));
        if (load_models()) {
            s_frame = heap_caps_calloc((size_t)s_chunk, sizeof(int16_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
            if (s_frame == NULL) fail("no memory for the audio frame");
            else if (xTaskCreatePinnedToCore(feed_task, "voice_feed", 4096, NULL, 6, NULL, 1) != pdPASS) fail("feed task could not be created");
            else if (xTaskCreatePinnedToCore(fetch_task, "voice_fetch", 6144, NULL, 5, NULL, 1) != pdPASS) fail("fetch task could not be created");
            else ok = true;
        }
    }
    if (!ok && s_hooks.guard) s_hooks.guard(false);          /* a graceful failure is not a crash */
    portENTER_CRITICAL(&s_lock);
    s_load_ms = (int32_t)((esp_timer_get_time() - t0) / 1000);
    s_heap_int = (uint32_t)heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    s_heap_psram = (uint32_t)heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    portEXIT_CRITICAL(&s_lock);
    if (ok) {
        s_feeding = true;                                     /* audio flows only from here on */
        set_phase(DEV_VPH_READY, s_detail_buf[0] ? s_detail_buf : "");
        ESP_LOGI(TAG, "voice control ready after %d ms (internal free %u, PSRAM free %u)", (int)s_load_ms, (unsigned)s_heap_int, (unsigned)s_heap_psram);
        if (s_hooks.guard) s_hooks.guard(false);
    }
    vTaskDelete(NULL);
}

void dev_voice_open(const dev_voice_hooks_t *hooks)
{
    if (hooks) s_hooks = *hooks;
    set_phase(DEV_VPH_LOADING, "starting");
    if (xTaskCreatePinnedToCore(load_task, "voice_load", 10240, NULL, 4, NULL, 1) != pdPASS) fail("loader task could not be created");
}

void dev_voice_mark_skipped(void)
{
    ESP_LOGW(TAG, "voice init skipped: it was running when the previous boot ended abnormally (power-cycle to retry)");
    set_phase(DEV_VPH_SKIPPED, "skipped (crash guard)");
}

void dev_voice_take(dev_voice_status_t *out)
{
    portENTER_CRITICAL(&s_lock);
    out->vs = s_vs;
    out->detail = s_detail;
    out->wake2_loaded = s_wake2;
    out->load_ms = s_load_ms;
    out->heap_internal_free = s_heap_int;
    out->heap_psram_free = s_heap_psram;
    portEXIT_CRITICAL(&s_lock);
    out->mic_slot = s_slot.slot;
    out->ring_dropped = s_ring.dropped;
    out->fetch_loops = s_fetch_loops;
}
