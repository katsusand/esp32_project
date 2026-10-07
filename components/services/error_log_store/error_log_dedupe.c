#include "error_log_dedupe.h"
#include <string.h>

static uint32_t fnv1a(uint32_t hash, const void *data, size_t size)
{
    const uint8_t *bytes = data;

    for (size_t i = 0; i < size; ++i) {
        hash ^= bytes[i];
        hash *= 16777619U;
    }
    return hash;
}

uint32_t error_log_dedupe_key(const char *tag, const char *func, int line, int32_t code, const char *message)
{
    static const char separator = '\0';
    uint32_t hash = 2166136261U;
    const char *parts[3] = { tag != NULL ? tag : "", func != NULL ? func : "", message != NULL ? message : "" };

    hash = fnv1a(hash, parts[0], strlen(parts[0]));
    hash = fnv1a(hash, &separator, 1);
    hash = fnv1a(hash, parts[1], strlen(parts[1]));
    hash = fnv1a(hash, &separator, 1);
    hash = fnv1a(hash, &line, sizeof(line));
    hash = fnv1a(hash, &code, sizeof(code));
    hash = fnv1a(hash, parts[2], strlen(parts[2]));
    return hash;
}

void error_log_dedupe_init(error_log_dedupe_t *dedupe, uint32_t window_ms)
{
    memset(dedupe, 0, sizeof(*dedupe));
    dedupe->window_ms = window_ms;
}

static void make_report(error_log_repeat_t *report, const error_log_dedupe_slot_t *slot)
{
    report->count = slot->suppressed;
    memcpy(report->label, slot->label, sizeof(report->label));
}

bool error_log_dedupe_check(error_log_dedupe_t *dedupe,
                            uint32_t key,
                            const char *label,
                            uint64_t now_ms,
                            error_log_repeat_t reports[2],
                            size_t *report_count)
{
    error_log_dedupe_slot_t *slot = NULL;
    error_log_dedupe_slot_t *victim = NULL;

    *report_count = 0;
    if (dedupe->window_ms == 0) {
        return true;
    }

    for (size_t i = 0; i < ERROR_LOG_DEDUPE_SLOTS; ++i) {
        error_log_dedupe_slot_t *candidate = &dedupe->slots[i];
        if (candidate->used && candidate->key == key) {
            slot = candidate;
            break;
        }
    }

    if (slot != NULL) {
        slot->last_seen_ms = now_ms;
        if (now_ms - slot->last_emit_ms < dedupe->window_ms) {
            ++slot->suppressed;
            return false;
        }
        if (slot->suppressed > 0) {
            make_report(&reports[(*report_count)++], slot);
            slot->suppressed = 0;
        }
        slot->last_emit_ms = now_ms;
        return true;
    }

    /* A new event: an empty slot, else the least recently seen one that has
       nothing waiting to be reported, else the least recently seen of all. */
    for (size_t i = 0; i < ERROR_LOG_DEDUPE_SLOTS; ++i) {
        error_log_dedupe_slot_t *candidate = &dedupe->slots[i];
        if (!candidate->used) {
            victim = candidate;
            break;
        }
    }
    if (victim == NULL) {
        for (int pass = 0; pass < 2 && victim == NULL; ++pass) {
            for (size_t i = 0; i < ERROR_LOG_DEDUPE_SLOTS; ++i) {
                error_log_dedupe_slot_t *candidate = &dedupe->slots[i];
                if (pass == 0 && candidate->suppressed > 0) {
                    continue;
                }
                if (victim == NULL || candidate->last_seen_ms < victim->last_seen_ms) {
                    victim = candidate;
                }
            }
        }
        if (victim->suppressed > 0) {
            make_report(&reports[(*report_count)++], victim);
        }
    }

    victim->used = true;
    victim->key = key;
    victim->last_emit_ms = now_ms;
    victim->last_seen_ms = now_ms;
    victim->suppressed = 0;
    strncpy(victim->label, label != NULL ? label : "", sizeof(victim->label) - 1U);
    victim->label[sizeof(victim->label) - 1U] = '\0';
    return true;
}

size_t error_log_dedupe_collect(error_log_dedupe_t *dedupe,
                                uint64_t now_ms,
                                bool force,
                                error_log_repeat_t *out,
                                size_t max)
{
    size_t count = 0;

    for (size_t i = 0; i < ERROR_LOG_DEDUPE_SLOTS && count < max; ++i) {
        error_log_dedupe_slot_t *slot = &dedupe->slots[i];
        if (!slot->used || slot->suppressed == 0) {
            continue;
        }
        if (force || now_ms - slot->last_emit_ms >= dedupe->window_ms) {
            make_report(&out[count++], slot);
            /* The burst is over; the next one starts fresh and is written. */
            slot->used = false;
            slot->suppressed = 0;
        }
    }
    return count;
}

bool error_log_dedupe_next_due_ms(const error_log_dedupe_t *dedupe, uint64_t now_ms, uint64_t *due_in_ms)
{
    bool found = false;
    uint64_t best = 0;

    for (size_t i = 0; i < ERROR_LOG_DEDUPE_SLOTS; ++i) {
        const error_log_dedupe_slot_t *slot = &dedupe->slots[i];
        if (!slot->used || slot->suppressed == 0) {
            continue;
        }
        uint64_t end = slot->last_emit_ms + dedupe->window_ms;
        uint64_t remaining = end > now_ms ? end - now_ms : 0;
        if (!found || remaining < best) {
            best = remaining;
            found = true;
        }
    }
    if (found && due_in_ms != NULL) {
        *due_in_ms = best;
    }
    return found;
}
