#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * A first-in first-out buffer for log lines that cannot be written yet. No SDK
 * in here, so it can be tested on the host.
 *
 * Lines wait here while the card is not ready, or while it is not known yet
 * whether the clock is right. An entry keeps the uptime it was made at, not a
 * wall clock, so that once the clock is known the line can be stamped with the
 * time it really had (see error_log_restamp()).
 *
 * English contract: when it is full the NEWEST lines are refused and counted,
 * and the oldest are kept. What happened first is what explains the rest, and a
 * boot that is going wrong tends to repeat itself, so the repeats are the part
 * that can be spared. The count is taken with error_log_hold_take_dropped() and
 * written into the log as its own line, so the loss is on record.
 *
 * Not thread safe: the caller holds a lock.
 */
typedef struct {
    uint8_t *storage;
    size_t capacity;
    size_t head;       /* where the oldest entry starts */
    size_t used;       /* bytes in use, headers included */
    uint32_t count;    /* entries */
    uint32_t dropped;  /* entries refused for lack of room since the last take */
} error_log_hold_t;

/* Bytes an entry costs besides its text. */
#define ERROR_LOG_HOLD_ENTRY_OVERHEAD 12U

void error_log_hold_init(error_log_hold_t *hold, uint8_t *storage, size_t capacity);

/*
 * Adds one entry. `raw` marks text that is already a finished line (it keeps its
 * own stamp); otherwise the text is a body that still needs one. False, and the
 * drop counted, when it does not fit.
 */
bool error_log_hold_push(error_log_hold_t *hold, uint64_t uptime_ms, bool raw, const char *text, size_t length);

/* Removes the oldest entry. The text is NUL terminated and cut to `text_size`. False when empty. */
bool error_log_hold_pop(error_log_hold_t *hold,
                        uint64_t *uptime_ms,
                        bool *raw,
                        char *text,
                        size_t text_size,
                        size_t *length);

bool error_log_hold_is_empty(const error_log_hold_t *hold);
uint32_t error_log_hold_count(const error_log_hold_t *hold);

/* How many entries were refused since the last call; the count starts again. */
uint32_t error_log_hold_take_dropped(error_log_hold_t *hold);
