#include <stddef.h>
#include "sd_card_status_icon_art.h"

/*
 * Symbols:  .  background        O  outline        B  card body
 *           G  contacts          K  the sign       R  the strike-through
 *           W  white (the filled part of the gauge)
 *
 * The card is 12 pixels wide and 14 tall with its top-right corner cut, as an
 * SD card is. It sits in a 16x16 block because that is two grid cells
 * (8 pixels each) square, and the block is drawn opaque on a black screen.
 */

/* not inserted, or not answering: a grey card struck through */
static const char *const s_art_no_card[SD_CARD_STATUS_ICON_SIZE_PX] = {
    "................",
    "..OOOOOOOOO.....",
    "..RRBBBBBBBO....",
    "..ORRBBBBBBBO...",
    "..OBRRBBBBBBBO..",
    "..OBBRRBBBBBBO..",
    "..OBBBRRBBBBBO..",
    "..OBBBBRRBBBBO..",
    "..OBBBBBRRBBBO..",
    "..OBBBBBBRRBBO..",
    "..OBBBBBBBRRBO..",
    "..OBBBBBBBBRRO..",
    "..OBGBGBGBGBRR..",
    "..OBGBGBGBGBBO..",
    "..OOOOOOOOOOOO..",
    "................",
};

/* inserted but not formatted (or not FAT): a yellow card with a question mark */
static const char *const s_art_no_filesystem[SD_CARD_STATUS_ICON_SIZE_PX] = {
    "................",
    "..OOOOOOOOO.....",
    "..OBBBBBBBBO....",
    "..OBBBKKKKBBO...",
    "..OBBKKBBKKBBO..",
    "..OBBBBBBKKBBO..",
    "..OBBBBBKKBBBO..",
    "..OBBBBKKBBBBO..",
    "..OBBBBKKBBBBO..",
    "..OBBBBBBBBBBO..",
    "..OBBBBKKBBBBO..",
    "..OBBBBBBBBBBO..",
    "..OBGBGBGBGBBO..",
    "..OBGBGBGBGBBO..",
    "..OOOOOOOOOOOO..",
    "................",
};

/* no free space left: an orange card with a completely filled gauge */
static const char *const s_art_full[SD_CARD_STATUS_ICON_SIZE_PX] = {
    "................",
    "..OOOOOOOOO.....",
    "..OBBBBBBBBO....",
    "..OBBBBBBBBBO...",
    "..OBBBBBBBBBBO..",
    "..OBKKKKKKKKBO..",
    "..OBKWWWWWWKBO..",
    "..OBKWWWWWWKBO..",
    "..OBKKKKKKKKBO..",
    "..OBBBBBBBBBBO..",
    "..OBBBBBBBBBBO..",
    "..OBBBBBBBBBBO..",
    "..OBGBGBGBGBBO..",
    "..OBGBGBGBGBBO..",
    "..OOOOOOOOOOOO..",
    "................",
};

/* would not take a write, or anything else: a red card with an exclamation mark */
static const char *const s_art_fault[SD_CARD_STATUS_ICON_SIZE_PX] = {
    "................",
    "..OOOOOOOOO.....",
    "..OBBBBBBBBO....",
    "..OBBBBKKBBBO...",
    "..OBBBBKKBBBBO..",
    "..OBBBBKKBBBBO..",
    "..OBBBBKKBBBBO..",
    "..OBBBBKKBBBBO..",
    "..OBBBBBBBBBBO..",
    "..OBBBBKKBBBBO..",
    "..OBBBBKKBBBBO..",
    "..OBBBBBBBBBBO..",
    "..OBGBGBGBGBBO..",
    "..OBGBGBGBGBBO..",
    "..OOOOOOOOOOOO..",
    "................",
};

static const char *const *const s_art[SD_CARD_STATUS_ICON_KIND_COUNT] = {
    [SD_CARD_STATUS_ICON_NO_CARD] = s_art_no_card,
    [SD_CARD_STATUS_ICON_NO_FILESYSTEM] = s_art_no_filesystem,
    [SD_CARD_STATUS_ICON_FULL] = s_art_full,
    [SD_CARD_STATUS_ICON_FAULT] = s_art_fault,
};

/* RGB565, the same values as CYD_UI_COLOR_* where one exists. */
#define COLOR_BLACK 0x0000U
#define COLOR_WHITE 0xffffU
#define COLOR_RED 0xf800U
#define COLOR_YELLOW 0xffe0U
#define COLOR_ORANGE 0xfd20U
#define COLOR_DIMGREY 0x39e7U
#define COLOR_LIGHTGREY 0xc618U
#define COLOR_GOLD 0xfea0U

typedef struct {
    uint16_t body;
    uint16_t sign;
} sd_card_status_icon_palette_t;

static const sd_card_status_icon_palette_t s_palette[SD_CARD_STATUS_ICON_KIND_COUNT] = {
    [SD_CARD_STATUS_ICON_NO_CARD] = { .body = COLOR_DIMGREY, .sign = COLOR_BLACK },
    [SD_CARD_STATUS_ICON_NO_FILESYSTEM] = { .body = COLOR_YELLOW, .sign = COLOR_BLACK },
    [SD_CARD_STATUS_ICON_FULL] = { .body = COLOR_ORANGE, .sign = COLOR_BLACK },
    [SD_CARD_STATUS_ICON_FAULT] = { .body = COLOR_RED, .sign = COLOR_WHITE },
};

static bool symbol_color(sd_card_status_icon_kind_t kind, char symbol, uint16_t *color)
{
    switch (symbol) {
    case '.':
        *color = COLOR_BLACK;
        return true;
    case 'O':
        *color = COLOR_LIGHTGREY;
        return true;
    case 'B':
        *color = s_palette[kind].body;
        return true;
    case 'G':
        *color = COLOR_GOLD;
        return true;
    case 'K':
        *color = s_palette[kind].sign;
        return true;
    case 'R':
        *color = COLOR_RED;
        return true;
    case 'W':
        *color = COLOR_WHITE;
        return true;
    default:
        return false;
    }
}

const char *const *sd_card_status_icon_art_rows(sd_card_status_icon_kind_t kind)
{
    if ((int)kind < 0 || kind >= SD_CARD_STATUS_ICON_KIND_COUNT) {
        return NULL;
    }
    return s_art[kind];
}

bool sd_card_status_icon_art_render(sd_card_status_icon_kind_t kind, uint16_t *pixels)
{
    const char *const *rows = sd_card_status_icon_art_rows(kind);

    if (rows == NULL || pixels == NULL) {
        return false;
    }
    for (size_t y = 0; y < SD_CARD_STATUS_ICON_SIZE_PX; ++y) {
        const char *row = rows[y];
        for (size_t x = 0; x < SD_CARD_STATUS_ICON_SIZE_PX; ++x) {
            /* A short row ends in '\0', which has no colour and is caught below. */
            if (!symbol_color(kind, row[x], &pixels[y * SD_CARD_STATUS_ICON_SIZE_PX + x])) {
                return false;
            }
        }
        if (row[SD_CARD_STATUS_ICON_SIZE_PX] != '\0') {
            return false;
        }
    }
    return true;
}
