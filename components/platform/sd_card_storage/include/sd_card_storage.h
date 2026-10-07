#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Mounts the card. Safe to call again after a failure: a failed call leaves
 * nothing allocated, so a card inserted or formatted later can be picked up by
 * calling it again.
 *
 * English contract: the error code says how far it got. ESP_FAIL means the card
 * answered but its filesystem could not be mounted (not formatted, or not FAT).
 * Any other code means the card itself could not be brought up (usually no card
 * inserted, ESP_ERR_TIMEOUT) or the host side failed (ESP_ERR_NO_MEM, an SPI
 * bus that is already taken). There is no automatic format, on purpose.
 */
esp_err_t sd_card_storage_init(void);
bool sd_card_storage_is_mounted(void);
const char *sd_card_storage_get_mount_point(void);

/*
 * Unmounts and releases the SPI bus, so sd_card_storage_init() can run again
 * (a card that was swapped, or put back after being read on a PC). A no-op when
 * nothing is mounted. The state is reset even when the unmount reports an
 * error, because the card may already be gone; the error is returned for the
 * log.
 *
 * English contract: every file on the card must be closed first, and
 * init / deinit / probe / get_space must all be called from one task. There is
 * no locking here.
 */
esp_err_t sd_card_storage_deinit(void);

/*
 * Checks that the card still answers by reading its first sector. Goes around
 * the filesystem on purpose: FATFS caches enough that stat() and the free-space
 * count keep succeeding after the card was pulled. ESP_OK when it answered,
 * ESP_ERR_INVALID_STATE when nothing is mounted, otherwise the card's error.
 */
esp_err_t sd_card_storage_probe(void);

/*
 * Capacity and free bytes of the mounted volume. The first call on a card whose
 * FAT32 info sector is unset has to scan the FAT, which can take seconds, so do
 * not call it from a task that must stay responsive.
 */
esp_err_t sd_card_storage_get_space(uint64_t *total_bytes, uint64_t *free_bytes);

#ifdef __cplusplus
}
#endif
