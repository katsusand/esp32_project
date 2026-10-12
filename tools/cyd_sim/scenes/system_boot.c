/*
 * Screens system_boot shows before any app (system_boot_view.c).
 */
#include "cyd_ui.h"
#include "sim_catalog.h"
#include "system_boot_view.h"

static void build_shortcut_prompt(cyd_display_screen_t *screen, unsigned frame)
{
    (void)frame;
    system_boot_view_build_shortcut_prompt(screen);
}

static const cyd_sim_scene_t k_scenes[] = {
    { "boot_shortcut", "起動: 設定を開きます", "画面を押したまま起動したとき", build_shortcut_prompt },
};

CYD_SIM_REGISTER_SCENES(system_boot, CYD_SIM_ORDER_MAIN_APP - 1, k_scenes);
