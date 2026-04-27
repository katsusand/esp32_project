#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "driver/ledc.h"
#include "esp_check.h"
#include "esp_log.h"
#include "app_stack_monitor.h"
#include "cyd_speaker.h"

#define CYD_SPEAKER_QUEUE_LEN 8
#define CYD_SPEAKER_TASK_STACK 3072
#define CYD_SPEAKER_TASK_PRIO 3
#define CYD_SPEAKER_DEFAULT_FREQ_HZ 1000
#define CYD_SPEAKER_DUTY_RES LEDC_TIMER_10_BIT
#define CYD_SPEAKER_DUTY_MAX ((1U << 10) - 1U)
#define CYD_SPEAKER_MIN_FREQ_HZ 20
#define CYD_SPEAKER_MAX_FREQ_HZ 20000
#define CYD_SPEAKER_MAX_DURATION_MS 5000

static const char *TAG = "cyd_speaker";

typedef enum {
    CYD_SPEAKER_CMD_PLAY = 0,
    CYD_SPEAKER_CMD_STOP,
} cyd_speaker_cmd_id_t;

typedef struct {
    cyd_speaker_cmd_id_t id;
    cyd_speaker_note_t notes[CYD_SPEAKER_MAX_NOTES];
    size_t note_count;
} cyd_speaker_cmd_t;

static QueueHandle_t s_speaker_queue;
static bool s_speaker_started;

static ledc_mode_t cyd_speaker_speed_mode(void)
{
    return LEDC_LOW_SPEED_MODE;
}

static ledc_timer_t cyd_speaker_timer(void)
{
    return (ledc_timer_t)CONFIG_CYD_SPEAKER_LEDC_TIMER;
}

static ledc_channel_t cyd_speaker_channel(void)
{
    return (ledc_channel_t)CONFIG_CYD_SPEAKER_LEDC_CHANNEL;
}

static uint32_t cyd_speaker_duty(void)
{
    return ((CYD_SPEAKER_DUTY_MAX / 2U) * CONFIG_CYD_SPEAKER_VOLUME_PERCENT) / 100U;
}

static esp_err_t cyd_speaker_apply_silence(void)
{
#if CONFIG_CYD_SPEAKER_ENABLED
    ESP_RETURN_ON_ERROR(ledc_set_duty(cyd_speaker_speed_mode(), cyd_speaker_channel(), 0), TAG, "set duty failed");
    return ledc_update_duty(cyd_speaker_speed_mode(), cyd_speaker_channel());
#else
    return ESP_OK;
#endif
}

static esp_err_t cyd_speaker_apply_tone(uint32_t frequency_hz)
{
#if CONFIG_CYD_SPEAKER_ENABLED
    ESP_RETURN_ON_ERROR(
        ledc_set_freq(cyd_speaker_speed_mode(), cyd_speaker_timer(), frequency_hz),
        TAG,
        "set freq failed");
    ESP_RETURN_ON_ERROR(
        ledc_set_duty(cyd_speaker_speed_mode(), cyd_speaker_channel(), cyd_speaker_duty()),
        TAG,
        "set duty failed");
    return ledc_update_duty(cyd_speaker_speed_mode(), cyd_speaker_channel());
#else
    (void)frequency_hz;
    return ESP_OK;
#endif
}

static bool cyd_speaker_delay_or_stop(uint32_t delay_ms)
{
    cyd_speaker_cmd_t cmd;
    TickType_t remaining_ticks = pdMS_TO_TICKS(delay_ms);

    while (remaining_ticks > 0) {
        TickType_t step_ticks = remaining_ticks > pdMS_TO_TICKS(20) ? pdMS_TO_TICKS(20) : remaining_ticks;

        if (xQueueReceive(s_speaker_queue, &cmd, step_ticks) == pdTRUE) {
            if (cmd.id == CYD_SPEAKER_CMD_STOP) {
                ESP_ERROR_CHECK(cyd_speaker_apply_silence());
                return true;
            }

            xQueueSendToFront(s_speaker_queue, &cmd, 0);
            return false;
        }

        remaining_ticks -= step_ticks;
    }

    return false;
}

static void cyd_speaker_play_notes(const cyd_speaker_cmd_t *cmd)
{
    for (size_t i = 0; i < cmd->note_count; ++i) {
        const cyd_speaker_note_t *note = &cmd->notes[i];

        if (note->frequency_hz > 0 && note->duration_ms > 0) {
            ESP_ERROR_CHECK(cyd_speaker_apply_tone(note->frequency_hz));
            if (cyd_speaker_delay_or_stop(note->duration_ms)) {
                return;
            }
        }

        ESP_ERROR_CHECK(cyd_speaker_apply_silence());

        if (note->gap_ms > 0 && cyd_speaker_delay_or_stop(note->gap_ms)) {
            return;
        }
    }

    ESP_ERROR_CHECK(cyd_speaker_apply_silence());
}

static void cyd_speaker_task(void *arg)
{
    cyd_speaker_cmd_t cmd;

    (void)arg;

    while (true) {
        APP_STACK_MONITOR_CHECK(TAG, "cyd_speaker", 30000);

        if (xQueueReceive(s_speaker_queue, &cmd, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        switch (cmd.id) {
            case CYD_SPEAKER_CMD_PLAY:
                cyd_speaker_play_notes(&cmd);
                break;
            case CYD_SPEAKER_CMD_STOP:
                ESP_ERROR_CHECK(cyd_speaker_apply_silence());
                break;
            default:
                break;
        }
        APP_STACK_MONITOR_CHECK(TAG, "cyd_speaker", 30000);
    }
}

static esp_err_t cyd_speaker_validate_note(const cyd_speaker_note_t *note)
{
    ESP_RETURN_ON_FALSE(note != NULL, ESP_ERR_INVALID_ARG, TAG, "note is null");
    ESP_RETURN_ON_FALSE(
        note->frequency_hz == 0 ||
            (note->frequency_hz >= CYD_SPEAKER_MIN_FREQ_HZ && note->frequency_hz <= CYD_SPEAKER_MAX_FREQ_HZ),
        ESP_ERR_INVALID_ARG,
        TAG,
        "frequency out of range");
    ESP_RETURN_ON_FALSE(note->duration_ms <= CYD_SPEAKER_MAX_DURATION_MS, ESP_ERR_INVALID_ARG, TAG, "duration too long");
    ESP_RETURN_ON_FALSE(note->gap_ms <= CYD_SPEAKER_MAX_DURATION_MS, ESP_ERR_INVALID_ARG, TAG, "gap too long");
    return ESP_OK;
}

static esp_err_t cyd_speaker_send(const cyd_speaker_cmd_t *cmd)
{
    ESP_RETURN_ON_FALSE(s_speaker_queue != NULL, ESP_ERR_INVALID_STATE, TAG, "speaker task not initialized");
    return xQueueSend(s_speaker_queue, cmd, pdMS_TO_TICKS(100)) == pdTRUE ? ESP_OK : ESP_ERR_TIMEOUT;
}

esp_err_t cyd_speaker_init(void)
{
#if CONFIG_CYD_SPEAKER_ENABLED
    ledc_timer_config_t timer_conf = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = CYD_SPEAKER_DUTY_RES,
        .timer_num = (ledc_timer_t)CONFIG_CYD_SPEAKER_LEDC_TIMER,
        .freq_hz = CYD_SPEAKER_DEFAULT_FREQ_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ledc_channel_config_t channel_conf = {
        .gpio_num = CONFIG_CYD_SPEAKER_GPIO,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = (ledc_channel_t)CONFIG_CYD_SPEAKER_LEDC_CHANNEL,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = (ledc_timer_t)CONFIG_CYD_SPEAKER_LEDC_TIMER,
        .duty = 0,
        .hpoint = 0,
        .flags = {
            .output_invert = 0,
        },
    };

    ESP_RETURN_ON_FALSE(CONFIG_CYD_SPEAKER_GPIO >= 0, ESP_ERR_INVALID_ARG, TAG, "speaker gpio is disabled");

    if (s_speaker_started) {
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(ledc_timer_config(&timer_conf), TAG, "ledc timer init failed");
    ESP_RETURN_ON_ERROR(ledc_channel_config(&channel_conf), TAG, "ledc channel init failed");

    s_speaker_queue = xQueueCreate(CYD_SPEAKER_QUEUE_LEN, sizeof(cyd_speaker_cmd_t));
    ESP_RETURN_ON_FALSE(s_speaker_queue != NULL, ESP_ERR_NO_MEM, TAG, "queue alloc failed");

    BaseType_t task_ok = xTaskCreatePinnedToCore(
        cyd_speaker_task,
        "cyd_speaker",
        CYD_SPEAKER_TASK_STACK,
        NULL,
        CYD_SPEAKER_TASK_PRIO,
        NULL,
        tskNO_AFFINITY
    );
    ESP_RETURN_ON_FALSE(task_ok == pdPASS, ESP_ERR_NO_MEM, TAG, "task create failed");

    s_speaker_started = true;
    ESP_LOGI(TAG, "CYD speaker configured: gpio=%d ledc_timer=%d ledc_channel=%d volume=%d%%",
             CONFIG_CYD_SPEAKER_GPIO,
             CONFIG_CYD_SPEAKER_LEDC_TIMER,
             CONFIG_CYD_SPEAKER_LEDC_CHANNEL,
             CONFIG_CYD_SPEAKER_VOLUME_PERCENT);
#else
    if (s_speaker_started) {
        return ESP_OK;
    }

    s_speaker_queue = xQueueCreate(CYD_SPEAKER_QUEUE_LEN, sizeof(cyd_speaker_cmd_t));
    ESP_RETURN_ON_FALSE(s_speaker_queue != NULL, ESP_ERR_NO_MEM, TAG, "queue alloc failed");
    s_speaker_started = true;
    ESP_LOGI(TAG, "CYD speaker disabled");
#endif

    return ESP_OK;
}

esp_err_t cyd_speaker_play_tone(uint32_t frequency_hz, uint32_t duration_ms)
{
    cyd_speaker_note_t note = {
        .frequency_hz = frequency_hz,
        .duration_ms = duration_ms,
        .gap_ms = 0,
    };

    return cyd_speaker_play_sequence(&note, 1);
}

esp_err_t cyd_speaker_play_sequence(const cyd_speaker_note_t *notes, size_t note_count)
{
    cyd_speaker_cmd_t cmd = {
        .id = CYD_SPEAKER_CMD_PLAY,
        .note_count = note_count,
    };

    ESP_RETURN_ON_FALSE(notes != NULL, ESP_ERR_INVALID_ARG, TAG, "notes is null");
    ESP_RETURN_ON_FALSE(note_count > 0 && note_count <= CYD_SPEAKER_MAX_NOTES, ESP_ERR_INVALID_ARG, TAG, "invalid note count");

    for (size_t i = 0; i < note_count; ++i) {
        ESP_RETURN_ON_ERROR(cyd_speaker_validate_note(&notes[i]), TAG, "invalid note");
    }

    memcpy(cmd.notes, notes, sizeof(cyd_speaker_note_t) * note_count);
    return cyd_speaker_send(&cmd);
}

esp_err_t cyd_speaker_play_event(cyd_speaker_event_t event)
{
    static const cyd_speaker_note_t click[] = {
        { .frequency_hz = 1800, .duration_ms = 35, .gap_ms = 0 },
    };
    static const cyd_speaker_note_t beep[] = {
        { .frequency_hz = 1200, .duration_ms = 80, .gap_ms = 0 },
    };
    static const cyd_speaker_note_t error[] = {
        { .frequency_hz = 330, .duration_ms = 160, .gap_ms = 40 },
        { .frequency_hz = 220, .duration_ms = 220, .gap_ms = 0 },
    };
    static const cyd_speaker_note_t warning[] = {
        { .frequency_hz = 660, .duration_ms = 120, .gap_ms = 80 },
        { .frequency_hz = 660, .duration_ms = 120, .gap_ms = 0 },
    };
    static const cyd_speaker_note_t alert[] = {
        { .frequency_hz = 880, .duration_ms = 120, .gap_ms = 60 },
        { .frequency_hz = 880, .duration_ms = 120, .gap_ms = 60 },
        { .frequency_hz = 880, .duration_ms = 120, .gap_ms = 0 },
    };
    static const cyd_speaker_note_t alarm[] = {
        { .frequency_hz = 880, .duration_ms = 180, .gap_ms = 80 },
        { .frequency_hz = 440, .duration_ms = 180, .gap_ms = 80 },
        { .frequency_hz = 880, .duration_ms = 180, .gap_ms = 80 },
        { .frequency_hz = 440, .duration_ms = 180, .gap_ms = 0 },
    };
    static const cyd_speaker_note_t success[] = {
        { .frequency_hz = 1200, .duration_ms = 60, .gap_ms = 20 },
        { .frequency_hz = 1800, .duration_ms = 90, .gap_ms = 0 },
    };

    switch (event) {
        case CYD_SPEAKER_EVENT_CLICK:
            return cyd_speaker_play_sequence(click, sizeof(click) / sizeof(click[0]));
        case CYD_SPEAKER_EVENT_BEEP:
            return cyd_speaker_play_sequence(beep, sizeof(beep) / sizeof(beep[0]));
        case CYD_SPEAKER_EVENT_ERROR:
            return cyd_speaker_play_sequence(error, sizeof(error) / sizeof(error[0]));
        case CYD_SPEAKER_EVENT_WARNING:
            return cyd_speaker_play_sequence(warning, sizeof(warning) / sizeof(warning[0]));
        case CYD_SPEAKER_EVENT_ALERT:
            return cyd_speaker_play_sequence(alert, sizeof(alert) / sizeof(alert[0]));
        case CYD_SPEAKER_EVENT_ALARM:
            return cyd_speaker_play_sequence(alarm, sizeof(alarm) / sizeof(alarm[0]));
        case CYD_SPEAKER_EVENT_SUCCESS:
            return cyd_speaker_play_sequence(success, sizeof(success) / sizeof(success[0]));
        default:
            return ESP_ERR_INVALID_ARG;
    }
}

esp_err_t cyd_speaker_stop(void)
{
    cyd_speaker_cmd_t cmd = {
        .id = CYD_SPEAKER_CMD_STOP,
        .note_count = 0,
    };

    if (s_speaker_queue != NULL) {
        xQueueReset(s_speaker_queue);
    }

    return cyd_speaker_send(&cmd);
}
