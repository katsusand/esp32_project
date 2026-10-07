#pragma once

#include <stdbool.h>
#include <stdint.h>

/*
 * The SD card status icons, as 16x16 pixel art.
 *
 * This file knows nothing about the display or the card: it only turns a kind of
 * problem into RGB565 pixels, so the art can be checked on the host (see
 * test/host/test_sd_card_status_icon_art.c). sd_card_status_icon.c connects it to a screen.
 */

#define SD_CARD_STATUS_ICON_SIZE_PX 16

typedef enum {
    /* Not inserted, or not answering: a grey card struck through in red. */
    SD_CARD_STATUS_ICON_NO_CARD = 0,
    /* Inserted but not formatted, or not FAT: a yellow card with "?". */
    SD_CARD_STATUS_ICON_NO_FILESYSTEM,
    /* No free space left: an orange card with a completely filled gauge. */
    SD_CARD_STATUS_ICON_FULL,
    /* Would not take a write, or anything else: a red card with "!". */
    SD_CARD_STATUS_ICON_FAULT,
    SD_CARD_STATUS_ICON_KIND_COUNT,
} sd_card_status_icon_kind_t;

/* SD_CARD_STATUS_ICON_SIZE_PX strings of SD_CARD_STATUS_ICON_SIZE_PX characters, or NULL for an unknown kind. */
const char *const *sd_card_status_icon_art_rows(sd_card_status_icon_kind_t kind);

/*
 * Fills `pixels` (SD_CARD_STATUS_ICON_SIZE_PX squared RGB565 values, row by row).
 * False, with `pixels` left unusable, when the art has a row of the wrong length
 * or a symbol with no colour: a typo in the art must show up in a test, not as
 * a smudged icon on a unit.
 */
bool sd_card_status_icon_art_render(sd_card_status_icon_kind_t kind, uint16_t *pixels);
