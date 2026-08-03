#ifndef APP_LAUNCHER_H
#define APP_LAUNCHER_H

#include "app_shell.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Generic home/menu app: lists everything in app_registry and switches to the
 * selected entry.
 *
 * It holds no product knowledge, so it works as the home app for any product,
 * or as a normal app reached from another screen. Which one it is depends
 * entirely on what the composition passes to app_shell_start().
 *
 * English contract: when used as home, the launcher hides its back button,
 * because app_shell has nowhere above home to return to.
 */
const app_shell_app_t *app_launcher_get_app(void);

#ifdef __cplusplus
}
#endif

#endif
