#ifndef CYD_DISPLAY_PORT_H
#define CYD_DISPLAY_PORT_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * What the renderer needs to know about memory, supplied by whoever links it.
 *
 * The firmware (cyd_display.cpp) answers from the ESP32 memory map. The desktop
 * simulator (tools/cyd_sim) answers from its own executable image. Keeping the
 * answers behind these two calls is what lets the simulator compile the very
 * same rendering and text code as the device.
 */

/* True when `p` can be dereferenced at all. Guards the not-copied pointers. */
bool cyd_display_port_ptr_readable(const void *p);

/*
 * True when `p` points into storage that can never change or be freed: flash
 * rodata on the device. Only such text may be referenced instead of copied.
 */
bool cyd_display_port_text_is_immutable(const char *p);

#ifdef __cplusplus
}
#endif

#endif
