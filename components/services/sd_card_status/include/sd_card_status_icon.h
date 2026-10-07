#pragma once

#include "cyd_display.h"
#include "sd_card_status.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The icon for a card state, or NULL when nothing should be drawn: the state
 * needs none (OK, DISABLED) or CONFIG_SD_CARD_STATUS_SHOW_ICON is off.
 *
 * The icon is 16x16 pixels, which is two grid cells square, and is drawn opaque
 * on a black screen. Put it in a corner nothing else uses:
 *
 *     const cyd_display_bitmap_t *icon = sd_card_status_icon_for_state(sd_card_status_get_state());
 *     if (icon != NULL) {
 *         cyd_ui_add_icon(screen, icon, 38, 0, 2, 2);
 *     }
 *
 * English contract: add it last, so the widgets before it keep their positions
 * in the list whether or not the icon is there (the display compares screens
 * widget by widget). The returned bitmap and its pixels live for the whole run,
 * as cyd_display_bitmap_t requires: the display task reads them after submit()
 * returns. Call from one task (the app's), because the pixels are built on first
 * use. A screen that shows the icon should draw again when
 * sd_card_status_get_revision() changes.
 */
const cyd_display_bitmap_t *sd_card_status_icon_for_state(sd_card_status_state_t state);

#ifdef __cplusplus
}
#endif
