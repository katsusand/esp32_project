#ifndef APP_LAUNCHER_VIEW_H
#define APP_LAUNCHER_VIEW_H

/*
 * The launcher screen, built from a model only: one page of registered apps,
 * page navigation, and a back button when the launcher is not home.
 *
 * English contract: app_launcher_view_build() calls no service and touches no
 * global state, so the simulator and the host tests can build it by hand.
 */

#include <stdbool.h>
#include <stddef.h>
#include "cyd_display.h"

#ifdef __cplusplus
extern "C" {
#endif

#define APP_LAUNCHER_ACTION_BACK 0x2500
#define APP_LAUNCHER_ACTION_PREV_PAGE 0x2501
#define APP_LAUNCHER_ACTION_NEXT_PAGE 0x2502
/* + index on the current page */
#define APP_LAUNCHER_ACTION_APP_BASE 0x2510

/* Apps per page: 32px buttons between the header and the page navigation. */
#define APP_LAUNCHER_VIEW_APPS_PER_PAGE 4

typedef struct {
    const char *titles[APP_LAUNCHER_VIEW_APPS_PER_PAGE]; /* this page only */
    size_t count;        /* on this page */
    size_t total;        /* registered apps */
    size_t page_index;
    size_t page_count;
    bool show_back;      /* false when the launcher is home: nothing to go back to */
} app_launcher_view_model_t;

/* Clears `screen` and builds the launcher. */
void app_launcher_view_build(cyd_display_screen_t *screen, const app_launcher_view_model_t *model);

#ifdef __cplusplus
}
#endif

#endif
