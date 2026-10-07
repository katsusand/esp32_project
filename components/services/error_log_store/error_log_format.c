#include "error_log_format.h"
#include <stdio.h>
#include <string.h>

typedef struct {
    char *buf;
    size_t size; /* usable bytes, NUL excluded */
    size_t pos;
    bool truncated;
} error_log_builder_t;

static void builder_put(error_log_builder_t *b, const char *text, size_t length, bool sanitize)
{
    for (size_t i = 0; i < length; ++i) {
        if (b->pos >= b->size) {
            b->truncated = true;
            return;
        }
        unsigned char c = (unsigned char)text[i];
        b->buf[b->pos++] = (sanitize && (c < 0x20U || c == 0x7fU)) ? ' ' : (char)c;
    }
}

static void builder_puts(error_log_builder_t *b, const char *text, bool sanitize)
{
    builder_put(b, text, strlen(text), sanitize);
}

/* Ends the text in "..." when something was left out. */
static void builder_finish(error_log_builder_t *b)
{
    if (b->truncated) {
        size_t keep = b->size >= 3U ? b->size - 3U : 0U;
        if (b->pos > keep) {
            b->pos = keep;
        }
        builder_put(b, "...", 3, false);
    }
    b->buf[b->pos] = '\0';
}

size_t error_log_format_stamp(char *out, size_t out_size, const struct tm *local, uint64_t uptime_ms)
{
    int written = snprintf(out,
                           out_size,
                           "[%04d-%02d-%02d %02d:%02d:%02d +%llums]",
                           local->tm_year + 1900,
                           local->tm_mon + 1,
                           local->tm_mday,
                           local->tm_hour,
                           local->tm_min,
                           local->tm_sec,
                           (unsigned long long)uptime_ms);

    if (written < 0 || (size_t)written >= out_size) {
        if (out_size > 0) {
            out[0] = '\0';
        }
        return 0;
    }
    return (size_t)written;
}

size_t error_log_format_body(char *out, size_t out_size, const error_log_fields_t *fields)
{
    error_log_builder_t b = { .buf = out, .size = out_size > 0 ? out_size - 1U : 0U };
    char number[24];

    if (out_size == 0) {
        return 0;
    }

    builder_puts(&b, fields->tag != NULL ? fields->tag : "?", true);
    if (fields->func != NULL && fields->func[0] != '\0') {
        builder_put(&b, " ", 1, false);
        builder_puts(&b, fields->func, true);
        (void)snprintf(number, sizeof(number), ":%d", fields->line);
        builder_puts(&b, number, false);
    }
    if (fields->has_code) {
        builder_put(&b, " ", 1, false);
        builder_puts(&b, fields->code_name != NULL ? fields->code_name : "ERR", true);
        /* ESP_FAIL is -1, and 0xffffffff would read as a different error. */
        if (fields->code < 0) {
            (void)snprintf(number, sizeof(number), "(%d)", (int)fields->code);
        } else {
            (void)snprintf(number, sizeof(number), "(0x%x)", (unsigned)fields->code);
        }
        builder_puts(&b, number, false);
    }
    if (fields->message != NULL && fields->message[0] != '\0') {
        builder_put(&b, ": ", 2, false);
        builder_puts(&b, fields->message, true);
    }
    if (fields->message_cut) {
        b.truncated = true;
    }
    builder_finish(&b);
    return b.pos;
}

size_t error_log_format_line(char *out, size_t out_size, const char *stamp, const char *body)
{
    error_log_builder_t b;

    if (out_size < 2U) {
        if (out_size == 1U) {
            out[0] = '\0';
        }
        return 0;
    }
    /* One byte for the newline, one for the NUL. */
    b = (error_log_builder_t){ .buf = out, .size = out_size - 2U };
    builder_puts(&b, stamp, false);
    builder_put(&b, " ", 1, false);
    builder_puts(&b, body, true);
    builder_finish(&b);
    out[b.pos++] = '\n';
    out[b.pos] = '\0';
    return b.pos;
}

time_t error_log_restamp(time_t now_wall, uint64_t now_uptime_ms, uint64_t entry_uptime_ms)
{
    if (entry_uptime_ms >= now_uptime_ms) {
        return now_wall;
    }
    return now_wall - (time_t)((now_uptime_ms - entry_uptime_ms) / 1000U);
}
