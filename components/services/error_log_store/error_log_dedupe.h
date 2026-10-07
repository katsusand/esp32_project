#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Collapses an error that repeats into one line and a count. No SDK in here, so
 * it can be tested on the host.
 *
 * The same error twenty times a second (a Wi-Fi retry loop, a card that keeps
 * failing) would fill the log and push out everything else. The first one is
 * written; the same one again within the window is counted instead; when the
 * window ends a single line says how many were left out.
 *
 * "The same" means the same tag, function, line, code AND message. A message
 * that differs (a different card number, say) is a different event and is never
 * folded, so nothing is lost by folding.
 *
 * English contract: not thread safe, the caller holds a lock. The caller owns
 * the clock (`now_ms`) and the timer: it asks error_log_dedupe_next_due_ms() when
 * to look again, and calls error_log_dedupe_collect() then. A burst's count is
 * therefore reported at the end of its window even if nothing else is logged.
 */
#define ERROR_LOG_DEDUPE_SLOTS 8U
#define ERROR_LOG_DEDUPE_LABEL_MAX 64U

typedef struct {
    bool used;
    uint32_t key;
    uint64_t last_emit_ms;
    uint64_t last_seen_ms;
    uint32_t suppressed;
    char label[ERROR_LOG_DEDUPE_LABEL_MAX];
} error_log_dedupe_slot_t;

typedef struct {
    error_log_dedupe_slot_t slots[ERROR_LOG_DEDUPE_SLOTS];
    uint32_t window_ms; /* 0 turns folding off */
} error_log_dedupe_t;

/* A burst that ended: how many repeats were left out, and which event it was. */
typedef struct {
    uint32_t count;
    char label[ERROR_LOG_DEDUPE_LABEL_MAX];
} error_log_repeat_t;

void error_log_dedupe_init(error_log_dedupe_t *dedupe, uint32_t window_ms);

uint32_t error_log_dedupe_key(const char *tag, const char *func, int line, int32_t code, const char *message);

/*
 * True when the event is to be written. False when it is a repeat inside the
 * window and was only counted. `reports` (room for 2) receives any bursts that
 * ended because of this call: the same event again after its window, or an
 * older event pushed out of the table.
 */
bool error_log_dedupe_check(error_log_dedupe_t *dedupe,
                            uint32_t key,
                            const char *label,
                            uint64_t now_ms,
                            error_log_repeat_t reports[2],
                            size_t *report_count);

/* Bursts whose window has ended (all of them with `force`), and forgets them. Returns how many. */
size_t error_log_dedupe_collect(error_log_dedupe_t *dedupe,
                                uint64_t now_ms,
                                bool force,
                                error_log_repeat_t *out,
                                size_t max);

/* Milliseconds until the first pending burst's window ends, or false when none is pending. */
bool error_log_dedupe_next_due_ms(const error_log_dedupe_t *dedupe, uint64_t now_ms, uint64_t *due_in_ms);
