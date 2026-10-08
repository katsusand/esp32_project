#include <string.h>
#include "cyd_display.h"
#include "cyd_display_port.h"
#include "cyd_ui_fonts.h"

/* Declared for the renderer in cyd_display_render.hpp. */
const cyd_ui_font_t *cyd_display_font_table(cyd_display_font_t font);

const cyd_ui_font_t *cyd_display_font_table(cyd_display_font_t font)
{
    switch (font) {
    case CYD_DISPLAY_FONT_BODY:
        return &cyd_ui_font_body;
    case CYD_DISPLAY_FONT_BODY_BOLD:
        return &cyd_ui_font_body_bold;
    case CYD_DISPLAY_FONT_TITLE:
        return &cyd_ui_font_title;
    case CYD_DISPLAY_FONT_CLOCK_MEDIUM:
        return &cyd_ui_font_clock_medium;
    case CYD_DISPLAY_FONT_CLOCK_LARGE:
        return &cyd_ui_font_clock_large;
    case CYD_DISPLAY_FONT_LEGACY:
    default:
        return NULL;
    }
}

bool cyd_display_utf8_copy(char *dst, size_t dst_size, const char *src)
{
    size_t len = 0;

    if (dst == NULL || dst_size == 0) {
        return false;
    }
    if (src == NULL) {
        dst[0] = '\0';
        return true;
    }

    len = strlen(src);
    if (len < dst_size) {
        memcpy(dst, src, len + 1U);
        return true;
    }

    /* Back up over continuation bytes so the cut lands on a character start. */
    len = dst_size - 1U;
    while (len > 0 && (((unsigned char)src[len]) & 0xC0) == 0x80) {
        --len;
    }
    memcpy(dst, src, len);
    dst[len] = '\0';
    return false;
}

void cyd_display_widget_set_text(cyd_display_widget_t *widget, const char *text)
{
    if (widget == NULL) {
        return;
    }

    widget->text_ref = NULL;
    if (text != NULL && cyd_display_port_text_is_immutable(text)) {
        widget->text_ref = text;
        widget->text[0] = '\0';
        return;
    }
    (void)cyd_display_utf8_copy(widget->text, sizeof(widget->text), text);
}

void cyd_display_widget_set_text_pinned(cyd_display_widget_t *widget, const char *text)
{
    if (widget == NULL) {
        return;
    }
    widget->text[0] = '\0';
    widget->text_ref = (text != NULL && cyd_display_port_ptr_readable(text)) ? text : NULL;
}

const char *cyd_display_widget_text(const cyd_display_widget_t *widget)
{
    if (widget == NULL) {
        return "";
    }
    if (widget->text_ref != NULL) {
        return widget->text_ref;
    }
    return widget->text;
}

int32_t cyd_display_text_width(cyd_display_font_t font, const char *text)
{
    return cyd_ui_font_text_width(cyd_display_font_table(font), text);
}
