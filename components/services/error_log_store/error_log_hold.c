#include "error_log_hold.h"
#include <string.h>

/* Bytes are copied one at a time around the end of the ring. Lines are short and
   rare, so a straight loop is easier to trust than a two-part memcpy. */
static void ring_write(error_log_hold_t *hold, size_t from_head, const void *data, size_t size)
{
    const uint8_t *in = data;

    for (size_t i = 0; i < size; ++i) {
        hold->storage[(hold->head + from_head + i) % hold->capacity] = in[i];
    }
}

static void ring_read(const error_log_hold_t *hold, size_t from_head, void *data, size_t size)
{
    uint8_t *out = data;

    for (size_t i = 0; i < size; ++i) {
        out[i] = hold->storage[(hold->head + from_head + i) % hold->capacity];
    }
}

void error_log_hold_init(error_log_hold_t *hold, uint8_t *storage, size_t capacity)
{
    *hold = (error_log_hold_t){ .storage = storage, .capacity = capacity };
}

bool error_log_hold_push(error_log_hold_t *hold, uint64_t uptime_ms, bool raw, const char *text, size_t length)
{
    uint8_t header[ERROR_LOG_HOLD_ENTRY_OVERHEAD];
    size_t needed = ERROR_LOG_HOLD_ENTRY_OVERHEAD + length;

    if (hold->capacity == 0 || length > 0xffffU || needed > hold->capacity - hold->used) {
        ++hold->dropped;
        return false;
    }
    header[0] = (uint8_t)(length & 0xffU);
    header[1] = (uint8_t)(length >> 8);
    header[2] = raw ? 1U : 0U;
    header[3] = 0;
    for (size_t i = 0; i < 8U; ++i) {
        header[4U + i] = (uint8_t)(uptime_ms >> (8U * i));
    }
    ring_write(hold, hold->used, header, sizeof(header));
    ring_write(hold, hold->used + sizeof(header), text, length);
    hold->used += needed;
    ++hold->count;
    return true;
}

bool error_log_hold_pop(error_log_hold_t *hold,
                        uint64_t *uptime_ms,
                        bool *raw,
                        char *text,
                        size_t text_size,
                        size_t *length)
{
    uint8_t header[ERROR_LOG_HOLD_ENTRY_OVERHEAD];

    if (hold->count == 0) {
        return false;
    }
    ring_read(hold, 0, header, sizeof(header));
    size_t entry_length = (size_t)header[0] | ((size_t)header[1] << 8);

    if (uptime_ms != NULL) {
        uint64_t value = 0;
        for (size_t i = 0; i < 8U; ++i) {
            value |= (uint64_t)header[4U + i] << (8U * i);
        }
        *uptime_ms = value;
    }
    if (raw != NULL) {
        *raw = header[2] != 0;
    }
    size_t copied = 0;
    if (text != NULL && text_size > 0) {
        copied = entry_length < text_size - 1U ? entry_length : text_size - 1U;
        ring_read(hold, sizeof(header), text, copied);
        text[copied] = '\0';
    }
    if (length != NULL) {
        *length = copied;
    }

    size_t entry_size = ERROR_LOG_HOLD_ENTRY_OVERHEAD + entry_length;
    hold->head = (hold->head + entry_size) % hold->capacity;
    hold->used -= entry_size;
    --hold->count;
    if (hold->count == 0) {
        hold->head = 0; /* keeps the next entries in one piece, which is easier to inspect */
    }
    return true;
}

bool error_log_hold_peek_uptime(const error_log_hold_t *hold, uint64_t *uptime_ms)
{
    uint8_t header[ERROR_LOG_HOLD_ENTRY_OVERHEAD];
    uint64_t value = 0;

    if (hold->count == 0) {
        return false;
    }
    ring_read(hold, 0, header, sizeof(header));
    for (size_t i = 0; i < 8U; ++i) {
        value |= (uint64_t)header[4U + i] << (8U * i);
    }
    *uptime_ms = value;
    return true;
}

bool error_log_hold_is_empty(const error_log_hold_t *hold)
{
    return hold->count == 0;
}

uint32_t error_log_hold_count(const error_log_hold_t *hold)
{
    return hold->count;
}

uint32_t error_log_hold_take_dropped(error_log_hold_t *hold)
{
    uint32_t dropped = hold->dropped;

    hold->dropped = 0;
    return dropped;
}
