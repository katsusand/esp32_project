/*
 * Japanese UI text: UTF-8 handling, the reference-or-copy rule, measuring and
 * wrapping with the real font tables.
 *
 * Layout checks of individual screens live in their own test files and use
 * ui_test_check_screen() from ui_test_support.h.
 */

#include <stdio.h>
#include <string.h>
#include "cyd_display.h"
#include "cyd_ui_fonts.h"
#include "ui_test_support.h"

static void test_utf8_copy(void)
{
    char buf[CYD_DISPLAY_TEXT_MAX_LEN + 1];
    /* 14 kana = 42 bytes: one character more than the buffer holds. */
    const char *kana = "あいうえおかきくけこさしすせ";
    bool fit = cyd_display_utf8_copy(buf, sizeof(buf), kana);

    ui_test_check(!fit, "a 42-byte string reports that it did not fit");
    ui_test_check(strlen(buf) == 39U, "the cut lands after 13 whole characters (39 bytes)");
    ui_test_check(strncmp(buf, kana, 39U) == 0, "the kept part is unchanged");

    fit = cyd_display_utf8_copy(buf, sizeof(buf), "0123456789012345678901234567890123456789");
    ui_test_check(fit && strlen(buf) == 40U, "exactly 40 ASCII bytes fit");

    fit = cyd_display_utf8_copy(buf, sizeof(buf), NULL);
    ui_test_check(fit && buf[0] == '\0', "NULL copies as empty");
}

static void test_set_text(void)
{
    cyd_display_widget_t widget = { 0 };
    const char *long_label = "しばらくしてから再度お試しください";

    ui_test_set_all_text_immutable(true);
    cyd_display_widget_set_text(&widget, long_label);
    ui_test_check(widget.text_ref == long_label, "immutable text is referenced, not copied");
    ui_test_check(strcmp(cyd_display_widget_text(&widget), long_label) == 0, "and is read back whole");

    ui_test_set_all_text_immutable(false);
    cyd_display_widget_set_text(&widget, long_label);
    ui_test_check(widget.text_ref == NULL, "mutable text is never referenced");
    ui_test_check(strlen(cyd_display_widget_text(&widget)) == 39U, "and is copied, cut on a character boundary");
}

static void test_decode(void)
{
    const char *p = "あA";
    ui_test_check(cyd_ui_font_next_codepoint(&p) == 0x3042 && *p == 'A', "3-byte sequence decodes and advances");

    const char truncated[] = { (char)0xE3, (char)0x81, 0 };
    p = truncated;
    uint16_t cp = cyd_ui_font_next_codepoint(&p);
    ui_test_check(cp == 0xFFFD && *p == '\0', "a truncated sequence yields U+FFFD and stops at the NUL");

    p = "\xF0\x9F\x98\x80!";
    cp = cyd_ui_font_next_codepoint(&p);
    ui_test_check(cp == 0xFFFD && *p == '!', "a code point beyond the BMP yields U+FFFD and is skipped whole");

    p = "\x80x";
    cp = cyd_ui_font_next_codepoint(&p);
    ui_test_check(cp == 0xFFFD && *p == 'x', "a stray continuation byte is consumed alone");
}

static void test_width(void)
{
    const cyd_ui_font_t *body = &cyd_ui_font_body;
    int32_t a = cyd_ui_font_codepoint_advance(body, 'A');

    ui_test_check(a > 0 && cyd_ui_font_text_width(body, "AA") == 2 * a, "width is the sum of advances");
    ui_test_check(cyd_ui_font_text_width(body, "\xE9\xBE\x98") == cyd_ui_font_codepoint_advance(body, ' '),
          "a character outside the subset counts as a space");
    ui_test_check(cyd_ui_font_find_glyph(body, 0x3000) != NULL, "the full-width space is in the text faces");
    ui_test_check(cyd_ui_font_find_glyph(&cyd_ui_font_clock_large, 0x2713) != NULL, "the clock face carries the check mark");
}

/* Wraps `text` at `max_px` and compares the lines with `want` ("|" separated). */
static void expect_wrap(const char *text, int32_t max_px, size_t max_lines, const char *want,
                        bool want_truncated, const char *why)
{
    cyd_ui_font_line_t lines[4];
    bool truncated = false;
    char got[256] = { 0 };
    size_t count = cyd_ui_font_wrap(&cyd_ui_font_body, text, max_px, lines, max_lines, &truncated);

    for (size_t i = 0; i < count; ++i) {
        if (i > 0) {
            strcat(got, "|");
        }
        strncat(got, text + lines[i].start, lines[i].length);
    }
    if (strcmp(got, want) == 0 && truncated == want_truncated) {
        ui_test_check(true, why);
    } else {
        printf("  FAIL %s: got \"%s\"%s, want \"%s\"%s\n", why, got, truncated ? " (truncated)" : "",
               want, want_truncated ? " (truncated)" : "");
        ui_test_fail();
    }
}

static void test_wrap(void)
{
    /* Every kana and kanji in the 16px face is 16px wide, so 64px is 4 characters. */
    expect_wrap("あいうえおかきく", 64, 2, "あいうえ|おかきく", false, "kana break every 4 characters");
    expect_wrap("あいうえおかきくけ", 64, 2, "あいうえ|おかきく", true, "text beyond the last line is reported");
    expect_wrap("あいう、えお", 48, 3, "あい|う、え|お", false, "a comma never starts a line: the character before it moves down");
    expect_wrap("あいう」えお", 48, 3, "あい|う」え|お", false, "neither does a closing bracket");
    expect_wrap("あいうっえ", 48, 2, "あい|うっえ", false, "nor a small tsu");
    expect_wrap("あ\nい", 64, 2, "あ|い", false, "a newline always breaks");
    expect_wrap("\nあ", 64, 2, "|あ", false, "a leading newline gives an empty first line");
    expect_wrap("", 64, 2, "", false, "empty text is no lines");

    int32_t abc = cyd_ui_font_text_width(&cyd_ui_font_body, "abc ");
    int32_t abc_defg = cyd_ui_font_text_width(&cyd_ui_font_body, "abc defg");
    expect_wrap("abc defgh", abc_defg, 2, "abc|defgh", false, "a word moves down whole at a space");
    expect_wrap("abc  def", abc, 2, "abc|def", false, "spaces at either end of a wrapped line are dropped");
}

int main(void)
{
    printf("UTF-8 copy\n");
    test_utf8_copy();
    printf("reference or copy\n");
    test_set_text();
    printf("decoding\n");
    test_decode();
    printf("measuring\n");
    test_width();
    printf("wrapping\n");
    test_wrap();

    return ui_test_finish();
}
