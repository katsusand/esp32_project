#include <string.h>
#include "cyd_ui_fonts.h"

#define CYD_UI_FONT_REPLACEMENT 0xFFFDu

const cyd_ui_font_glyph_t *cyd_ui_font_find_glyph(const cyd_ui_font_t *font, uint16_t codepoint)
{
    if (font == NULL || font->glyph_count == 0) {
        return NULL;
    }

    size_t lo = 0;
    size_t hi = font->glyph_count;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2U;
        uint16_t found = font->codepoints[mid];
        if (found == codepoint) {
            return &font->glyphs[mid];
        }
        if (found < codepoint) {
            lo = mid + 1U;
        } else {
            hi = mid;
        }
    }
    return NULL;
}

uint16_t cyd_ui_font_next_codepoint(const char **text)
{
    const unsigned char *p = (const unsigned char *)*text;
    uint32_t codepoint = 0;
    size_t extra = 0;

    if (p[0] == 0) {
        return 0;
    }
    if (p[0] < 0x80) {
        *text += 1;
        return p[0];
    }
    if ((p[0] & 0xE0) == 0xC0) {
        codepoint = p[0] & 0x1Fu;
        extra = 1;
    } else if ((p[0] & 0xF0) == 0xE0) {
        codepoint = p[0] & 0x0Fu;
        extra = 2;
    } else if ((p[0] & 0xF8) == 0xF0) {
        codepoint = p[0] & 0x07u;
        extra = 3;
    } else {
        *text += 1;
        return CYD_UI_FONT_REPLACEMENT;
    }

    for (size_t i = 1; i <= extra; ++i) {
        /* Also stops at the NUL, which is not a continuation byte. */
        if ((p[i] & 0xC0) != 0x80) {
            *text += i;
            return CYD_UI_FONT_REPLACEMENT;
        }
        codepoint = (codepoint << 6) | (p[i] & 0x3Fu);
    }
    *text += extra + 1U;
    return codepoint > 0xFFFFu ? CYD_UI_FONT_REPLACEMENT : (uint16_t)codepoint;
}

int32_t cyd_ui_font_codepoint_advance(const cyd_ui_font_t *font, uint16_t codepoint)
{
    const cyd_ui_font_glyph_t *glyph = cyd_ui_font_find_glyph(font, codepoint);
    if (glyph != NULL) {
        return glyph->x_advance;
    }
    glyph = cyd_ui_font_find_glyph(font, ' ');
    return glyph != NULL ? glyph->x_advance : (font != NULL ? font->pixel_size / 4 : 0);
}

int32_t cyd_ui_font_text_width(const cyd_ui_font_t *font, const char *text)
{
    int32_t width = 0;

    if (font == NULL || text == NULL) {
        return 0;
    }
    while (*text != '\0') {
        width += cyd_ui_font_codepoint_advance(font, cyd_ui_font_next_codepoint(&text));
    }
    return width;
}

/* Characters that must not begin a line (gyoto kinsoku), kept short on purpose. */
static bool cyd_ui_font_is_no_line_start(uint16_t cp)
{
    static const uint16_t k_no_start[] = {
        0x3001, 0x3002, 0xFF0C, 0xFF0E, 0x002C, 0x002E, /* 、。，．,. */
        0x30FC, 0x2026, 0x2025, 0x30FB, 0xFF1A, 0xFF1B, /* ー…‥・：； */
        0xFF01, 0xFF1F, 0x0021, 0x003F,                 /* ！？!? */
        0x300D, 0x300F, 0x3011, 0x3015, 0x3009, 0x300B, /* 」』】〕〉》 */
        0xFF09, 0x0029, 0xFF3D, 0x005D, 0xFF5D, 0x007D, /* ）)］]｝} */
        0x3041, 0x3043, 0x3045, 0x3047, 0x3049, 0x3063, 0x3083, 0x3085, 0x3087, 0x308E,
        0x30A1, 0x30A3, 0x30A5, 0x30A7, 0x30A9, 0x30C3, 0x30E3, 0x30E5, 0x30E7, 0x30EE,
        0x3005, 0x309D, 0x309E, 0x30FD, 0x30FE,         /* 々ゝゞヽヾ */
    };
    for (size_t i = 0; i < sizeof(k_no_start) / sizeof(k_no_start[0]); ++i) {
        if (k_no_start[i] == cp) {
            return true;
        }
    }
    return false;
}

static bool cyd_ui_font_is_word_char(uint16_t cp)
{
    return (cp >= '0' && cp <= '9') || (cp >= 'A' && cp <= 'Z') || (cp >= 'a' && cp <= 'z');
}

/* Code point starting at byte `at`; sets `*next` to the byte after it. */
static uint16_t cyd_ui_font_codepoint_at(const char *text, size_t at, size_t *next)
{
    const char *p = text + at;
    uint16_t cp = cyd_ui_font_next_codepoint(&p);
    *next = (size_t)(p - text);
    return cp;
}

/* Byte offset of the character before `at` (at > 0). */
static size_t cyd_ui_font_prev_start(const char *text, size_t at)
{
    size_t i = at - 1U;
    while (i > 0 && (((unsigned char)text[i]) & 0xC0) == 0x80) {
        --i;
    }
    return i;
}

size_t cyd_ui_font_wrap(const cyd_ui_font_t *font,
                        const char *text,
                        int32_t max_px,
                        cyd_ui_font_line_t *lines,
                        size_t max_lines,
                        bool *truncated)
{
    size_t count = 0;
    size_t pos = 0;
    size_t len = 0;

    if (truncated != NULL) {
        *truncated = false;
    }
    if (font == NULL || text == NULL || lines == NULL || max_lines == 0) {
        return 0;
    }
    len = strlen(text);
    if (len > UINT16_MAX) {
        len = UINT16_MAX;
    }

    while (pos < len && count < max_lines) {
        size_t start = pos;
        size_t end = pos;
        int32_t width = 0;
        bool hard_break = false;

        /* Greedy: take characters until the next one would overflow. */
        while (end < len) {
            size_t next = 0;
            uint16_t cp = cyd_ui_font_codepoint_at(text, end, &next);
            if (cp == '\n') {
                hard_break = true;
                break;
            }
            int32_t advance = cyd_ui_font_codepoint_advance(font, cp);
            if (width + advance > max_px && end > start) {
                break;
            }
            width += advance;
            end = next;
        }

        size_t resume = end;
        if (hard_break) {
            resume = end + 1U;
        } else if (end < len) {
            size_t after = 0;
            uint16_t first_next = cyd_ui_font_codepoint_at(text, end, &after);
            size_t prev = cyd_ui_font_prev_start(text, end);
            size_t ignored = 0;
            uint16_t last_here = cyd_ui_font_codepoint_at(text, prev, &ignored);

            if (cyd_ui_font_is_word_char(first_next) && cyd_ui_font_is_word_char(last_here)) {
                /* Mid-word: break at the last space on the line, if any. */
                for (size_t i = end; i > start; --i) {
                    if (text[i - 1U] == ' ') {
                        end = i - 1U;
                        resume = i;
                        break;
                    }
                }
            } else if (cyd_ui_font_is_no_line_start(first_next) && prev > start) {
                /* Push the previous character down so punctuation is not orphaned. */
                end = prev;
                resume = prev;
            }
        }

        /* Nor does it end with one: a centred line would sit off centre. */
        if (!hard_break) {
            while (end > start && text[end - 1U] == ' ') {
                --end;
            }
        }

        lines[count].start = (uint16_t)start;
        lines[count].length = (uint16_t)(end - start);
        ++count;

        pos = resume;
        /* A wrapped line does not begin with the space it wrapped at. */
        while (!hard_break && pos < len && text[pos] == ' ') {
            ++pos;
        }
    }

    if (truncated != NULL && pos < len) {
        *truncated = true;
    }
    return count;
}
