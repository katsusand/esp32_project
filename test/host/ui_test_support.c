#include <stdio.h>
#include "cyd_display_port.h"
#include "cyd_ui_fonts.h"
#include "ui_test_support.h"

int g_log_quiet = 1;

static int s_failures = 0;
static int s_checks = 0;
static bool s_all_immutable = false;

bool cyd_display_port_ptr_readable(const void *p)
{
    return p != NULL;
}

bool cyd_display_port_text_is_immutable(const char *p)
{
    (void)p;
    return s_all_immutable;
}

esp_err_t cyd_display_submit_screen(const cyd_display_screen_t *screen)
{
    (void)screen;
    return ESP_OK;
}

const cyd_ui_font_t *cyd_display_font_table(cyd_display_font_t font);

void ui_test_check(bool ok, const char *what)
{
    s_checks++;
    if (ok) {
        printf("  ok   %s\n", what);
    } else {
        printf("  FAIL %s\n", what);
        s_failures++;
    }
}

void ui_test_fail(void)
{
    s_checks++;
    s_failures++;
}

void ui_test_set_all_text_immutable(bool immutable)
{
    s_all_immutable = immutable;
}

static int32_t inset_for(const cyd_display_widget_t *w)
{
    /* WIDGET_BUTTON_TEXT_INSET_PX in cyd_display_render.hpp. */
    return w->type == CYD_DISPLAY_WIDGET_BUTTON ? 6 : 0;
}

void ui_test_check_screen(const char *name, const cyd_display_screen_t *screen)
{
    char what[160];
    bool all_fit = true;

    snprintf(what, sizeof(what), "%s: %u widgets fit the screen buffer", name, (unsigned)screen->widget_count);
    ui_test_check(screen->widget_count < CYD_DISPLAY_MAX_WIDGETS, what);

    for (size_t i = 0; i < screen->widget_count; ++i) {
        const cyd_display_widget_t *w = &screen->widgets[i];
        if ((w->type != CYD_DISPLAY_WIDGET_TEXT && w->type != CYD_DISPLAY_WIDGET_BUTTON) ||
            w->font == CYD_DISPLAY_FONT_LEGACY) {
            continue;
        }
        const char *text = cyd_display_widget_text(w);
        const cyd_ui_font_t *font = cyd_display_font_table((cyd_display_font_t)w->font);
        int32_t box = (int32_t)w->span_cols * CYD_DISPLAY_GRID_CELL_PX - 2 * inset_for(w);
        int32_t width = cyd_ui_font_text_width(font, text);
        if (width > box) {
            printf("  FAIL %s: \"%s\" is %ldpx in a %ldpx box\n", name, text, (long)width, (long)box);
            all_fit = false;
        }
        int32_t height = (int32_t)w->span_rows * CYD_DISPLAY_GRID_CELL_PX;
        if (font->line_height > height) {
            printf("  FAIL %s: \"%s\" box is shorter than its line\n", name, text);
            all_fit = false;
        }
    }
    snprintf(what, sizeof(what), "%s: every label fits without shrinking", name);
    ui_test_check(all_fit, what);
}

int ui_test_finish(void)
{
    printf("%d/%d checks passed\n", s_checks - s_failures, s_checks);
    return s_failures == 0 ? 0 : 1;
}
