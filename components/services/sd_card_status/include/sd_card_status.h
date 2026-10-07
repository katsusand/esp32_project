#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * What state the SD card is in, for the screen and for the log.
 *
 * English contract: the card is optional. Every state other than OK means "the
 * terminal works, but nothing is being logged to the card"; none of them is a
 * reason to fail the boot or to stop the main app. This component owns the whole life cycle
 * (mount, check, unmount, mount again) and the error log stream on top of it, so
 * nothing else should call sd_card_storage_init() or error_log_store_start().
 */
typedef enum {
    /* Not started, or built without SD support. Nothing to show. */
    SD_CARD_STATUS_DISABLED = 0,
    /* Mounted and logging. */
    SD_CARD_STATUS_OK,
    /* Not inserted, or inserted but not answering. */
    SD_CARD_STATUS_NO_CARD,
    /* The card answers but its filesystem cannot be mounted: not formatted, not
       FAT (exFAT is not supported), or unreadable. It is never formatted here. */
    SD_CARD_STATUS_NO_FILESYSTEM,
    /* Mounted, but free space is below the limit. Logging is stopped, not
       failing: the card can be read as it is. */
    SD_CARD_STATUS_FULL,
    /* Anything else: the card mounted but would not take a write, the log stream
       failed, or the host side could not set the card up. */
    SD_CARD_STATUS_FAULT,
} sd_card_status_state_t;

typedef struct {
    sd_card_status_state_t state;
    /* What put it in this state; ESP_OK for OK. */
    esp_err_t last_error;
    /* 0 until the volume has been measured. */
    uint64_t total_bytes;
    uint64_t free_bytes;
    /* Bumped on every change of `state`. */
    uint32_t revision;
} sd_card_status_info_t;

/*
 * Starts the task that mounts the card (right away, then again whenever it is
 * missing or failed) and keeps checking it. Returns at once: a missing or
 * unusable card is a state to show, not an error. Only a failure to create the
 * task is returned.
 *
 * English contract: call once, after system boot and before anything whose
 * errors should reach the card (Wi-Fi, time sync).
 */
esp_err_t sd_card_status_start(void);

sd_card_status_state_t sd_card_status_get_state(void);
/* Changes whenever the state does, so a screen can tell its icon is stale. */
uint32_t sd_card_status_get_revision(void);
void sd_card_status_get_info(sd_card_status_info_t *info);

/* "OK", "NO_CARD", ... for logs. */
const char *sd_card_status_state_name(sd_card_status_state_t state);
/* True for the states that deserve an icon. */
bool sd_card_status_state_is_problem(sd_card_status_state_t state);

/*
 * Decisions without side effects, split out so they can be tested on the host.
 */

/* Sorts what sd_card_storage_init() returned. See that function for the codes. */
sd_card_status_state_t sd_card_status_classify_mount_error(esp_err_t err);
/*
 * Sorts the result of writing `expected` bytes to the self-test file. A short
 * write, or ENOSPC, is a full card; any other failure is a fault.
 */
sd_card_status_state_t sd_card_status_classify_write_result(ssize_t written, size_t expected, int err_no);
/* Below the limit, or nothing left at all (also when the limit is 0). */
bool sd_card_status_is_full(uint64_t free_bytes, uint64_t min_free_bytes);
/* Next wait after a failed mount: `initial` first, then doubling, up to `max`. */
uint32_t sd_card_status_next_retry_ms(uint32_t current_ms, uint32_t initial_ms, uint32_t max_ms);

#ifdef __cplusplus
}
#endif
