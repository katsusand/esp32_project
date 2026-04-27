#ifndef CYD_SPEAKER_H
#define CYD_SPEAKER_H

#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CYD_SPEAKER_MAX_NOTES 8

typedef enum {
    CYD_SPEAKER_EVENT_CLICK = 0,
    CYD_SPEAKER_EVENT_BEEP,
    CYD_SPEAKER_EVENT_ERROR,
    CYD_SPEAKER_EVENT_WARNING,
    CYD_SPEAKER_EVENT_ALERT,
    CYD_SPEAKER_EVENT_ALARM,
    CYD_SPEAKER_EVENT_SUCCESS,
} cyd_speaker_event_t;

typedef struct {
    uint32_t frequency_hz;
    uint32_t duration_ms;
    uint32_t gap_ms;
} cyd_speaker_note_t;

esp_err_t cyd_speaker_init(void);
esp_err_t cyd_speaker_play_tone(uint32_t frequency_hz, uint32_t duration_ms);
esp_err_t cyd_speaker_play_sequence(const cyd_speaker_note_t *notes, size_t note_count);
esp_err_t cyd_speaker_play_event(cyd_speaker_event_t event);
esp_err_t cyd_speaker_stop(void);

#ifdef __cplusplus
}
#endif

#endif
