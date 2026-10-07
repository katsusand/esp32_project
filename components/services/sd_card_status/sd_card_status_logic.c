#include <errno.h>
#include "sd_card_status.h"

const char *sd_card_status_state_name(sd_card_status_state_t state)
{
    switch (state) {
    case SD_CARD_STATUS_DISABLED:
        return "DISABLED";
    case SD_CARD_STATUS_OK:
        return "OK";
    case SD_CARD_STATUS_NO_CARD:
        return "NO_CARD";
    case SD_CARD_STATUS_NO_FILESYSTEM:
        return "NO_FILESYSTEM";
    case SD_CARD_STATUS_FULL:
        return "FULL";
    case SD_CARD_STATUS_FAULT:
        return "FAULT";
    default:
        return "?";
    }
}

bool sd_card_status_state_is_problem(sd_card_status_state_t state)
{
    return state == SD_CARD_STATUS_NO_CARD ||
           state == SD_CARD_STATUS_NO_FILESYSTEM ||
           state == SD_CARD_STATUS_FULL ||
           state == SD_CARD_STATUS_FAULT;
}

sd_card_status_state_t sd_card_status_classify_mount_error(esp_err_t err)
{
    switch (err) {
    case ESP_OK:
        return SD_CARD_STATUS_OK;
    case ESP_FAIL:
        /* The card answered and the FAT mount failed. */
        return SD_CARD_STATUS_NO_FILESYSTEM;
    case ESP_ERR_NO_MEM:
    case ESP_ERR_INVALID_ARG:
    case ESP_ERR_INVALID_STATE:
        /* The host side: no memory, a bad configuration, a bus already taken.
           Reinserting a card would not help, so it must not read as "no card". */
        return SD_CARD_STATUS_FAULT;
    default:
        /* Timeout, invalid response, bad CRC: what an empty slot (or a card that
           does not answer) looks like while the card is being brought up. */
        return SD_CARD_STATUS_NO_CARD;
    }
}

sd_card_status_state_t sd_card_status_classify_write_result(ssize_t written, size_t expected, int err_no)
{
    if (written >= 0 && (size_t)written == expected) {
        return SD_CARD_STATUS_OK;
    }
    if (err_no == ENOSPC || (written >= 0 && (size_t)written < expected)) {
        return SD_CARD_STATUS_FULL;
    }
    return SD_CARD_STATUS_FAULT;
}

bool sd_card_status_is_full(uint64_t free_bytes, uint64_t min_free_bytes)
{
    return free_bytes == 0 || free_bytes < min_free_bytes;
}

uint32_t sd_card_status_next_retry_ms(uint32_t current_ms, uint32_t initial_ms, uint32_t max_ms)
{
    if (initial_ms > max_ms) {
        initial_ms = max_ms;
    }
    if (current_ms < initial_ms) {
        return initial_ms;
    }
    if (current_ms >= max_ms / 2U) {
        return max_ms;
    }
    return current_ms * 2U;
}
