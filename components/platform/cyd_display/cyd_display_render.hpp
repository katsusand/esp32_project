#ifndef CYD_DISPLAY_RENDER_HPP
#define CYD_DISPLAY_RENDER_HPP

/*
 * Widget rendering and frame diffing, free of FreeRTOS, NVS and the panel.
 *
 * cyd_display.cpp includes this to draw the device's 16px strips, and the
 * desktop simulator (tools/cyd_sim) includes the same file to draw its window.
 * Anything that decides what a pixel looks like or whether a widget is redrawn
 * belongs here, so the simulator cannot drift from the device.
 *
 * English contract: include once per translation unit, after defining
 * LGFX_USE_V1 and including <LovyanGFX.hpp>. Memory questions go through
 * cyd_display_port.h, which each side implements.
 */

#include <cstring>
#include <string>
#include "esp_log.h"
#include "cyd_display.h"
#include "cyd_display_port.h"
#include "cyd_ui_fonts.h"

extern "C" const cyd_ui_font_t *cyd_display_font_table(cyd_display_font_t font);

namespace {

static constexpr const char *RENDER_TAG = "cyd_display";
static constexpr int32_t GRID_CELL_PX = CYD_DISPLAY_GRID_CELL_PX;
static constexpr int32_t WIDGET_BUTTON_RADIUS = 8;
/* Keeps a button label off its rounded border. */
static constexpr int32_t WIDGET_BUTTON_TEXT_INSET_PX = 6;
/* Longest label the ellipsis path will shorten; longer text is cut first. */
static constexpr size_t FIT_TEXT_MAX_BYTES = 160;
static constexpr uint16_t ELLIPSIS_CODEPOINT = 0x2026;

typedef struct {
    int32_t x;
    int32_t y;
    int32_t w;
    int32_t h;
} cyd_display_dirty_rect_t;

static int32_t cyd_display_col_to_px(uint8_t col)
{
    return static_cast<int32_t>(col) * GRID_CELL_PX;
}

static int32_t cyd_display_row_to_px(uint8_t row)
{
    return static_cast<int32_t>(row) * GRID_CELL_PX;
}

static uint16_t cyd_display_resolve_bg(const cyd_display_widget_t &widget)
{
    return widget.bg_color != 0 ? widget.bg_color : TFT_BLACK;
}

static int32_t cyd_display_border_px(const cyd_display_widget_t &widget)
{
    return widget.border_width > 0 ? widget.border_width : 1;
}

static bool cyd_display_bitmap_is_usable(const cyd_display_bitmap_t *bitmap)
{
    if (!cyd_display_port_ptr_readable(bitmap)) {
        return false;
    }
    if (bitmap->width_px == 0 || bitmap->height_px == 0) {
        return false;
    }
    if (!cyd_display_port_ptr_readable(bitmap->data)) {
        return false;
    }

    /* Last pixel too: a truncated buffer only faults partway through pushImage. */
    size_t pixel_count = static_cast<size_t>(bitmap->width_px) * bitmap->height_px;
    return cyd_display_port_ptr_readable(&bitmap->data[pixel_count - 1U]);
}

template <typename TDisplay>
static void cyd_display_draw_centered_line(TDisplay &display,
                                           const std::string &text,
                                           int32_t center_x,
                                           int32_t y,
                                           uint8_t text_size,
                                           uint16_t color)
{
    display.setTextDatum(lgfx::middle_center);
    display.setTextSize(text_size);
    display.setTextColor(color, TFT_BLACK);
    display.drawString(text.c_str(), center_x, y);
}

template <typename TDisplay>
static void cyd_display_draw_left_line(TDisplay &display,
                                       const std::string &text,
                                       int32_t x,
                                       int32_t y,
                                       uint8_t text_size,
                                       uint16_t color)
{
    display.setTextDatum(lgfx::top_left);
    display.setTextSize(text_size);
    display.setTextColor(color, TFT_BLACK);
    display.drawString(text.c_str(), x, y);
}

/* ---- anti-aliased text ---------------------------------------------------- */

/* Mixes two RGB565 colours; `alpha` is the 4-bit glyph coverage (0..15). */
static uint16_t cyd_display_blend565(uint16_t fg, uint16_t bg, uint32_t alpha)
{
    uint32_t inv = 15U - alpha;
    uint32_t r = ((((fg >> 11) & 0x1FU) * alpha) + (((bg >> 11) & 0x1FU) * inv) + 7U) / 15U;
    uint32_t g = ((((fg >> 5) & 0x3FU) * alpha) + (((bg >> 5) & 0x3FU) * inv) + 7U) / 15U;
    uint32_t b = (((fg & 0x1FU) * alpha) + ((bg & 0x1FU) * inv) + 7U) / 15U;
    return static_cast<uint16_t>((r << 11) | (g << 5) | b);
}

/*
 * Draws one line of text with its line box's top-left at (x, y) and returns
 * the pen position after it. Only pixels inside the target's clip rect are
 * touched; the caller sets that to the widget box.
 */
template <typename TDisplay>
static int32_t cyd_display_draw_aa_text(TDisplay &display,
                                        const cyd_ui_font_t &font,
                                        const char *text,
                                        int32_t x,
                                        int32_t y,
                                        uint16_t color)
{
    int32_t clip_x = 0;
    int32_t clip_y = 0;
    int32_t clip_w = 0;
    int32_t clip_h = 0;
    display.getClipRect(&clip_x, &clip_y, &clip_w, &clip_h);
    int32_t clip_right = clip_x + clip_w;
    int32_t clip_bottom = clip_y + clip_h;

    while (text != nullptr && *text != '\0') {
        uint16_t codepoint = cyd_ui_font_next_codepoint(&text);
        const cyd_ui_font_glyph_t *glyph = cyd_ui_font_find_glyph(&font, codepoint);

        if (glyph == nullptr) {
            /* A visible box rather than a gap: dynamic text from outside the
               firmware can still reach a character the subset lacks. */
            int32_t advance = cyd_ui_font_codepoint_advance(&font, codepoint);
            if (codepoint != ' ' && advance > 2) {
                display.drawRect(x + 1, y + font.line_height / 8, advance - 2,
                                 (font.line_height * 3) / 4, color);
            }
            x += advance;
            continue;
        }

        int32_t gx = x + glyph->x_offset;
        int32_t gy = y + glyph->y_offset;
        const uint8_t *bits = font.bitmaps + glyph->bitmap_offset;
        for (int32_t row = 0; row < glyph->height; ++row) {
            int32_t py = gy + row;
            if (py < clip_y || py >= clip_bottom) {
                continue;
            }
            for (int32_t col = 0; col < glyph->width; ++col) {
                int32_t px = gx + col;
                if (px < clip_x || px >= clip_right) {
                    continue;
                }
                uint32_t index = static_cast<uint32_t>(row) * glyph->width + static_cast<uint32_t>(col);
                uint8_t packed = bits[index >> 1];
                uint32_t alpha = (index & 1U) ? (packed & 0x0FU) : (packed >> 4);
                if (alpha == 0) {
                    continue;
                }
                if (alpha == 15U) {
                    display.drawPixel(px, py, color);
                } else {
                    uint16_t under = display.readPixel(px, py);
                    display.drawPixel(px, py, cyd_display_blend565(color, under, alpha));
                }
            }
        }
        x += glyph->x_advance;
    }
    return x;
}

/* The next smaller face of the same kind, or nullptr when there is none. */
static const cyd_ui_font_t *cyd_display_fallback_font(cyd_display_font_t font)
{
    switch (font) {
        case CYD_DISPLAY_FONT_TITLE:
            return cyd_display_font_table(CYD_DISPLAY_FONT_BODY_BOLD);
        case CYD_DISPLAY_FONT_CLOCK_LARGE:
            return cyd_display_font_table(CYD_DISPLAY_FONT_CLOCK_MEDIUM);
        default:
            return nullptr;
    }
}

/*
 * Cuts `text` so that it plus "…" fits in `max_px`. Writes into `out`, which
 * holds FIT_TEXT_MAX_BYTES + 1 bytes. Fonts without the ellipsis glyph (the
 * digit-only clocks) are cut without one.
 */
static void cyd_display_ellipsize(const cyd_ui_font_t &font,
                                  const char *text,
                                  int32_t max_px,
                                  char *out)
{
    bool has_ellipsis = cyd_ui_font_find_glyph(&font, ELLIPSIS_CODEPOINT) != nullptr;
    int32_t budget = max_px - (has_ellipsis ? cyd_ui_font_codepoint_advance(&font, ELLIPSIS_CODEPOINT) : 0);
    size_t used = 0;
    int32_t width = 0;
    const char *cursor = text;

    while (*cursor != '\0') {
        const char *start = cursor;
        int32_t advance = cyd_ui_font_codepoint_advance(&font, cyd_ui_font_next_codepoint(&cursor));
        size_t bytes = static_cast<size_t>(cursor - start);
        if (width + advance > budget || used + bytes + 3U > FIT_TEXT_MAX_BYTES) {
            break;
        }
        memcpy(out + used, start, bytes);
        used += bytes;
        width += advance;
    }
    if (has_ellipsis) {
        memcpy(out + used, "\xE2\x80\xA6", 3U);
        used += 3U;
    }
    out[used] = '\0';
}

/*
 * Draws a TEXT or BUTTON label in an anti-aliased face, centred vertically in
 * the box, aligned horizontally, shrunk or ellipsized to fit, and clipped to
 * the box.
 */
template <typename TDisplay>
static void cyd_display_draw_widget_text(TDisplay &display,
                                         const cyd_display_widget_t &widget,
                                         int32_t x,
                                         int32_t y,
                                         int32_t w,
                                         int32_t h,
                                         int32_t inset)
{
    const cyd_ui_font_t *font = cyd_display_font_table(static_cast<cyd_display_font_t>(widget.font));
    const char *text = cyd_display_widget_text(&widget);
    if (font == nullptr || text[0] == '\0') {
        return;
    }

    int32_t available = w - (inset * 2);
    int32_t text_w = cyd_ui_font_text_width(font, text);
    if (text_w > available) {
        const cyd_ui_font_t *smaller = cyd_display_fallback_font(static_cast<cyd_display_font_t>(widget.font));
        if (smaller != nullptr) {
            font = smaller;
            text_w = cyd_ui_font_text_width(font, text);
        }
    }

    char fitted[FIT_TEXT_MAX_BYTES + 1];
    if (text_w > available) {
        cyd_display_ellipsize(*font, text, available, fitted);
        text = fitted;
        text_w = cyd_ui_font_text_width(font, text);
    }

    int32_t text_x = x + inset;
    if (widget.align == CYD_DISPLAY_ALIGN_CENTER) {
        text_x = x + (w - text_w) / 2;
    } else if (widget.align == CYD_DISPLAY_ALIGN_RIGHT) {
        text_x = x + w - inset - text_w;
    }
    int32_t text_y = y + (h - font->line_height) / 2;

    int32_t saved_x = 0;
    int32_t saved_y = 0;
    int32_t saved_w = 0;
    int32_t saved_h = 0;
    display.getClipRect(&saved_x, &saved_y, &saved_w, &saved_h);
    int32_t left = x > saved_x ? x : saved_x;
    int32_t top = y > saved_y ? y : saved_y;
    int32_t right = (x + w) < (saved_x + saved_w) ? (x + w) : (saved_x + saved_w);
    int32_t bottom = (y + h) < (saved_y + saved_h) ? (y + h) : (saved_y + saved_h);
    if (right > left && bottom > top) {
        display.setClipRect(left, top, right - left, bottom - top);
        cyd_display_draw_aa_text(display, *font, text, text_x, text_y, widget.fg_color);
    }
    display.setClipRect(saved_x, saved_y, saved_w, saved_h);
}

/* ---- shapes ----------------------------------------------------------------- */

/*
 * A filled rounded box with a border of `thickness` pixels.
 *
 * Drawn as two fills - the border colour, then the inside inset by the border
 * width - rather than as nested outlines: concentric drawRoundRect() calls
 * leave single-pixel gaps on the corners once the border is thicker than 1px.
 */
template <typename TDisplay>
static void cyd_display_fill_bordered(TDisplay &display,
                                      int32_t x,
                                      int32_t y,
                                      int32_t w,
                                      int32_t h,
                                      int32_t radius,
                                      int32_t thickness,
                                      uint16_t fill,
                                      uint16_t border)
{
    if (thickness <= 1 || w <= 2 * thickness || h <= 2 * thickness) {
        if (radius > 0) {
            display.fillRoundRect(x, y, w, h, radius, fill);
            display.drawRoundRect(x, y, w, h, radius, border);
        } else {
            display.fillRect(x, y, w, h, fill);
            display.drawRect(x, y, w, h, border);
        }
        return;
    }

    int32_t inner_radius = radius - thickness;
    if (radius > 0) {
        display.fillRoundRect(x, y, w, h, radius, border);
    } else {
        display.fillRect(x, y, w, h, border);
    }
    if (inner_radius > 0) {
        display.fillRoundRect(x + thickness, y + thickness, w - 2 * thickness, h - 2 * thickness,
                              inner_radius, fill);
    } else {
        display.fillRect(x + thickness, y + thickness, w - 2 * thickness, h - 2 * thickness, fill);
    }
}

template <typename TDisplay>
static void cyd_display_draw_round_border(TDisplay &display,
                                          int32_t x,
                                          int32_t y,
                                          int32_t w,
                                          int32_t h,
                                          int32_t radius,
                                          int32_t thickness,
                                          uint16_t color)
{
    for (int32_t i = 0; i < thickness && (w - 2 * i) > 0 && (h - 2 * i) > 0; ++i) {
        int32_t r = radius - i;
        if (r > 0) {
            display.drawRoundRect(x + i, y + i, w - 2 * i, h - 2 * i, r, color);
        } else {
            display.drawRect(x + i, y + i, w - 2 * i, h - 2 * i, color);
        }
    }
}

/*
 * Maps a value onto 0..span_px. Saturates instead of extrapolating so a sample
 * outside [min_value, max_value] cannot draw beyond the widget box.
 */
static int32_t cyd_display_map_value(int32_t value, int32_t min_value, int32_t max_value, int32_t span_px)
{
    if (max_value <= min_value || span_px <= 0) {
        return 0;
    }
    if (value <= min_value) {
        return 0;
    }
    if (value >= max_value) {
        return span_px;
    }
    return ((value - min_value) * span_px) / (max_value - min_value);
}

template <typename TDisplay>
static void cyd_display_draw_bar(TDisplay &display,
                                 const cyd_display_widget_t &widget,
                                 int32_t x,
                                 int32_t y,
                                 int32_t w,
                                 int32_t h)
{
    display.fillRect(x, y, w, h, cyd_display_resolve_bg(widget));

    if (widget.bar.vertical) {
        int32_t filled = cyd_display_map_value(widget.bar.value, widget.bar.min_value, widget.bar.max_value, h);
        if (filled > 0) {
            display.fillRect(x, y + (h - filled), w, filled, widget.fg_color);
        }
    } else {
        int32_t filled = cyd_display_map_value(widget.bar.value, widget.bar.min_value, widget.bar.max_value, w);
        if (filled > 0) {
            display.fillRect(x, y, filled, h, widget.fg_color);
        }
    }

    if (widget.border_color != 0) {
        display.drawRect(x, y, w, h, widget.border_color);
    }
}

template <typename TDisplay>
static void cyd_display_draw_sparkline(TDisplay &display,
                                       const cyd_display_widget_t &widget,
                                       int32_t x,
                                       int32_t y,
                                       int32_t w,
                                       int32_t h)
{
    const cyd_display_sparkline_t &line = widget.sparkline;

    display.fillRect(x, y, w, h, cyd_display_resolve_bg(widget));

    if (line.has_baseline) {
        int32_t baseline_y = y + (h - 1) -
                             cyd_display_map_value(line.baseline_value, line.min_value, line.max_value, h - 1);
        display.drawFastHLine(x, baseline_y, w, line.baseline_color);
    }

    if (line.samples != nullptr && !cyd_display_port_ptr_readable(line.samples)) {
        ESP_LOGW(RENDER_TAG, "sparkline samples unreadable (%p); check the lifetime contract", line.samples);
    } else if (line.samples != nullptr && line.count > 0 && w > 0 && h > 0) {
        /*
         * This runs once per strip the widget overlaps, so segments fully above
         * or below the current target are skipped rather than relying on the
         * clip inside drawLine.
         */
        int32_t target_h = display.height();
        int32_t prev_x = 0;
        int32_t prev_y = 0;
        bool has_prev = false;

        for (uint16_t i = 0; i < line.count; ++i) {
            /* A dropout breaks the polyline: the next valid sample starts a new
               segment instead of drawing a line across the missing span. */
            if (line.has_gap_value && line.samples[i] == line.gap_value) {
                has_prev = false;
                continue;
            }

            int32_t sample_x = (line.count == 1)
                                   ? x
                                   : x + ((static_cast<int32_t>(i) * (w - 1)) / (line.count - 1));
            int32_t sample_y = y + (h - 1) -
                               cyd_display_map_value(line.samples[i], line.min_value, line.max_value, h - 1);

            if (line.fill) {
                display.drawFastVLine(sample_x, sample_y, (y + h) - sample_y, widget.fg_color);
            }

            if (!has_prev) {
                display.drawPixel(sample_x, sample_y, widget.fg_color);
            } else if (!((prev_y < 0 && sample_y < 0) ||
                         (prev_y >= target_h && sample_y >= target_h))) {
                display.drawLine(prev_x, prev_y, sample_x, sample_y, widget.fg_color);
            }

            prev_x = sample_x;
            prev_y = sample_y;
            has_prev = true;
        }
    }

    if (widget.border_color != 0) {
        display.drawRect(x, y, w, h, widget.border_color);
    }
}

/* ---- widgets ---------------------------------------------------------------- */

static bool cyd_display_widget_bounds_px(const cyd_display_widget_t &widget, cyd_display_dirty_rect_t *rect)
{
    if (rect == nullptr || widget.type == CYD_DISPLAY_WIDGET_NONE) {
        return false;
    }

    rect->x = cyd_display_col_to_px(widget.col);
    rect->y = cyd_display_row_to_px(widget.row);
    rect->w = static_cast<int32_t>(widget.span_cols) * GRID_CELL_PX;
    rect->h = static_cast<int32_t>(widget.span_rows) * GRID_CELL_PX;
    return rect->w > 0 && rect->h > 0;
}

static bool cyd_display_widget_intersects_rect(const cyd_display_widget_t &widget, const cyd_display_dirty_rect_t &rect)
{
    cyd_display_dirty_rect_t widget_rect = {};
    if (!cyd_display_widget_bounds_px(widget, &widget_rect)) {
        return false;
    }

    return widget_rect.x < (rect.x + rect.w) &&
           (widget_rect.x + widget_rect.w) > rect.x &&
           widget_rect.y < (rect.y + rect.h) &&
           (widget_rect.y + widget_rect.h) > rect.y;
}

template <typename TDisplay>
static void cyd_display_render_screen_to_target(TDisplay &display,
                                                const cyd_display_screen_t &screen,
                                                int32_t origin_x,
                                                int32_t origin_y,
                                                const cyd_display_dirty_rect_t *clip_rect)
{
    auto px = [origin_x](int32_t value) { return value - origin_x; };
    auto py = [origin_y](int32_t value) { return value - origin_y; };

    for (size_t i = 0; i < screen.widget_count; ++i) {
        const cyd_display_widget_t &widget = screen.widgets[i];
        if (clip_rect != nullptr && !cyd_display_widget_intersects_rect(widget, *clip_rect)) {
            continue;
        }

        int32_t x = px(cyd_display_col_to_px(widget.col));
        int32_t y = py(cyd_display_row_to_px(widget.row));
        int32_t w = static_cast<int32_t>(widget.span_cols) * GRID_CELL_PX;
        int32_t h = static_cast<int32_t>(widget.span_rows) * GRID_CELL_PX;
        const char *text = cyd_display_widget_text(&widget);

        switch (widget.type) {
            case CYD_DISPLAY_WIDGET_TEXT:
                if (widget.font != CYD_DISPLAY_FONT_LEGACY) {
                    cyd_display_draw_widget_text(display, widget, x, y, w, h, 0);
                } else if (widget.align == CYD_DISPLAY_ALIGN_LEFT) {
                    cyd_display_draw_left_line(display,
                                               text,
                                               x,
                                               y,
                                               widget.scale_y > 0 ? widget.scale_y : 1,
                                               widget.fg_color);
                } else if (widget.align == CYD_DISPLAY_ALIGN_RIGHT) {
                    display.setTextDatum(lgfx::top_right);
                    display.setTextSize(widget.scale_y > 0 ? widget.scale_y : 1);
                    display.setTextColor(widget.fg_color, cyd_display_resolve_bg(widget));
                    display.drawString(text, x + w, y);
                } else {
                    cyd_display_draw_centered_line(display,
                                                   text,
                                                   x + (w / 2),
                                                   y + (h / 2),
                                                   widget.scale_y > 0 ? widget.scale_y : 1,
                                                   widget.fg_color);
                }
                break;

            case CYD_DISPLAY_WIDGET_BUTTON:
                cyd_display_fill_bordered(display,
                                          x,
                                          y,
                                          w,
                                          h,
                                          WIDGET_BUTTON_RADIUS,
                                          cyd_display_border_px(widget),
                                          cyd_display_resolve_bg(widget),
                                          widget.border_color != 0 ? widget.border_color : TFT_LIGHTGREY);
                if (widget.font != CYD_DISPLAY_FONT_LEGACY) {
                    cyd_display_draw_widget_text(display, widget, x, y, w, h, WIDGET_BUTTON_TEXT_INSET_PX);
                } else {
                    display.setTextDatum(lgfx::middle_center);
                    display.setTextSize(widget.scale_y > 0 ? widget.scale_y : 1);
                    display.setTextColor(widget.fg_color, cyd_display_resolve_bg(widget));
                    display.drawString(text, x + (w / 2), y + (h / 2));
                }
                break;

            case CYD_DISPLAY_WIDGET_ICON:
                if (cyd_display_bitmap_is_usable(widget.bitmap)) {
                    display.pushImage(x,
                                      y,
                                      widget.bitmap->width_px,
                                      widget.bitmap->height_px,
                                      widget.bitmap->data);
                } else if (widget.bitmap != nullptr) {
                    ESP_LOGW(RENDER_TAG, "icon bitmap unreadable (%p); check the lifetime contract", widget.bitmap);
                }
                break;

            case CYD_DISPLAY_WIDGET_RECT:
                if (widget.rect.filled && widget.border_color != 0) {
                    cyd_display_fill_bordered(display,
                                              x,
                                              y,
                                              w,
                                              h,
                                              widget.rect.radius,
                                              cyd_display_border_px(widget),
                                              cyd_display_resolve_bg(widget),
                                              widget.border_color);
                    break;
                }
                if (widget.rect.filled) {
                    if (widget.rect.radius > 0) {
                        display.fillRoundRect(x, y, w, h, widget.rect.radius, cyd_display_resolve_bg(widget));
                    } else {
                        display.fillRect(x, y, w, h, cyd_display_resolve_bg(widget));
                    }
                }
                if (widget.border_color != 0) {
                    cyd_display_draw_round_border(display,
                                                  x,
                                                  y,
                                                  w,
                                                  h,
                                                  widget.rect.radius,
                                                  cyd_display_border_px(widget),
                                                  widget.border_color);
                }
                break;

            case CYD_DISPLAY_WIDGET_BAR:
                cyd_display_draw_bar(display, widget, x, y, w, h);
                break;

            case CYD_DISPLAY_WIDGET_SPARKLINE:
                cyd_display_draw_sparkline(display, widget, x, y, w, h);
                break;

            case CYD_DISPLAY_WIDGET_NONE:
            default:
                break;
        }
    }
}

/* The enabled button covering grid cell (col, row), if any. */
static bool cyd_display_hit_test_cell(const cyd_display_screen_t &screen,
                                      uint8_t col,
                                      uint8_t row,
                                      uint16_t *action_id)
{
    size_t count = screen.widget_count < CYD_DISPLAY_MAX_WIDGETS ? screen.widget_count : CYD_DISPLAY_MAX_WIDGETS;
    for (size_t i = 0; i < count; ++i) {
        const cyd_display_widget_t &widget = screen.widgets[i];
        if (widget.type != CYD_DISPLAY_WIDGET_BUTTON || !widget.enabled) {
            continue;
        }
        if (col >= widget.col && col < (widget.col + widget.span_cols) &&
            row >= widget.row && row < (widget.row + widget.span_rows)) {
            if (action_id != nullptr) {
                *action_id = widget.action_id;
            }
            return true;
        }
    }
    return false;
}

/* ---- frame diffing ---------------------------------------------------------- */

/*
 * Payload comparison is per-type on purpose. memcmp over the union would read
 * padding bytes for the non-text variants and report spurious differences.
 */
static bool cyd_display_widget_payload_equals(const cyd_display_widget_t &lhs, const cyd_display_widget_t &rhs)
{
    switch (lhs.type) {
        case CYD_DISPLAY_WIDGET_TEXT:
        case CYD_DISPLAY_WIDGET_BUTTON:
            /* By content: a referenced literal and a copied buffer holding the
               same words draw the same pixels, and a reused buffer whose words
               changed must not compare equal just because its address did not. */
            return strcmp(cyd_display_widget_text(&lhs), cyd_display_widget_text(&rhs)) == 0;

        case CYD_DISPLAY_WIDGET_RECT:
            return lhs.rect.filled == rhs.rect.filled &&
                   lhs.rect.radius == rhs.rect.radius;

        case CYD_DISPLAY_WIDGET_BAR:
            return lhs.bar.value == rhs.bar.value &&
                   lhs.bar.min_value == rhs.bar.min_value &&
                   lhs.bar.max_value == rhs.bar.max_value &&
                   lhs.bar.vertical == rhs.bar.vertical;

        case CYD_DISPLAY_WIDGET_SPARKLINE:
            /* revision is what makes in-place sample updates visible here. */
            return lhs.sparkline.samples == rhs.sparkline.samples &&
                   lhs.sparkline.count == rhs.sparkline.count &&
                   lhs.sparkline.revision == rhs.sparkline.revision &&
                   lhs.sparkline.min_value == rhs.sparkline.min_value &&
                   lhs.sparkline.max_value == rhs.sparkline.max_value &&
                   lhs.sparkline.fill == rhs.sparkline.fill &&
                   lhs.sparkline.has_baseline == rhs.sparkline.has_baseline &&
                   lhs.sparkline.baseline_value == rhs.sparkline.baseline_value &&
                   lhs.sparkline.baseline_color == rhs.sparkline.baseline_color &&
                   lhs.sparkline.has_gap_value == rhs.sparkline.has_gap_value &&
                   lhs.sparkline.gap_value == rhs.sparkline.gap_value;

        default:
            return memcmp(lhs.text, rhs.text, sizeof(lhs.text)) == 0;
    }
}

static bool cyd_display_widget_equals(const cyd_display_widget_t &lhs, const cyd_display_widget_t &rhs)
{
    return lhs.type == rhs.type &&
           lhs.col == rhs.col &&
           lhs.row == rhs.row &&
           lhs.span_cols == rhs.span_cols &&
           lhs.span_rows == rhs.span_rows &&
           lhs.align == rhs.align &&
           lhs.scale_x == rhs.scale_x &&
           lhs.scale_y == rhs.scale_y &&
           lhs.fg_color == rhs.fg_color &&
           lhs.bg_color == rhs.bg_color &&
           lhs.border_color == rhs.border_color &&
           lhs.action_id == rhs.action_id &&
           lhs.enabled == rhs.enabled &&
           lhs.font == rhs.font &&
           lhs.border_width == rhs.border_width &&
           lhs.bitmap == rhs.bitmap &&
           cyd_display_widget_payload_equals(lhs, rhs);
}

/*
 * Appends one dirty rect. When the list is full the whole panel becomes the
 * single dirty rect: redrawing too much is merely slow, missing a change is a
 * stale screen.
 */
static void cyd_display_append_dirty_rect(cyd_display_dirty_rect_t *rects,
                                          size_t *rect_count,
                                          size_t max_rects,
                                          int32_t panel_w,
                                          int32_t panel_h,
                                          const cyd_display_dirty_rect_t &rect)
{
    if (rects == nullptr || rect_count == nullptr || rect.w <= 0 || rect.h <= 0) {
        return;
    }

    if (*rect_count >= max_rects) {
        rects[0] = {
            .x = 0,
            .y = 0,
            .w = panel_w,
            .h = panel_h,
        };
        *rect_count = 1;
        return;
    }

    rects[*rect_count] = rect;
    ++(*rect_count);
}

/* Rects that differ between `previous` (if any) and `current`. */
static void cyd_display_collect_dirty_rects(const cyd_display_screen_t &previous,
                                            bool has_previous,
                                            const cyd_display_screen_t &current,
                                            int32_t panel_w,
                                            int32_t panel_h,
                                            cyd_display_dirty_rect_t *rects,
                                            size_t max_rects,
                                            size_t *rect_count)
{
    if (rect_count == nullptr) {
        return;
    }

    *rect_count = 0;

    if (!has_previous) {
        cyd_display_append_dirty_rect(rects, rect_count, max_rects, panel_w, panel_h,
                                      {
                                          .x = 0,
                                          .y = 0,
                                          .w = panel_w,
                                          .h = panel_h,
                                      });
        return;
    }

    size_t max_widgets = current.widget_count > previous.widget_count
                             ? current.widget_count
                             : previous.widget_count;

    for (size_t i = 0; i < max_widgets; ++i) {
        const cyd_display_widget_t *current_widget = i < current.widget_count ? &current.widgets[i] : nullptr;
        const cyd_display_widget_t *previous_widget = i < previous.widget_count ? &previous.widgets[i] : nullptr;

        if (current_widget != nullptr && previous_widget != nullptr &&
            cyd_display_widget_equals(*current_widget, *previous_widget)) {
            continue;
        }

        cyd_display_dirty_rect_t rect = {};
        if (previous_widget != nullptr && cyd_display_widget_bounds_px(*previous_widget, &rect)) {
            cyd_display_append_dirty_rect(rects, rect_count, max_rects, panel_w, panel_h, rect);
        }
        if (current_widget != nullptr && cyd_display_widget_bounds_px(*current_widget, &rect)) {
            cyd_display_append_dirty_rect(rects, rect_count, max_rects, panel_w, panel_h, rect);
        }
    }
}

/* Marks which `strip_height`-tall strips any of `rects` touches. */
static void cyd_display_mark_dirty_strips(bool *dirty_strips,
                                          size_t strip_count,
                                          int32_t strip_height,
                                          const cyd_display_dirty_rect_t *rects,
                                          size_t rect_count)
{
    if (dirty_strips == nullptr) {
        return;
    }

    memset(dirty_strips, 0, sizeof(bool) * strip_count);
    if (rects == nullptr) {
        return;
    }

    for (size_t i = 0; i < rect_count; ++i) {
        const cyd_display_dirty_rect_t &rect = rects[i];
        if (rect.w <= 0 || rect.h <= 0) {
            continue;
        }

        int32_t start_strip = rect.y / strip_height;
        int32_t end_strip = (rect.y + rect.h - 1) / strip_height;
        if (start_strip < 0) {
            start_strip = 0;
        }
        if (end_strip >= static_cast<int32_t>(strip_count)) {
            end_strip = static_cast<int32_t>(strip_count) - 1;
        }

        for (int32_t strip = start_strip; strip <= end_strip; ++strip) {
            dirty_strips[strip] = true;
        }
    }
}

}  // namespace

#endif
