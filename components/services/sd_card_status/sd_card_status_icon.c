#include <stdbool.h>
#include <stddef.h>
#include "esp_log.h"
#include "sdkconfig.h"
#include "sd_card_status_icon.h"
#include "sd_card_status_icon_art.h"

#if CONFIG_SD_CARD_STATUS_SHOW_ICON

#define TAG "sd_card_status_icon"

/* Static, not on the stack: the display task reads these pixels later. */
static uint16_t s_pixels[SD_CARD_STATUS_ICON_KIND_COUNT][SD_CARD_STATUS_ICON_SIZE_PX * SD_CARD_STATUS_ICON_SIZE_PX];
static cyd_display_bitmap_t s_bitmaps[SD_CARD_STATUS_ICON_KIND_COUNT];
static bool s_ready[SD_CARD_STATUS_ICON_KIND_COUNT];

static bool sd_card_status_icon_kind_for_state(sd_card_status_state_t state, sd_card_status_icon_kind_t *kind)
{
    switch (state) {
    case SD_CARD_STATUS_NO_CARD:
        *kind = SD_CARD_STATUS_ICON_NO_CARD;
        return true;
    case SD_CARD_STATUS_NO_FILESYSTEM:
        *kind = SD_CARD_STATUS_ICON_NO_FILESYSTEM;
        return true;
    case SD_CARD_STATUS_FULL:
        *kind = SD_CARD_STATUS_ICON_FULL;
        return true;
    case SD_CARD_STATUS_FAULT:
        *kind = SD_CARD_STATUS_ICON_FAULT;
        return true;
    case SD_CARD_STATUS_DISABLED:
    case SD_CARD_STATUS_OK:
    default:
        return false;
    }
}

const cyd_display_bitmap_t *sd_card_status_icon_for_state(sd_card_status_state_t state)
{
    sd_card_status_icon_kind_t kind = SD_CARD_STATUS_ICON_NO_CARD;

    if (!sd_card_status_icon_kind_for_state(state, &kind)) {
        return NULL;
    }
    if (!s_ready[kind]) {
        if (!sd_card_status_icon_art_render(kind, s_pixels[kind])) {
            /* Covered by a host test; a missing icon beats a garbled one. */
            ESP_LOGE(TAG, "icon art for kind %d is malformed", (int)kind);
            return NULL;
        }
        s_bitmaps[kind] = (cyd_display_bitmap_t){
            .data = s_pixels[kind],
            .width_px = SD_CARD_STATUS_ICON_SIZE_PX,
            .height_px = SD_CARD_STATUS_ICON_SIZE_PX,
        };
        s_ready[kind] = true;
    }
    return &s_bitmaps[kind];
}

#else /* CONFIG_SD_CARD_STATUS_SHOW_ICON */

const cyd_display_bitmap_t *sd_card_status_icon_for_state(sd_card_status_state_t state)
{
    (void)state;
    return NULL;
}

#endif /* CONFIG_SD_CARD_STATUS_SHOW_ICON */
