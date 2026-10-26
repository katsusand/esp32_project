#ifndef SYSTEM_SETTINGS_FIX_INTERNAL_H
#define SYSTEM_SETTINGS_FIX_INTERNAL_H

/*
 * The fix table without the "before app_shell_start()" rule, for the settings
 * app and the host tests. Products call system_settings_fix().
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "system_settings_fix.h"

/* READONLY / HIDDEN / NONE with the same checks as system_settings_fix(). */
esp_err_t system_settings_fix_set(system_settings_item_t item, system_settings_fix_mode_t mode, const char *reason);
/* Back to no fixes at all (host tests). */
void system_settings_fix_reset(void);

/* True for items with a value to show (READONLY allowed). */
bool system_settings_item_has_value(system_settings_item_t item);

/* True when every item listed is HIDDEN: the page they make up is left out. */
bool system_settings_items_all_hidden(const system_settings_item_t *items, size_t count);

/* Which settings item a button belongs to. */
typedef struct {
    uint16_t action_id;
    system_settings_item_t item;
} system_settings_action_item_t;

/* True when `action_id` belongs to a fixed item: the settings app ignores it. */
bool system_settings_fixed_action(uint16_t action_id, const system_settings_action_item_t *map, size_t count);

#endif
