/*
 * SD card status icons.
 *
 * The art is typed in as strings, so a slipped character is easy to make and
 * hard to see in review: one short row shifts the whole icon, one wrong letter
 * paints a stray pixel in a colour that was never meant to be there. These
 * checks make such a slip fail here instead of showing on a unit.
 *
 * With --dump the rendered pixels are printed as hex, one icon after another, so
 * they can be turned into a picture and looked at.
 */

#include <stdio.h>
#include <string.h>
#include "sd_card_status_icon_art.h"

int g_log_quiet = 1;

static int checks = 0;
static int failures = 0;

static void check(bool ok, const char *what)
{
    checks++;
    if (!ok) {
        printf("  FAIL %s\n", what);
        failures++;
    } else {
        printf("  ok   %s\n", what);
    }
}

#define PIXELS (SD_CARD_STATUS_ICON_SIZE_PX * SD_CARD_STATUS_ICON_SIZE_PX)

static size_t count_non_black(const uint16_t *pixels)
{
    size_t count = 0;

    for (size_t i = 0; i < PIXELS; ++i) {
        count += pixels[i] != 0 ? 1U : 0U;
    }
    return count;
}

static void test_every_icon_renders(void)
{
    for (int kind = 0; kind < SD_CARD_STATUS_ICON_KIND_COUNT; ++kind) {
        uint16_t pixels[PIXELS];
        char what[64];

        snprintf(what, sizeof(what), "kind %d renders (16 rows of 16 known symbols)", kind);
        check(sd_card_status_icon_art_render((sd_card_status_icon_kind_t)kind, pixels), what);
    }
}

static void test_icons_fit_and_are_distinct(void)
{
    uint16_t all[SD_CARD_STATUS_ICON_KIND_COUNT][PIXELS];

    for (int kind = 0; kind < SD_CARD_STATUS_ICON_KIND_COUNT; ++kind) {
        char what[64];

        (void)sd_card_status_icon_art_render((sd_card_status_icon_kind_t)kind, all[kind]);

        /* The block is drawn opaque on black, so the border must be black or the
           icon would show a box around itself. */
        bool border_clear = true;
        for (size_t i = 0; i < SD_CARD_STATUS_ICON_SIZE_PX; ++i) {
            border_clear = border_clear &&
                           all[kind][i] == 0 &&
                           all[kind][(SD_CARD_STATUS_ICON_SIZE_PX - 1U) * SD_CARD_STATUS_ICON_SIZE_PX + i] == 0;
        }
        snprintf(what, sizeof(what), "kind %d keeps its top and bottom row clear", kind);
        check(border_clear, what);

        snprintf(what, sizeof(what), "kind %d draws something substantial", kind);
        check(count_non_black(all[kind]) > 100U, what);
    }

    for (int a = 0; a < SD_CARD_STATUS_ICON_KIND_COUNT; ++a) {
        for (int b = a + 1; b < SD_CARD_STATUS_ICON_KIND_COUNT; ++b) {
            char what[64];

            snprintf(what, sizeof(what), "kinds %d and %d look different", a, b);
            check(memcmp(all[a], all[b], sizeof(all[a])) != 0, what);
        }
    }
}

static void test_bad_arguments(void)
{
    uint16_t pixels[PIXELS];

    check(sd_card_status_icon_art_rows(SD_CARD_STATUS_ICON_KIND_COUNT) == NULL, "kind past the end has no art");
    check(sd_card_status_icon_art_rows((sd_card_status_icon_kind_t)-1) == NULL, "negative kind has no art");
    check(!sd_card_status_icon_art_render(SD_CARD_STATUS_ICON_KIND_COUNT, pixels), "kind past the end does not render");
    check(!sd_card_status_icon_art_render(SD_CARD_STATUS_ICON_FAULT, NULL), "no output buffer does not render");
}

static void test_art_text_shape(void)
{
    /* The renderer already rejects these; this pins the same rule on the source
       strings so the failure message names the row. */
    for (int kind = 0; kind < SD_CARD_STATUS_ICON_KIND_COUNT; ++kind) {
        const char *const *rows = sd_card_status_icon_art_rows((sd_card_status_icon_kind_t)kind);
        bool shaped = rows != NULL;

        for (size_t y = 0; shaped && y < SD_CARD_STATUS_ICON_SIZE_PX; ++y) {
            shaped = rows[y] != NULL && strlen(rows[y]) == SD_CARD_STATUS_ICON_SIZE_PX;
        }
        char what[64];
        snprintf(what, sizeof(what), "kind %d art is 16 rows of 16 characters", kind);
        check(shaped, what);
    }
}

int main(int argc, char **argv)
{
    if (argc > 1 && strcmp(argv[1], "--dump") == 0) {
        for (int kind = 0; kind < SD_CARD_STATUS_ICON_KIND_COUNT; ++kind) {
            uint16_t pixels[PIXELS];

            if (!sd_card_status_icon_art_render((sd_card_status_icon_kind_t)kind, pixels)) {
                return 1;
            }
            for (size_t i = 0; i < PIXELS; ++i) {
                printf("%04x%c", pixels[i], (i + 1U) % SD_CARD_STATUS_ICON_SIZE_PX == 0 ? '\n' : ' ');
            }
        }
        return 0;
    }

    test_every_icon_renders();
    test_icons_fit_and_are_distinct();
    test_bad_arguments();
    test_art_text_shape();

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
