/*
 * The simulator's side of cyd_display_port.h, plus the few device entry points
 * that the shared sources call.
 */
#include <cstdio>
#include <cstring>
#include "cyd_display.h"
#include "cyd_display_port.h"

#if defined(__APPLE__)
#include <mach-o/getsect.h>
#include <mach-o/ldsyms.h>
#endif

extern "C" {

bool cyd_display_port_ptr_readable(const void *p)
{
    return p != nullptr;
}

/*
 * The executable's __TEXT segment is the host counterpart of the ESP32's flash
 * rodata: string literals (__cstring) and `static const` arrays (__const) live
 * there, while stack, heap and writable statics never do. Matching the device
 * here matters, because it decides which labels may exceed the copy buffer.
 */
bool cyd_display_port_text_is_immutable(const char *p)
{
#if defined(__APPLE__)
    unsigned long size = 0;
    const uint8_t *start = getsegmentdata(&_mh_execute_header, "__TEXT", &size);
    const uint8_t *q = reinterpret_cast<const uint8_t *>(p);
    return start != nullptr && q >= start && q < start + size;
#else
    (void)p;
    return false;
#endif
}

/* Set by cyd_ui_submit(); sim_main.cpp renders it. */
cyd_display_screen_t g_cyd_sim_submitted;
bool g_cyd_sim_has_submitted = false;

esp_err_t cyd_display_submit_screen(const cyd_display_screen_t *screen)
{
    if (screen == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }
    g_cyd_sim_submitted = *screen;
    g_cyd_sim_has_submitted = true;
    return ESP_OK;
}

}  // extern "C"
