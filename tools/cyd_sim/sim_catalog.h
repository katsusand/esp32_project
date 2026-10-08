#ifndef CYD_SIM_CATALOG_H
#define CYD_SIM_CATALOG_H

#include <stddef.h>
#include "cyd_display.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * One screen state the simulator can show.
 *
 * `build` fills `screen` exactly as the app would before submitting it. It is
 * called again on every frame, so a scene that depends on time (a clock, a
 * spinner) animates, and the frame diff runs just as it does on the device.
 * `frame` counts calls since the scene was entered.
 */
typedef struct {
    const char *id;      /* file name for --export, e.g. "punch_idle" */
    const char *title;   /* shown in the window title and the index page */
    const char *note;    /* one line for the index page; may be NULL */
    void (*build)(cyd_display_screen_t *screen, unsigned frame);
} cyd_sim_scene_t;

typedef struct {
    const cyd_sim_scene_t *scenes;
    size_t count;
    int order; /* lists are shown in ascending order, then by name */
    const char *name;
} cyd_sim_scene_list_t;

/*
 * Each scenes/ file registers its own list, so adding a file is all it takes
 * to add scenes: the build globs every .c file in scenes/, and nothing else names the list.
 * A project that derives from this one adds its scene files without touching
 * shared simulator sources.
 *
 * English contract: registration runs from a static constructor before main()
 * and must not depend on any other list being registered first.
 */
void cyd_sim_register_scene_list(const cyd_sim_scene_list_t *list);

/* Suggested orders: the product's main app first, the font specimen last. */
#define CYD_SIM_ORDER_MAIN_APP 10
#define CYD_SIM_ORDER_LAUNCHER 20
#define CYD_SIM_ORDER_SETTINGS 30
#define CYD_SIM_ORDER_COMMON 40
#define CYD_SIM_ORDER_SPECIMEN 90

#define CYD_SIM_REGISTER_SCENES(list_name, list_order, scene_array)                \
    static const cyd_sim_scene_list_t s_scene_list_##list_name = {               \
        .scenes = (scene_array),                                                 \
        .count = sizeof(scene_array) / sizeof((scene_array)[0]),                 \
        .order = (list_order),                                                   \
        .name = #list_name,                                                      \
    };                                                                           \
    __attribute__((constructor)) static void s_register_scenes_##list_name(void) \
    {                                                                            \
        cyd_sim_register_scene_list(&s_scene_list_##list_name);                  \
    }

#ifdef __cplusplus
}
#endif

#endif
