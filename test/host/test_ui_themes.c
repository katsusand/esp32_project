/*
 * Colour themes: every text/background pair of every theme meets its
 * contrast ratio after rounding to RGB565, and every theme name fits the
 * settings stepper.
 *
 * Contrast uses the WCAG 2.x relative-luminance formula. Targets: 7:1 for
 * text in the high-contrast theme, 4.5:1 for text in the others, 3:1 for
 * borders and other non-text marks, and for disabled text outside the
 * high-contrast theme (WCAG exempts it; we keep it legible anyway).
 */

#include <math.h>
#include <stdio.h>
#include <stddef.h>
#include "cyd_display.h"
#include "cyd_ui.h"
#include "ui_test_support.h"

static double channel(unsigned v8)
{
    double c = v8 / 255.0;
    return c <= 0.03928 ? c / 12.92 : pow((c + 0.055) / 1.055, 2.4);
}

static double luminance(uint16_t rgb565)
{
    unsigned r = (rgb565 >> 11) & 0x1f;
    unsigned g = (rgb565 >> 5) & 0x3f;
    unsigned b = rgb565 & 0x1f;
    return 0.2126 * channel((r << 3) | (r >> 2)) + 0.7152 * channel((g << 2) | (g >> 4)) +
           0.0722 * channel((b << 3) | (b >> 2));
}

static double contrast(uint16_t a, uint16_t b)
{
    double la = luminance(a);
    double lb = luminance(b);
    return la > lb ? (la + 0.05) / (lb + 0.05) : (lb + 0.05) / (la + 0.05);
}

typedef enum { KIND_TEXT, KIND_LINE, KIND_DISABLED } kind_t;

typedef struct {
    const char *what;
    size_t fg;
    size_t bg;
    kind_t kind;
} pair_t;

#define F(field) offsetof(cyd_ui_theme_t, field)

static const pair_t k_pairs[] = {
    { "text on bg", F(text), F(bg), KIND_TEXT },
    { "text on surface", F(text), F(surface), KIND_TEXT },
    { "subtext on bg", F(subtext), F(bg), KIND_TEXT },
    { "subtext on surface", F(subtext), F(surface), KIND_TEXT },
    { "disabled on surface", F(disabled), F(surface), KIND_DISABLED },
    { "line on bg", F(line), F(bg), KIND_LINE },
    { "line on surface", F(line), F(surface), KIND_LINE },
    { "success_soft on bg", F(success_soft), F(bg), KIND_TEXT },
    { "success_soft on surface", F(success_soft), F(surface), KIND_TEXT },
    { "on_success on success", F(on_success), F(success), KIND_TEXT },
    { "primary_soft on bg", F(primary_soft), F(bg), KIND_TEXT },
    { "primary_soft on surface", F(primary_soft), F(surface), KIND_TEXT },
    { "on_primary on primary", F(on_primary), F(primary), KIND_TEXT },
    { "primary on bg", F(primary), F(bg), KIND_LINE },
    { "warning on bg", F(warning), F(bg), KIND_TEXT },
    { "warning on warning_tint", F(warning), F(warning_tint), KIND_TEXT },
    { "on_warning on warning", F(on_warning), F(warning), KIND_TEXT },
    { "info on bg", F(info), F(bg), KIND_TEXT },
    { "info on info_tint", F(info), F(info_tint), KIND_TEXT },
    { "on_info on info", F(on_info), F(info), KIND_TEXT },
    { "danger_soft on bg", F(danger_soft), F(bg), KIND_TEXT },
    { "danger_soft on danger_tint", F(danger_soft), F(danger_tint), KIND_TEXT },
    { "on_danger on danger", F(on_danger), F(danger), KIND_TEXT },
    { "danger on bg", F(danger), F(bg), KIND_LINE },
};

static uint16_t field(const cyd_ui_theme_t *t, size_t offset)
{
    return *(const uint16_t *)((const char *)t + offset);
}

int main(void)
{
    char what[128];

    for (int id = 0; id < CYD_UI_THEME_ID_COUNT; ++id) {
        const cyd_ui_theme_t *t = cyd_ui_theme_colors((cyd_ui_theme_id_t)id);
        const int high = id == CYD_UI_THEME_ID_HIGH_CONTRAST;

        printf("%s\n", cyd_ui_theme_name((cyd_ui_theme_id_t)id));
        for (size_t i = 0; i < sizeof(k_pairs) / sizeof(k_pairs[0]); ++i) {
            double need = k_pairs[i].kind == KIND_LINE ? 3.0
                        : k_pairs[i].kind == KIND_DISABLED ? (high ? 4.5 : 3.0)
                        : (high ? 7.0 : 4.5);
            double got = contrast(field(t, k_pairs[i].fg), field(t, k_pairs[i].bg));
            snprintf(what, sizeof(what), "%s: %.2f:1 (needs %.1f)", k_pairs[i].what, got, need);
            ui_test_check(got >= need, what);
        }
        /* A disabled button must not look like an enabled one. */
        ui_test_check(t->disabled != t->text, "disabled text differs from enabled text");

        int32_t width = cyd_display_text_width(CYD_DISPLAY_FONT_BODY_BOLD, cyd_ui_theme_name((cyd_ui_theme_id_t)id));
        snprintf(what, sizeof(what), "the name fits the 120px stepper value (%ldpx)", (long)width);
        ui_test_check(width <= 120, what);
    }

    printf("selection\n");
    ui_test_check(cyd_ui_theme_id() == CYD_UI_THEME_ID_DEFAULT && CYD_UI_THEME_ID_DEFAULT == CYD_UI_THEME_ID_HIGH_CONTRAST,
                  "the default is the high-contrast theme");
    cyd_ui_set_theme(CYD_UI_THEME_ID_LIGHT);
    ui_test_check(CYD_UI_THEME_BG == cyd_ui_theme_colors(CYD_UI_THEME_ID_LIGHT)->bg,
                  "the colour names follow the selected theme");
    cyd_ui_set_theme((cyd_ui_theme_id_t)99);
    ui_test_check(cyd_ui_theme_id() == CYD_UI_THEME_ID_DEFAULT, "an unknown id selects the default");
    return ui_test_finish();
}
