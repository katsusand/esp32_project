/*
 * cyd_sim - shows CYD screens on a Mac with the device's own renderer.
 *
 *   cyd_sim [--scene ID] [--scale N]     window; ←/→ switch scenes, click = tap
 *   cyd_sim --export DIR [--scale N]     PNG per scene + index.html, no window
 *   cyd_sim --list                       scene ids
 *
 * Rendering goes through the same path as cyd_display.cpp: the frame is diffed
 * against the previous one, and only dirty 16px strips are drawn into a strip
 * sprite and pushed. See docs/cyd_sim.md.
 */
#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include <LGFX_AUTODETECT.hpp>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>
#include <sys/stat.h>

#include "cyd_display_render.hpp"
#include "sim_catalog.h"

namespace {

constexpr int32_t PANEL_W = 320;
constexpr int32_t PANEL_H = 240;
/* Matches STRIP_HEIGHT_PX in cyd_display.cpp. */
constexpr int32_t STRIP_H = 16;
constexpr size_t STRIP_COUNT = (PANEL_H + STRIP_H - 1) / STRIP_H;
constexpr size_t MAX_DIRTY_RECTS = CYD_DISPLAY_MAX_WIDGETS * 2;
/* The left/right arrow keys, as LovyanGFX's SDL panel maps them to GPIOs. */
constexpr uint32_t KEY_GPIO_LEFT = 39;
constexpr uint32_t KEY_GPIO_RIGHT = 37;

/* Filled by cyd_sim_register_scene_list() before main() runs. */
std::vector<const cyd_sim_scene_list_t *> &registered_lists()
{
    static std::vector<const cyd_sim_scene_list_t *> lists;
    return lists;
}

std::vector<const cyd_sim_scene_t *> all_scenes()
{
    std::vector<const cyd_sim_scene_list_t *> lists = registered_lists();
    std::sort(lists.begin(), lists.end(),
              [](const cyd_sim_scene_list_t *a, const cyd_sim_scene_list_t *b) {
                  if (a->order != b->order) {
                      return a->order < b->order;
                  }
                  return std::strcmp(a->name, b->name) < 0;
              });
    std::vector<const cyd_sim_scene_t *> out;
    for (const cyd_sim_scene_list_t *list : lists) {
        for (size_t i = 0; i < list->count; ++i) {
            out.push_back(&list->scenes[i]);
        }
    }
    return out;
}

/* The device's strip compositor, pointed at any LovyanGFX target. */
class StripRenderer {
public:
    StripRenderer()
    {
        strip_.setColorDepth(16);
        strip_.createSprite(PANEL_W, STRIP_H);
    }

    void invalidate() { has_previous_ = false; }

    /* Returns how many strips were redrawn. */
    template <typename TTarget>
    size_t apply(TTarget &target, const cyd_display_screen_t &screen)
    {
        cyd_display_dirty_rect_t rects[MAX_DIRTY_RECTS];
        size_t rect_count = 0;
        bool dirty[STRIP_COUNT];
        size_t drawn = 0;

        cyd_display_collect_dirty_rects(previous_, has_previous_, screen, PANEL_W, PANEL_H,
                                        rects, MAX_DIRTY_RECTS, &rect_count);
        cyd_display_mark_dirty_strips(dirty, STRIP_COUNT, STRIP_H, rects, rect_count);
        for (size_t i = 0; i < STRIP_COUNT; ++i) {
            if (!dirty[i]) {
                continue;
            }
            int32_t y = static_cast<int32_t>(i) * STRIP_H;
            cyd_display_dirty_rect_t strip_rect = { 0, y, PANEL_W, STRIP_H };
            strip_.fillRect(0, 0, PANEL_W, STRIP_H, TFT_BLACK);
            cyd_display_render_screen_to_target(strip_, screen, 0, y, &strip_rect);
            strip_.pushSprite(&target, 0, y);
            ++drawn;
        }
        previous_ = screen;
        has_previous_ = true;
        return drawn;
    }

private:
    LGFX_Sprite strip_;
    cyd_display_screen_t previous_ = {};
    bool has_previous_ = false;
};

struct Options {
    std::string export_dir;
    std::string start_scene;
    int scale = 2;
    bool list = false;
};

Options g_options;

void html_escape(FILE *f, const char *text)
{
    for (const char *p = text != nullptr ? text : ""; *p != '\0'; ++p) {
        switch (*p) {
            case '&': std::fputs("&amp;", f); break;
            case '<': std::fputs("&lt;", f); break;
            case '>': std::fputs("&gt;", f); break;
            case '"': std::fputs("&quot;", f); break;
            default: std::fputc(*p, f); break;
        }
    }
}

int run_export(const Options &options)
{
    auto scenes = all_scenes();
    mkdir(options.export_dir.c_str(), 0755);

    LGFX_Sprite frame;
    frame.setColorDepth(16);
    frame.createSprite(PANEL_W, PANEL_H);
    LGFX_Sprite scaled;
    scaled.setColorDepth(24);
    scaled.createSprite(PANEL_W * options.scale, PANEL_H * options.scale);

    std::string index_path = options.export_dir + "/index.html";
    FILE *index = std::fopen(index_path.c_str(), "w");
    if (index == nullptr) {
        std::perror(index_path.c_str());
        return 1;
    }
    std::fprintf(index,
                 "<!doctype html><meta charset=\"utf-8\"><title>cyd_sim export</title>\n"
                 "<style>body{background:#1b1f27;color:#e6eaf1;font-family:sans-serif;margin:24px}"
                 "main{display:flex;flex-wrap:wrap;gap:24px}figure{margin:0;max-width:%dpx}"
                 "img{display:block;image-rendering:pixelated;border:6px solid #000;border-radius:8px}"
                 "figcaption{font-size:14px;line-height:1.5;margin-top:6px}code{color:#98a2b3}</style>\n"
                 "<h1>cyd_sim</h1><main>\n",
                 PANEL_W * options.scale + 12);

    for (const cyd_sim_scene_t *scene : scenes) {
        StripRenderer renderer;
        cyd_display_screen_t screen = {};
        scene->build(&screen, 0);
        frame.fillScreen(TFT_BLACK);
        renderer.apply(frame, screen);

        for (int32_t y = 0; y < PANEL_H; ++y) {
            for (int32_t x = 0; x < PANEL_W; ++x) {
                scaled.fillRect(x * options.scale, y * options.scale, options.scale, options.scale,
                                frame.readPixelRGB(x, y));
            }
        }

        size_t len = 0;
        void *png = scaled.createPng(&len, 0, 0, scaled.width(), scaled.height());
        std::string path = options.export_dir + "/" + scene->id + ".png";
        FILE *f = std::fopen(path.c_str(), "wb");
        if (png == nullptr || f == nullptr) {
            std::fprintf(stderr, "failed to write %s\n", path.c_str());
            if (f != nullptr) {
                std::fclose(f);
            }
            std::free(png);
            std::fclose(index);
            return 1;
        }
        std::fwrite(png, 1, len, f);
        std::fclose(f);
        std::free(png);
        std::printf("%s\n", path.c_str());

        std::fprintf(index, "<figure><img src=\"%s.png\" width=\"%d\" alt=\"\"><figcaption><b>",
                     scene->id, PANEL_W * options.scale);
        html_escape(index, scene->title);
        std::fputs("</b> <code>", index);
        html_escape(index, scene->id);
        std::fputs("</code><br>", index);
        html_escape(index, scene->note);
        std::fputs("</figcaption></figure>\n", index);
    }
    std::fputs("</main>\n", index);
    std::fclose(index);
    std::printf("%s\n", index_path.c_str());
    return 0;
}

int window_main(bool *running)
{
    auto scenes = all_scenes();
    if (scenes.empty()) {
        return 1;
    }

    size_t index = 0;
    for (size_t i = 0; i < scenes.size(); ++i) {
        if (g_options.start_scene == scenes[i]->id) {
            index = i;
        }
    }

    LGFX lcd(PANEL_W, PANEL_H, g_options.scale, g_options.scale);
    lcd.init();
    lcd.fillScreen(TFT_BLACK);
    auto *panel = static_cast<lgfx::Panel_sdl *>(lcd.getPanel());

    StripRenderer renderer;
    unsigned frame = 0;
    bool was_left = false;
    bool was_right = false;
    bool was_touching = false;
    size_t titled_index = SIZE_MAX;
    cyd_display_screen_t screen = {};

    std::printf("cyd_sim: ←/→ switch scenes, click to tap, Ctrl+1..6 zoom\n");
    while (*running) {
        bool left = !lgfx::gpio_in(KEY_GPIO_LEFT);
        bool right = !lgfx::gpio_in(KEY_GPIO_RIGHT);
        if (left && !was_left) {
            index = (index + scenes.size() - 1) % scenes.size();
            frame = 0;
        }
        if (right && !was_right) {
            index = (index + 1) % scenes.size();
            frame = 0;
        }
        was_left = left;
        was_right = right;

        const cyd_sim_scene_t *scene = scenes[index];
        if (titled_index != index) {
            char title[160];
            std::snprintf(title, sizeof(title), "cyd_sim %zu/%zu  %s  [%s]",
                          index + 1, scenes.size(), scene->title, scene->id);
            panel->setWindowTitle(title);
            std::printf("scene %s: %s\n", scene->id, scene->title);
            titled_index = index;
        }

        screen = {};
        scene->build(&screen, frame++);
        renderer.apply(lcd, screen);

        lgfx::touch_point_t tp;
        bool touching = lcd.getTouch(&tp) > 0;
        if (touching && !was_touching) {
            uint16_t action = 0;
            uint8_t col = static_cast<uint8_t>(tp.x / CYD_DISPLAY_GRID_CELL_PX);
            uint8_t row = static_cast<uint8_t>(tp.y / CYD_DISPLAY_GRID_CELL_PX);
            if (cyd_display_hit_test_cell(screen, col, row, &action)) {
                std::printf("tap (%d,%d) cell (%u,%u) -> action %u\n", tp.x, tp.y, col, row, action);
            } else {
                std::printf("tap (%d,%d) cell (%u,%u) -> no button\n", tp.x, tp.y, col, row);
            }
        }
        was_touching = touching;
        lgfx::delay(33);
    }
    return 0;
}

int parse(int argc, char **argv, Options *options)
{
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--export" && i + 1 < argc) {
            options->export_dir = argv[++i];
        } else if (arg == "--scene" && i + 1 < argc) {
            options->start_scene = argv[++i];
        } else if (arg == "--scale" && i + 1 < argc) {
            options->scale = std::atoi(argv[++i]);
            if (options->scale < 1 || options->scale > 6) {
                std::fprintf(stderr, "--scale must be 1..6\n");
                return 1;
            }
        } else if (arg == "--list") {
            options->list = true;
        } else {
            std::fprintf(stderr,
                         "usage: cyd_sim [--scene ID] [--scale N] | --export DIR [--scale N] | --list\n");
            return 1;
        }
    }
    return 0;
}

}  // namespace

extern "C" void cyd_sim_register_scene_list(const cyd_sim_scene_list_t *list)
{
    registered_lists().push_back(list);
}

int main(int argc, char **argv)
{
    /* Scenes use fixed instants; render them in the device's time zone. */
    setenv("TZ", "JST-9", 1);
    tzset();
    if (parse(argc, argv, &g_options) != 0) {
        return 2;
    }
    if (g_options.list) {
        for (const cyd_sim_scene_t *scene : all_scenes()) {
            std::printf("%-28s %s\n", scene->id, scene->title);
        }
        return 0;
    }
    if (!g_options.export_dir.empty()) {
        return run_export(g_options);
    }
    return lgfx::Panel_sdl::main(window_main);
}
