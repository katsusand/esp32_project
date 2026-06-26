#include <array>
#include <cstring>
#include <string>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_log.h"
#include "nvs.h"
#include "nvs_health.h"
#include "nvs_schema.h"
#include "sdkconfig.h"
#include "app_stack_monitor.h"
#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include "cyd_display.h"

namespace {

static const char *TAG = "cyd_display";
static constexpr uint8_t CYD_DISPLAY_MIN_SAFE_BRIGHTNESS = 13;
static constexpr int32_t GRID_CELL_PX = CYD_DISPLAY_GRID_CELL_PX;
static constexpr int32_t SCREEN_MARGIN_X = GRID_CELL_PX;
static constexpr uint8_t SCREEN_TITLE_ROW = 2;
static constexpr uint8_t SCREEN_FIRST_LINE_ROW = 6;
static constexpr uint8_t SCREEN_LINE_HEIGHT_ROWS = 2;
static constexpr size_t SCREEN_MAX_LINE_COUNT = 10;
static constexpr uint8_t MODE_BUTTON_ROW = 25;
static constexpr uint8_t MODE_BUTTON_HEIGHT_ROWS = 4;
static constexpr uint8_t MODE_BUTTON_GAP_COLS = 1;
static constexpr int32_t MODE_BUTTON_RADIUS = 8;
static constexpr uint8_t MODE_BUTTON_LEFT_COL = 1;
static constexpr uint8_t MODE_BUTTON_RIGHT_COL = CYD_DISPLAY_GRID_COLS - 2;
static constexpr size_t DISPLAY_QUEUE_LENGTH = 3;
static constexpr size_t DISPLAY_LOG_QUEUE_LENGTH = 8;
static constexpr uint32_t DISPLAY_TASK_STACK_SIZE = 4096;
static constexpr uint32_t DISPLAY_STACK_LOG_INTERVAL_MS = 30000;
static constexpr size_t MAX_DIRTY_RECTS = CYD_DISPLAY_MAX_WIDGETS * 2;
static constexpr int32_t STRIP_HEIGHT_PX = 16;
static constexpr size_t MAX_STRIP_COUNT = (320 + STRIP_HEIGHT_PX - 1) / STRIP_HEIGHT_PX;
static constexpr uint8_t LOG_TITLE_ROW = 0;
static constexpr uint8_t LOG_FIRST_LINE_ROW = 3;
static constexpr size_t LOG_VISIBLE_ROWS = CYD_DISPLAY_GRID_ROWS - LOG_FIRST_LINE_ROW;
static constexpr uint32_t CYD_DISPLAY_CONFIG_VERSION = 1U;

#ifdef CONFIG_CYD_DISPLAY_RGB_ORDER
static constexpr bool CYD_DISPLAY_RGB_ORDER = true;
#else
static constexpr bool CYD_DISPLAY_RGB_ORDER = false;
#endif

#ifdef CONFIG_CYD_DISPLAY_INVERT
static constexpr bool CYD_DISPLAY_INVERT = true;
#else
static constexpr bool CYD_DISPLAY_INVERT = false;
#endif

#ifdef CONFIG_CYD_DISPLAY_READABLE
static constexpr bool CYD_DISPLAY_READABLE = true;
#else
static constexpr bool CYD_DISPLAY_READABLE = false;
#endif

static spi_host_device_t cyd_spi_host_from_config(int host)
{
    return (host == 3) ? SPI3_HOST : SPI2_HOST;
}

class LGFX_CYD : public lgfx::LGFX_Device
{
    #if CONFIG_CYD_DISPLAY_PANEL_ILI9341
    lgfx::Panel_ILI9341 _panel_instance;
    #elif CONFIG_CYD_DISPLAY_PANEL_ILI9341_2
    lgfx::Panel_ILI9341_2 _panel_instance;
    #elif CONFIG_CYD_DISPLAY_PANEL_ST7789
    lgfx::Panel_ST7789 _panel_instance;
    #else
    #error "Unsupported CYD display panel type"
    #endif
    lgfx::Bus_SPI _bus_instance;
    lgfx::Light_PWM _light_instance;

public:
    LGFX_CYD(void)
    {
        {
            auto cfg = _bus_instance.config();
            cfg.spi_host = cyd_spi_host_from_config(CONFIG_CYD_DISPLAY_SPI_HOST);
            cfg.spi_mode = 0;
            cfg.freq_write = 40000000;
            cfg.freq_read = 16000000;
            cfg.spi_3wire = false;
            cfg.use_lock = true;
            cfg.dma_channel = SPI_DMA_DISABLED;
            cfg.pin_sclk = CONFIG_CYD_DISPLAY_PIN_SCLK;
            cfg.pin_mosi = CONFIG_CYD_DISPLAY_PIN_MOSI;
            cfg.pin_miso = CONFIG_CYD_DISPLAY_PIN_MISO;
            cfg.pin_dc = CONFIG_CYD_DISPLAY_PIN_DC;
            _bus_instance.config(cfg);
            _panel_instance.setBus(&_bus_instance);
        }

        {
            auto cfg = _panel_instance.config();
            cfg.pin_cs = CONFIG_CYD_DISPLAY_PIN_CS;
            cfg.pin_rst = CONFIG_CYD_DISPLAY_PIN_RST;
            cfg.pin_busy = -1;
            cfg.panel_width = 240;
            cfg.panel_height = 320;
            cfg.offset_x = CONFIG_CYD_DISPLAY_OFFSET_X;
            cfg.offset_y = CONFIG_CYD_DISPLAY_OFFSET_Y;
            cfg.offset_rotation = CONFIG_CYD_DISPLAY_OFFSET_ROTATION;
            cfg.dummy_read_pixel = 8;
            cfg.dummy_read_bits = 1;
            cfg.readable = CYD_DISPLAY_READABLE;
            cfg.invert = CYD_DISPLAY_INVERT;
            cfg.rgb_order = CYD_DISPLAY_RGB_ORDER;
            cfg.dlen_16bit = false;
            cfg.bus_shared = false;
            _panel_instance.config(cfg);
        }

        {
            auto cfg = _light_instance.config();
            cfg.pin_bl = CONFIG_CYD_DISPLAY_BACKLIGHT_GPIO;
            cfg.invert = false;
            cfg.freq = 44100;
            cfg.pwm_channel = 7;
            _light_instance.config(cfg);
            _panel_instance.setLight(&_light_instance);
        }

        setPanel(&_panel_instance);
    }
};

static LGFX_CYD s_display;
static bool s_initialized = false;
static uint8_t s_backlight_brightness = CONFIG_CYD_DISPLAY_BACKLIGHT_BRIGHTNESS;
static QueueHandle_t s_display_queue = nullptr;
static QueueHandle_t s_display_log_queue = nullptr;
static QueueSetHandle_t s_display_queue_set = nullptr;
static TaskHandle_t s_display_task_handle = nullptr;
static TaskHandle_t s_display_owner_task_handle = nullptr;
static cyd_display_screen_t s_current_screen = {};
static cyd_display_screen_t s_previous_screen = {};
static cyd_display_screen_t s_log_screen = {};
static cyd_display_grid_rect_t s_mode_button_rects[CYD_DISPLAY_MAX_MODE_BUTTONS];
static size_t s_mode_button_count = 0;
static bool s_has_previous_screen = false;
static LGFX_Sprite s_strip_sprite(&s_display);

typedef struct {
    uint32_t version;
    uint8_t brightness;
    uint8_t reserved[3];
} cyd_display_disk_t;

static esp_err_t cyd_display_load_brightness_blob(uint8_t *brightness)
{
    nvs_handle_t nvs_handle;
    cyd_display_disk_t disk = {};
    size_t disk_size = sizeof(disk);

    ESP_RETURN_ON_FALSE(brightness != nullptr, ESP_ERR_INVALID_ARG, TAG, "brightness is null");

    esp_err_t err = nvs_open_descriptor(NVS_KEY_CYD_DISPLAY_CONFIG.ns, NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_get_blob(nvs_handle, NVS_KEY_CYD_DISPLAY_CONFIG.key, &disk, &disk_size);
    nvs_close(nvs_handle);
    if (err != ESP_OK) {
        if (err == ESP_ERR_NVS_INVALID_LENGTH) {
            nvs_health_report_invalid(&NVS_KEY_CYD_DISPLAY_CONFIG, err, "invalid display config length");
        }
        return err;
    }
    if (disk_size != sizeof(disk) || disk.version != CYD_DISPLAY_CONFIG_VERSION) {
        nvs_health_report_invalid(&NVS_KEY_CYD_DISPLAY_CONFIG, ESP_ERR_INVALID_VERSION, "invalid display config");
        return ESP_ERR_INVALID_VERSION;
    }

    *brightness = (disk.brightness < CYD_DISPLAY_MIN_SAFE_BRIGHTNESS)
                      ? CYD_DISPLAY_MIN_SAFE_BRIGHTNESS
                      : disk.brightness;
    return ESP_OK;
}

static esp_err_t cyd_display_write_brightness(uint8_t brightness)
{
    nvs_handle_t nvs_handle;
    cyd_display_disk_t disk = {
        .version = CYD_DISPLAY_CONFIG_VERSION,
        .brightness = brightness,
        .reserved = {0},
    };

    ESP_RETURN_ON_ERROR(nvs_open_descriptor(NVS_KEY_CYD_DISPLAY_CONFIG.ns,
                                            NVS_READWRITE,
                                            &nvs_handle),
                        TAG,
                        "open NVS failed");
    esp_err_t err = nvs_set_blob(nvs_handle, NVS_KEY_CYD_DISPLAY_CONFIG.key, &disk, sizeof(disk));
    if (err == ESP_OK) {
        err = nvs_commit(nvs_handle);
    }
    nvs_close(nvs_handle);
    return err;
}
static bool s_strip_sprite_ready = false;
static size_t s_strip_count = 0;

typedef struct {
    int32_t x;
    int32_t y;
    int32_t w;
    int32_t h;
} cyd_display_dirty_rect_t;

typedef enum {
    CYD_DISPLAY_LOG_CMD_SHOW = 0,
    CYD_DISPLAY_LOG_CMD_HIDE,
    CYD_DISPLAY_LOG_CMD_CLEAR,
    CYD_DISPLAY_LOG_CMD_PUSH,
    CYD_DISPLAY_LOG_CMD_SCROLL,
} cyd_display_log_cmd_id_t;

typedef struct {
    cyd_display_log_cmd_id_t id;
    int scroll_delta;
    char text[CYD_DISPLAY_TEXT_MAX_LEN + 1];
} cyd_display_log_cmd_t;

typedef struct {
    bool visible;
    size_t head;
    size_t count;
    size_t scroll_offset;
    char title[CYD_DISPLAY_TEXT_MAX_LEN + 1];
    char lines[CYD_DISPLAY_LOG_MAX_LINES][CYD_DISPLAY_TEXT_MAX_LEN + 1];
} cyd_display_log_state_t;

static cyd_display_dirty_rect_t s_dirty_rects[MAX_DIRTY_RECTS];
static cyd_display_log_state_t s_log_state = {
    .visible = false,
    .head = 0,
    .count = 0,
    .scroll_offset = 0,
    .title = "Log",
    .lines = {{ 0 }},
};

template <typename TDisplay>
static void cyd_display_draw_centered_line(TDisplay &display,
                                           const std::string &text,
                                           int32_t center_x,
                                           int32_t y,
                                           uint8_t text_size,
                                           uint16_t color);
template <typename TDisplay>
static void cyd_display_draw_left_line(TDisplay &display,
                                       const std::string &text,
                                       int32_t x,
                                       int32_t y,
                                       uint8_t text_size,
                                       uint16_t color);
template <typename TDisplay>
static void cyd_display_draw_calibration_marker(TDisplay &display,
                                                int32_t x,
                                                int32_t y,
                                                int32_t radius,
                                                uint16_t fg_color,
                                                uint16_t bg_color);
static void cyd_display_clear_mode_button_map(void);
static int32_t cyd_display_col_to_px(uint8_t col);
static int32_t cyd_display_row_to_px(uint8_t row);
static bool cyd_display_mode_button_rect_for_count(size_t button_count, size_t index, cyd_display_grid_rect_t *rect);
static void cyd_display_update_button_map_from_screen(const cyd_display_screen_t &screen);
static void cyd_display_copy_text(char *dst, size_t dst_size, const char *src);
static bool cyd_display_add_widget(cyd_display_screen_t *screen, const cyd_display_widget_t *widget);
static bool cyd_display_widget_equals(const cyd_display_widget_t &lhs, const cyd_display_widget_t &rhs);
static bool cyd_display_widget_bounds_px(const cyd_display_widget_t &widget, cyd_display_dirty_rect_t *rect);
static bool cyd_display_widget_intersects_rect(const cyd_display_widget_t &widget, const cyd_display_dirty_rect_t &rect);
static void cyd_display_append_dirty_rect(cyd_display_dirty_rect_t *rects, size_t *rect_count, const cyd_display_dirty_rect_t &rect);
static void cyd_display_collect_dirty_rects(cyd_display_dirty_rect_t *rects, size_t *rect_count);
template <typename TDisplay>
static void cyd_display_render_screen_to_target(TDisplay &display,
                                                const cyd_display_screen_t &screen,
                                                int32_t origin_x,
                                                int32_t origin_y,
                                                const cyd_display_dirty_rect_t *clip_rect);
static void cyd_display_mark_dirty_strips(bool *dirty_strips, const cyd_display_dirty_rect_t *rects, size_t rect_count);
static bool cyd_display_flush_strip(size_t strip_index);
static void cyd_display_flush_rects(const cyd_display_dirty_rect_t *rects, size_t rect_count);
static void cyd_display_task(void *arg);
static uint16_t cyd_display_resolve_bg(const cyd_display_widget_t &widget);
static void cyd_display_apply_screen(const cyd_display_screen_t &screen);
static void cyd_display_handle_log_cmd(const cyd_display_log_cmd_t &cmd);
static void cyd_display_render_log_screen(void);
static const char *cyd_display_log_line_at(size_t logical_index);
static esp_err_t cyd_display_submit_log_cmd(const cyd_display_log_cmd_t *cmd);
static void cyd_display_log_stack_usage(void);
static cyd_display_screen_t cyd_display_make_empty_screen(void);
static cyd_display_widget_t cyd_display_make_widget(cyd_display_widget_type_t type);
static cyd_display_dirty_rect_t cyd_display_make_empty_dirty_rect(void);
static cyd_display_grid_rect_t cyd_display_make_empty_grid_rect(void);
static cyd_display_log_cmd_t cyd_display_make_log_cmd(cyd_display_log_cmd_id_t id, int scroll_delta);

static const char *cyd_display_panel_name(void)
{
#if CONFIG_CYD_DISPLAY_PANEL_ILI9341
    return "ILI9341";
#elif CONFIG_CYD_DISPLAY_PANEL_ILI9341_2
    return "ILI9341_2";
#elif CONFIG_CYD_DISPLAY_PANEL_ST7789
    return "ST7789";
#else
    return "unknown";
#endif
}

static cyd_display_screen_t cyd_display_make_empty_screen(void)
{
    return {};
}

static cyd_display_widget_t cyd_display_make_widget(cyd_display_widget_type_t type)
{
    cyd_display_widget_t widget = {};
    widget.type = type;
    return widget;
}

static cyd_display_dirty_rect_t cyd_display_make_empty_dirty_rect(void)
{
    return {};
}

static cyd_display_grid_rect_t cyd_display_make_empty_grid_rect(void)
{
    return {};
}

static cyd_display_log_cmd_t cyd_display_make_log_cmd(cyd_display_log_cmd_id_t id, int scroll_delta)
{
    cyd_display_log_cmd_t cmd = {};
    cmd.id = id;
    cmd.scroll_delta = scroll_delta;
    return cmd;
}

static esp_err_t cyd_display_check_ready(void)
{
    ESP_RETURN_ON_FALSE(s_initialized, ESP_ERR_INVALID_STATE, TAG, "display not initialized");
    return ESP_OK;
}

static esp_err_t cyd_display_check_owner(void)
{
    if (s_display_owner_task_handle == nullptr) {
        return ESP_OK;
    }

    TaskHandle_t current_task = xTaskGetCurrentTaskHandle();
    ESP_RETURN_ON_FALSE(current_task == s_display_owner_task_handle,
                        ESP_ERR_INVALID_STATE,
                        TAG,
                        "display owner mismatch");
    return ESP_OK;
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

template <typename TDisplay>
static void cyd_display_draw_calibration_marker(TDisplay &display,
                                                int32_t x,
                                                int32_t y,
                                                int32_t radius,
                                                uint16_t fg_color,
                                                uint16_t bg_color)
{
    int32_t left = x - radius;
    int32_t top = y - radius;
    int32_t size = radius * 2 + 1;
    display.fillRect(left, top, size, size, bg_color);
    if (fg_color == bg_color) {
        return;
    }

    int32_t line_half_width = radius >> 3;
    if (line_half_width < 1) {
        line_half_width = 1;
    }

    display.setClipRect(left, top, size, size);
    display.fillRect(x - line_half_width,
                     y - radius,
                     line_half_width * 2 + 1,
                     radius * 2 + 1,
                     fg_color);
    display.fillRect(x - radius,
                     y - line_half_width,
                     radius * 2 + 1,
                     line_half_width * 2 + 1,
                     fg_color);
    for (int32_t i = -radius; i <= radius; ++i) {
        display.drawFastHLine(x + i - line_half_width, y + i, line_half_width * 2 + 1, fg_color);
        display.drawFastHLine(x - i - line_half_width, y + i, line_half_width * 2 + 1, fg_color);
    }
    display.clearClipRect();
}

static void cyd_display_clear_mode_button_map(void)
{
    s_mode_button_count = 0;
    memset(s_mode_button_rects, 0, sizeof(s_mode_button_rects));
}

static int32_t cyd_display_col_to_px(uint8_t col)
{
    return static_cast<int32_t>(col) * GRID_CELL_PX;
}

static int32_t cyd_display_row_to_px(uint8_t row)
{
    return static_cast<int32_t>(row) * GRID_CELL_PX;
}

static bool cyd_display_mode_button_rect_for_count(size_t button_count, size_t index, cyd_display_grid_rect_t *rect)
{
    if (button_count == 0 || button_count > CYD_DISPLAY_MAX_MODE_BUTTONS || index >= button_count || rect == nullptr) {
        return false;
    }

    int32_t available_cols = MODE_BUTTON_RIGHT_COL - MODE_BUTTON_LEFT_COL + 1;
    int32_t total_gap_cols = static_cast<int32_t>((button_count - 1) * MODE_BUTTON_GAP_COLS);
    int32_t button_cols = (available_cols - total_gap_cols) / static_cast<int32_t>(button_count);
    if (button_cols < 1) {
        return false;
    }

    int32_t first_col = MODE_BUTTON_LEFT_COL + static_cast<int32_t>(index) * (button_cols + MODE_BUTTON_GAP_COLS);
    rect->col = static_cast<uint8_t>(first_col);
    rect->row = MODE_BUTTON_ROW;
    rect->width = static_cast<uint8_t>(button_cols);
    rect->height = MODE_BUTTON_HEIGHT_ROWS;
    return true;
}

static void cyd_display_copy_text(char *dst, size_t dst_size, const char *src)
{
    if (dst == nullptr || dst_size == 0) {
        return;
    }

    if (src == nullptr) {
        dst[0] = '\0';
        return;
    }

    strncpy(dst, src, dst_size - 1);
    dst[dst_size - 1] = '\0';
}

static bool cyd_display_add_widget(cyd_display_screen_t *screen, const cyd_display_widget_t *widget)
{
    if (screen == nullptr || widget == nullptr || screen->widget_count >= CYD_DISPLAY_MAX_WIDGETS) {
        return false;
    }

    screen->widgets[screen->widget_count++] = *widget;
    return true;
}

static uint16_t cyd_display_resolve_bg(const cyd_display_widget_t &widget)
{
    return widget.bg_color != 0 ? widget.bg_color : TFT_BLACK;
}

static void cyd_display_update_button_map_from_screen(const cyd_display_screen_t &screen)
{
    cyd_display_clear_mode_button_map();

    for (size_t i = 0; i < screen.widget_count; ++i) {
        const cyd_display_widget_t &widget = screen.widgets[i];
        if (widget.type != CYD_DISPLAY_WIDGET_BUTTON || !widget.enabled || s_mode_button_count >= CYD_DISPLAY_MAX_MODE_BUTTONS) {
            continue;
        }

        s_mode_button_rects[s_mode_button_count].col = widget.col;
        s_mode_button_rects[s_mode_button_count].row = widget.row;
        s_mode_button_rects[s_mode_button_count].width = widget.span_cols;
        s_mode_button_rects[s_mode_button_count].height = widget.span_rows;
        ++s_mode_button_count;
    }
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
        int32_t text_x = x;
        int32_t text_y = y;

        switch (widget.type) {
            case CYD_DISPLAY_WIDGET_TEXT:
                if (widget.align == CYD_DISPLAY_ALIGN_LEFT) {
                    cyd_display_draw_left_line(display,
                                               widget.text,
                                               x,
                                               y,
                                               widget.scale_y > 0 ? widget.scale_y : 1,
                                               widget.fg_color);
                } else if (widget.align == CYD_DISPLAY_ALIGN_RIGHT) {
                    display.setTextDatum(lgfx::top_right);
                    display.setTextSize(widget.scale_y > 0 ? widget.scale_y : 1);
                    display.setTextColor(widget.fg_color, cyd_display_resolve_bg(widget));
                    display.drawString(widget.text, x + w, y);
                } else {
                    text_x = x + (w / 2);
                    text_y = y + (h / 2);
                    cyd_display_draw_centered_line(display,
                                                   widget.text,
                                                   text_x,
                                                   text_y,
                                                   widget.scale_y > 0 ? widget.scale_y : 1,
                                                   widget.fg_color);
                }
                break;

            case CYD_DISPLAY_WIDGET_BUTTON:
                display.fillRoundRect(x, y, w, h, MODE_BUTTON_RADIUS, cyd_display_resolve_bg(widget));
                display.drawRoundRect(x,
                                      y,
                                      w,
                                      h,
                                      MODE_BUTTON_RADIUS,
                                      widget.border_color != 0 ? widget.border_color : TFT_LIGHTGREY);
                display.setTextDatum(lgfx::middle_center);
                display.setTextSize(widget.scale_y > 0 ? widget.scale_y : 1);
                display.setTextColor(widget.fg_color, cyd_display_resolve_bg(widget));
                display.drawString(widget.text, x + (w / 2), y + (h / 2));
                break;

            case CYD_DISPLAY_WIDGET_ICON:
                if (widget.bitmap != nullptr && widget.bitmap->data != nullptr) {
                    display.pushImage(x,
                                      y,
                                      widget.bitmap->width_px,
                                      widget.bitmap->height_px,
                                      widget.bitmap->data);
                }
                break;

            case CYD_DISPLAY_WIDGET_NONE:
            default:
                break;
        }
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
           lhs.bitmap == rhs.bitmap &&
           memcmp(lhs.text, rhs.text, sizeof(lhs.text)) == 0;
}

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
    cyd_display_dirty_rect_t widget_rect = cyd_display_make_empty_dirty_rect();
    if (!cyd_display_widget_bounds_px(widget, &widget_rect)) {
        return false;
    }

    return widget_rect.x < (rect.x + rect.w) &&
           (widget_rect.x + widget_rect.w) > rect.x &&
           widget_rect.y < (rect.y + rect.h) &&
           (widget_rect.y + widget_rect.h) > rect.y;
}

static void cyd_display_append_dirty_rect(cyd_display_dirty_rect_t *rects, size_t *rect_count, const cyd_display_dirty_rect_t &rect)
{
    if (rects == nullptr || rect_count == nullptr || rect.w <= 0 || rect.h <= 0) {
        return;
    }

    if (*rect_count >= MAX_DIRTY_RECTS) {
        rects[0] = {
            .x = 0,
            .y = 0,
            .w = s_display.width(),
            .h = s_display.height(),
        };
        *rect_count = 1;
        return;
    }

    rects[*rect_count] = rect;
    ++(*rect_count);
}

static void cyd_display_collect_dirty_rects(cyd_display_dirty_rect_t *rects, size_t *rect_count)
{
    if (rect_count == nullptr) {
        return;
    }

    *rect_count = 0;

    if (!s_has_previous_screen) {
        cyd_display_append_dirty_rect(rects,
                                      rect_count,
                                      {
                                          .x = 0,
                                          .y = 0,
                                          .w = s_display.width(),
                                          .h = s_display.height(),
                                      });
        return;
    }

    size_t max_widgets = s_current_screen.widget_count > s_previous_screen.widget_count
                             ? s_current_screen.widget_count
                             : s_previous_screen.widget_count;

    for (size_t i = 0; i < max_widgets; ++i) {
        const cyd_display_widget_t *current_widget = i < s_current_screen.widget_count ? &s_current_screen.widgets[i] : nullptr;
        const cyd_display_widget_t *previous_widget = i < s_previous_screen.widget_count ? &s_previous_screen.widgets[i] : nullptr;

        if (current_widget != nullptr && previous_widget != nullptr && cyd_display_widget_equals(*current_widget, *previous_widget)) {
            continue;
        }

        cyd_display_dirty_rect_t rect = cyd_display_make_empty_dirty_rect();
        if (previous_widget != nullptr && cyd_display_widget_bounds_px(*previous_widget, &rect)) {
            cyd_display_append_dirty_rect(rects, rect_count, rect);
        }
        if (current_widget != nullptr && cyd_display_widget_bounds_px(*current_widget, &rect)) {
            cyd_display_append_dirty_rect(rects, rect_count, rect);
        }
    }
}

static void cyd_display_mark_dirty_strips(bool *dirty_strips, const cyd_display_dirty_rect_t *rects, size_t rect_count)
{
    if (dirty_strips == nullptr) {
        return;
    }

    memset(dirty_strips, 0, sizeof(bool) * MAX_STRIP_COUNT);
    if (rects == nullptr) {
        return;
    }

    for (size_t i = 0; i < rect_count; ++i) {
        const cyd_display_dirty_rect_t &rect = rects[i];
        if (rect.w <= 0 || rect.h <= 0) {
            continue;
        }

        int32_t start_strip = rect.y / STRIP_HEIGHT_PX;
        int32_t end_strip = (rect.y + rect.h - 1) / STRIP_HEIGHT_PX;
        if (start_strip < 0) {
            start_strip = 0;
        }
        if (end_strip >= static_cast<int32_t>(s_strip_count)) {
            end_strip = static_cast<int32_t>(s_strip_count) - 1;
        }

        for (int32_t strip = start_strip; strip <= end_strip; ++strip) {
            dirty_strips[strip] = true;
        }
    }
}

static bool cyd_display_flush_strip(size_t strip_index)
{
    if (!s_strip_sprite_ready || strip_index >= s_strip_count) {
        return false;
    }

    int32_t strip_y = static_cast<int32_t>(strip_index) * STRIP_HEIGHT_PX;
    if ((strip_y + STRIP_HEIGHT_PX) > s_display.height()) {
        return false;
    }

    cyd_display_dirty_rect_t strip_rect = {
        .x = 0,
        .y = strip_y,
        .w = s_display.width(),
        .h = STRIP_HEIGHT_PX,
    };

    s_strip_sprite.fillRect(0, 0, s_display.width(), STRIP_HEIGHT_PX, TFT_BLACK);
    cyd_display_render_screen_to_target(s_strip_sprite, s_current_screen, 0, strip_y, &strip_rect);
    s_strip_sprite.pushSprite(0, strip_y);
    return true;
}

static void cyd_display_flush_rects(const cyd_display_dirty_rect_t *rects, size_t rect_count)
{
    if (!s_strip_sprite_ready || rects == nullptr || rect_count == 0) {
        return;
    }

    bool dirty_strips[MAX_STRIP_COUNT];
    cyd_display_mark_dirty_strips(dirty_strips, rects, rect_count);

    for (size_t strip = 0; strip < s_strip_count; ++strip) {
        if (dirty_strips[strip]) {
            (void)cyd_display_flush_strip(strip);
        }
    }
}

static void cyd_display_apply_screen(const cyd_display_screen_t &screen)
{
    s_current_screen = screen;
    cyd_display_update_button_map_from_screen(s_current_screen);
    size_t dirty_rect_count = 0;
    cyd_display_collect_dirty_rects(s_dirty_rects, &dirty_rect_count);
    cyd_display_flush_rects(s_dirty_rects, dirty_rect_count);
    s_previous_screen = s_current_screen;
    s_has_previous_screen = true;
}

static const char *cyd_display_log_line_at(size_t logical_index)
{
    if (logical_index >= s_log_state.count) {
        return "";
    }

    size_t oldest = (s_log_state.head + CYD_DISPLAY_LOG_MAX_LINES - s_log_state.count) % CYD_DISPLAY_LOG_MAX_LINES;
    size_t physical = (oldest + logical_index) % CYD_DISPLAY_LOG_MAX_LINES;
    return s_log_state.lines[physical];
}

static void cyd_display_render_log_screen(void)
{
    memset(&s_log_screen, 0, sizeof(s_log_screen));

    cyd_display_widget_t title_widget = cyd_display_make_widget(CYD_DISPLAY_WIDGET_TEXT);
    title_widget.col = 0;
    title_widget.row = LOG_TITLE_ROW;
    title_widget.span_cols = CYD_DISPLAY_GRID_COLS;
    title_widget.span_rows = 2;
    title_widget.align = CYD_DISPLAY_ALIGN_CENTER;
    title_widget.scale_x = 1;
    title_widget.scale_y = 1;
    title_widget.fg_color = TFT_YELLOW;
    title_widget.bg_color = TFT_BLACK;
    cyd_display_copy_text(title_widget.text, sizeof(title_widget.text), s_log_state.title);
    cyd_display_add_widget(&s_log_screen, &title_widget);

    size_t visible_rows = LOG_VISIBLE_ROWS;
    if (visible_rows > (CYD_DISPLAY_MAX_WIDGETS - s_log_screen.widget_count)) {
        visible_rows = CYD_DISPLAY_MAX_WIDGETS - s_log_screen.widget_count;
    }
    if (visible_rows > s_log_state.count) {
        visible_rows = s_log_state.count;
    }

    size_t max_scroll = s_log_state.count > visible_rows ? s_log_state.count - visible_rows : 0;
    if (s_log_state.scroll_offset > max_scroll) {
        s_log_state.scroll_offset = max_scroll;
    }

    size_t first_line = s_log_state.count > (visible_rows + s_log_state.scroll_offset)
                            ? s_log_state.count - visible_rows - s_log_state.scroll_offset
                            : 0;

    for (size_t i = 0; i < visible_rows; ++i) {
        cyd_display_widget_t line_widget = cyd_display_make_widget(CYD_DISPLAY_WIDGET_TEXT);
        line_widget.col = 1;
        line_widget.row = static_cast<uint8_t>(LOG_FIRST_LINE_ROW + i);
        line_widget.span_cols = static_cast<uint8_t>(CYD_DISPLAY_GRID_COLS - 2);
        line_widget.span_rows = 1;
        line_widget.align = CYD_DISPLAY_ALIGN_LEFT;
        line_widget.scale_x = 1;
        line_widget.scale_y = 1;
        line_widget.fg_color = TFT_WHITE;
        line_widget.bg_color = TFT_BLACK;
        cyd_display_copy_text(line_widget.text, sizeof(line_widget.text), cyd_display_log_line_at(first_line + i));
        cyd_display_add_widget(&s_log_screen, &line_widget);
    }

    cyd_display_apply_screen(s_log_screen);
}

static void cyd_display_handle_log_cmd(const cyd_display_log_cmd_t &cmd)
{
    switch (cmd.id) {
        case CYD_DISPLAY_LOG_CMD_SHOW:
            s_log_state.visible = true;
            s_log_state.scroll_offset = 0;
            cyd_display_copy_text(s_log_state.title, sizeof(s_log_state.title), cmd.text[0] != '\0' ? cmd.text : "Log");
            cyd_display_render_log_screen();
            break;

        case CYD_DISPLAY_LOG_CMD_HIDE: {
            s_log_state.visible = false;
            memset(&s_log_screen, 0, sizeof(s_log_screen));
            cyd_display_apply_screen(s_log_screen);
            break;
        }

        case CYD_DISPLAY_LOG_CMD_CLEAR:
            memset(s_log_state.lines, 0, sizeof(s_log_state.lines));
            s_log_state.head = 0;
            s_log_state.count = 0;
            s_log_state.scroll_offset = 0;
            if (s_log_state.visible) {
                cyd_display_render_log_screen();
            }
            break;

        case CYD_DISPLAY_LOG_CMD_PUSH:
            cyd_display_copy_text(s_log_state.lines[s_log_state.head], sizeof(s_log_state.lines[s_log_state.head]), cmd.text);
            s_log_state.head = (s_log_state.head + 1) % CYD_DISPLAY_LOG_MAX_LINES;
            if (s_log_state.count < CYD_DISPLAY_LOG_MAX_LINES) {
                ++s_log_state.count;
            }
            s_log_state.scroll_offset = 0;
            if (s_log_state.visible) {
                cyd_display_render_log_screen();
            }
            break;

        case CYD_DISPLAY_LOG_CMD_SCROLL: {
            size_t visible_rows = LOG_VISIBLE_ROWS;
            if (visible_rows > (CYD_DISPLAY_MAX_WIDGETS - 1)) {
                visible_rows = CYD_DISPLAY_MAX_WIDGETS - 1;
            }
            if (visible_rows > s_log_state.count) {
                visible_rows = s_log_state.count;
            }

            size_t max_scroll = s_log_state.count > visible_rows ? s_log_state.count - visible_rows : 0;
            if (cmd.scroll_delta > 0) {
                size_t delta = static_cast<size_t>(cmd.scroll_delta);
                s_log_state.scroll_offset = (s_log_state.scroll_offset + delta) > max_scroll
                                                ? max_scroll
                                                : s_log_state.scroll_offset + delta;
            } else if (cmd.scroll_delta < 0) {
                size_t delta = static_cast<size_t>(-cmd.scroll_delta);
                s_log_state.scroll_offset = delta > s_log_state.scroll_offset ? 0 : s_log_state.scroll_offset - delta;
            }

            if (s_log_state.visible) {
                cyd_display_render_log_screen();
            }
            break;
        }

        default:
            break;
    }
}

static esp_err_t cyd_display_submit_log_cmd(const cyd_display_log_cmd_t *cmd)
{
    ESP_RETURN_ON_ERROR(cyd_display_check_ready(), TAG, "display unavailable");
    ESP_RETURN_ON_ERROR(cyd_display_check_owner(), TAG, "display owner required");
    ESP_RETURN_ON_FALSE(cmd != nullptr, ESP_ERR_INVALID_ARG, TAG, "log command required");

    if (xQueueSendToBack(s_display_log_queue, cmd, 0) == pdTRUE) {
        return ESP_OK;
    }

    cyd_display_log_cmd_t dropped = {};
    (void)xQueueReceive(s_display_log_queue, &dropped, 0);
    ESP_RETURN_ON_FALSE(xQueueSendToBack(s_display_log_queue, cmd, 0) == pdTRUE,
                        ESP_ERR_TIMEOUT,
                        TAG,
                        "display log queue full");
    return ESP_OK;
}

static void cyd_display_log_stack_usage(void)
{
    APP_STACK_MONITOR_CHECK(TAG, "cyd_display", DISPLAY_STACK_LOG_INTERVAL_MS);
}

static void cyd_display_task(void *arg)
{
    (void)arg;

    while (true) {
        QueueSetMemberHandle_t activated = xQueueSelectFromSet(s_display_queue_set, portMAX_DELAY);
        if (activated == s_display_queue) {
            if (xQueueReceive(s_display_queue, &s_current_screen, 0) == pdTRUE) {
                cyd_display_apply_screen(s_current_screen);
                cyd_display_log_stack_usage();
            }
            continue;
        }

        if (activated == s_display_log_queue) {
            cyd_display_log_cmd_t cmd = {};
            if (xQueueReceive(s_display_log_queue, &cmd, 0) == pdTRUE) {
                cyd_display_handle_log_cmd(cmd);
                cyd_display_log_stack_usage();
            }
            continue;
        }
    }
}

}  // namespace

extern "C" bool cyd_display_touch_to_grid(int16_t x, int16_t y, uint8_t *col, uint8_t *row)
{
    if (x < 0 || y < 0 || x >= s_display.width() || y >= s_display.height()) {
        return false;
    }

    if (col != nullptr) {
        *col = static_cast<uint8_t>(x / GRID_CELL_PX);
    }
    if (row != nullptr) {
        *row = static_cast<uint8_t>(y / GRID_CELL_PX);
    }
    return true;
}

extern "C" bool cyd_display_hit_test_action(int16_t x, int16_t y, uint16_t *action_id)
{
    uint8_t col = 0;
    uint8_t row = 0;
    if (!cyd_display_touch_to_grid(x, y, &col, &row)) {
        return false;
    }

    for (size_t i = 0; i < s_current_screen.widget_count; ++i) {
        const cyd_display_widget_t &widget = s_current_screen.widgets[i];
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

extern "C" bool cyd_display_get_mode_button_grid_rect(size_t index, cyd_display_grid_rect_t *rect)
{
    if (rect == nullptr || index >= s_mode_button_count) {
        return false;
    }

    *rect = s_mode_button_rects[index];
    return true;
}

extern "C" bool cyd_display_hit_test_mode_button(int16_t x, int16_t y, size_t *button_index)
{
    uint8_t col = 0;
    uint8_t row = 0;
    if (!cyd_display_touch_to_grid(x, y, &col, &row)) {
        return false;
    }

    for (size_t i = 0; i < s_mode_button_count; ++i) {
        const cyd_display_grid_rect_t &rect = s_mode_button_rects[i];
        if (col >= rect.col && col < (rect.col + rect.width) &&
            row >= rect.row && row < (rect.row + rect.height)) {
            if (button_index != nullptr) {
                *button_index = i;
            }
            return true;
        }
    }

    return false;
}

extern "C" bool cyd_display_get_mode_button_bounds(size_t button_count,
                                                   size_t index,
                                                   int32_t *x,
                                                   int32_t *y,
                                                   int32_t *w,
                                                   int32_t *h)
{
    cyd_display_grid_rect_t rect = cyd_display_make_empty_grid_rect();
    if (!cyd_display_mode_button_rect_for_count(button_count, index, &rect)) {
        return false;
    }

    if (x != nullptr) {
        *x = cyd_display_col_to_px(rect.col);
    }
    if (y != nullptr) {
        *y = cyd_display_row_to_px(rect.row);
    }
    if (w != nullptr) {
        *w = static_cast<int32_t>(rect.width) * GRID_CELL_PX;
    }
    if (h != nullptr) {
        *h = static_cast<int32_t>(rect.height) * GRID_CELL_PX;
    }

    return true;
}

extern "C" esp_err_t cyd_display_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }

    uint8_t brightness = CONFIG_CYD_DISPLAY_BACKLIGHT_BRIGHTNESS;
    esp_err_t brightness_err = cyd_display_load_brightness_blob(&brightness);
    if (brightness_err != ESP_OK && brightness_err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "brightness load failed: %s", esp_err_to_name(brightness_err));
    }
    s_backlight_brightness = brightness;

    s_display.init();
    s_display.setRotation(CONFIG_CYD_DISPLAY_ROTATION);
    s_display.setBrightness(s_backlight_brightness);
    s_display.fillScreen(TFT_BLACK);
    cyd_display_clear_mode_button_map();
    s_has_previous_screen = false;
    s_strip_sprite.setColorDepth(16);
    ESP_RETURN_ON_FALSE(s_display.width() > 0 && s_display.height() > 0,
                        ESP_ERR_INVALID_SIZE,
                        TAG,
                        "invalid display size");
    ESP_RETURN_ON_FALSE(s_strip_sprite.createSprite(s_display.width(), STRIP_HEIGHT_PX) != nullptr,
                        ESP_ERR_NO_MEM,
                        TAG,
                        "strip sprite alloc failed");
    s_strip_sprite_ready = true;
    s_strip_count = (static_cast<size_t>(s_display.height()) + STRIP_HEIGHT_PX - 1) / STRIP_HEIGHT_PX;
    s_display_queue = xQueueCreate(DISPLAY_QUEUE_LENGTH, sizeof(cyd_display_screen_t));
    ESP_RETURN_ON_FALSE(s_display_queue != nullptr, ESP_ERR_NO_MEM, TAG, "display queue alloc failed");
    s_display_log_queue = xQueueCreate(DISPLAY_LOG_QUEUE_LENGTH, sizeof(cyd_display_log_cmd_t));
    ESP_RETURN_ON_FALSE(s_display_log_queue != nullptr, ESP_ERR_NO_MEM, TAG, "display log queue alloc failed");
    s_display_queue_set = xQueueCreateSet(DISPLAY_QUEUE_LENGTH + DISPLAY_LOG_QUEUE_LENGTH);
    ESP_RETURN_ON_FALSE(s_display_queue_set != nullptr, ESP_ERR_NO_MEM, TAG, "display queue set alloc failed");
    ESP_RETURN_ON_FALSE(xQueueAddToSet(s_display_queue, s_display_queue_set) == pdPASS,
                        ESP_ERR_INVALID_STATE,
                        TAG,
                        "display queue set add failed");
    ESP_RETURN_ON_FALSE(xQueueAddToSet(s_display_log_queue, s_display_queue_set) == pdPASS,
                        ESP_ERR_INVALID_STATE,
                        TAG,
                        "display log queue set add failed");
    BaseType_t task_ok = xTaskCreate(cyd_display_task,
                                     "cyd_display",
                                     DISPLAY_TASK_STACK_SIZE,
                                     nullptr,
                                     4,
                                     &s_display_task_handle);
    ESP_RETURN_ON_FALSE(task_ok == pdPASS, ESP_ERR_NO_MEM, TAG, "display task create failed");
    s_initialized = true;

    ESP_LOGI(TAG,
             "CYD display configured: panel=%s TFT SCLK=%d MOSI=%d MISO=%d DC=%d CS=%d BL=%d rot=%d off=(%d,%d) rot_off=%d inv=%d rgb=%d read=%d",
             cyd_display_panel_name(),
             CONFIG_CYD_DISPLAY_PIN_SCLK,
             CONFIG_CYD_DISPLAY_PIN_MOSI,
             CONFIG_CYD_DISPLAY_PIN_MISO,
             CONFIG_CYD_DISPLAY_PIN_DC,
             CONFIG_CYD_DISPLAY_PIN_CS,
             CONFIG_CYD_DISPLAY_BACKLIGHT_GPIO,
             CONFIG_CYD_DISPLAY_ROTATION,
             CONFIG_CYD_DISPLAY_OFFSET_X,
             CONFIG_CYD_DISPLAY_OFFSET_Y,
             CONFIG_CYD_DISPLAY_OFFSET_ROTATION,
             CYD_DISPLAY_INVERT,
             CYD_DISPLAY_RGB_ORDER,
             CYD_DISPLAY_READABLE);

    return ESP_OK;
}

extern "C" esp_err_t cyd_display_set_brightness(uint8_t brightness)
{
    ESP_RETURN_ON_FALSE(s_initialized, ESP_ERR_INVALID_STATE, TAG, "display not initialized");

    s_display.setBrightness(brightness);
    s_backlight_brightness = brightness;
    return ESP_OK;
}

extern "C" esp_err_t cyd_display_save_brightness(void)
{
    ESP_RETURN_ON_FALSE(s_initialized, ESP_ERR_INVALID_STATE, TAG, "display not initialized");

    esp_err_t err = cyd_display_write_brightness(s_backlight_brightness);
    ESP_RETURN_ON_ERROR(err, TAG, "save brightness failed");
    return ESP_OK;
}

extern "C" uint8_t cyd_display_get_brightness(void)
{
    return s_backlight_brightness;
}

extern "C" esp_err_t cyd_display_claim_owner(void)
{
    ESP_RETURN_ON_ERROR(cyd_display_check_ready(), TAG, "display unavailable");

    TaskHandle_t current_task = xTaskGetCurrentTaskHandle();
    ESP_RETURN_ON_FALSE(current_task != nullptr, ESP_ERR_INVALID_STATE, TAG, "current task unavailable");

    if (s_display_owner_task_handle == nullptr || s_display_owner_task_handle == current_task) {
        s_display_owner_task_handle = current_task;
        return ESP_OK;
    }

    ESP_LOGW(TAG, "display owner already claimed by another task");
    return ESP_ERR_INVALID_STATE;
}

extern "C" esp_err_t cyd_display_release_owner(void)
{
    ESP_RETURN_ON_ERROR(cyd_display_check_ready(), TAG, "display unavailable");

    if (s_display_owner_task_handle == nullptr) {
        return ESP_OK;
    }

    TaskHandle_t current_task = xTaskGetCurrentTaskHandle();
    ESP_RETURN_ON_FALSE(current_task == s_display_owner_task_handle,
                        ESP_ERR_INVALID_STATE,
                        TAG,
                        "display owner mismatch");
    s_display_owner_task_handle = nullptr;
    return ESP_OK;
}

extern "C" esp_err_t cyd_display_submit_screen(const cyd_display_screen_t *screen)
{
    ESP_RETURN_ON_ERROR(cyd_display_check_ready(), TAG, "display unavailable");
    ESP_RETURN_ON_ERROR(cyd_display_check_owner(), TAG, "display owner required");
    ESP_RETURN_ON_FALSE(screen != nullptr, ESP_ERR_INVALID_ARG, TAG, "screen required");

    if (xQueueSendToBack(s_display_queue, screen, 0) == pdTRUE) {
        return ESP_OK;
    }

    cyd_display_screen_t dropped = {};
    (void)xQueueReceive(s_display_queue, &dropped, 0);
    ESP_RETURN_ON_FALSE(xQueueSendToBack(s_display_queue, screen, 0) == pdTRUE,
                        ESP_ERR_TIMEOUT,
                        TAG,
                        "display queue full");
    return ESP_OK;
}

extern "C" esp_err_t cyd_display_log_show(const char *title)
{
    cyd_display_log_cmd_t cmd = cyd_display_make_log_cmd(CYD_DISPLAY_LOG_CMD_SHOW, 0);
    cyd_display_copy_text(cmd.text, sizeof(cmd.text), title != nullptr ? title : "Log");
    return cyd_display_submit_log_cmd(&cmd);
}

extern "C" esp_err_t cyd_display_log_hide(void)
{
    cyd_display_log_cmd_t cmd = cyd_display_make_log_cmd(CYD_DISPLAY_LOG_CMD_HIDE, 0);
    return cyd_display_submit_log_cmd(&cmd);
}

extern "C" esp_err_t cyd_display_log_clear(void)
{
    cyd_display_log_cmd_t cmd = cyd_display_make_log_cmd(CYD_DISPLAY_LOG_CMD_CLEAR, 0);
    return cyd_display_submit_log_cmd(&cmd);
}

extern "C" esp_err_t cyd_display_log_push(const char *line)
{
    ESP_RETURN_ON_FALSE(line != nullptr, ESP_ERR_INVALID_ARG, TAG, "log line required");
    cyd_display_log_cmd_t cmd = cyd_display_make_log_cmd(CYD_DISPLAY_LOG_CMD_PUSH, 0);
    cyd_display_copy_text(cmd.text, sizeof(cmd.text), line);
    return cyd_display_submit_log_cmd(&cmd);
}

extern "C" esp_err_t cyd_display_log_scroll(int delta)
{
    cyd_display_log_cmd_t cmd = cyd_display_make_log_cmd(CYD_DISPLAY_LOG_CMD_SCROLL, delta);
    return cyd_display_submit_log_cmd(&cmd);
}

extern "C" esp_err_t cyd_display_show_touch_calibration_screen(void)
{
    ESP_RETURN_ON_ERROR(cyd_display_check_ready(), TAG, "display unavailable");
    ESP_RETURN_ON_ERROR(cyd_display_check_owner(), TAG, "display owner required");

    s_display.fillScreen(TFT_BLACK);
    cyd_display_draw_centered_line(s_display,
                                   "Touch Calibration",
                                   s_display.width() / 2,
                                   s_display.height() / 2 - 18,
                                   2,
                                   TFT_YELLOW);
    cyd_display_draw_centered_line(s_display,
                                   "Tap the 4 targets",
                                   s_display.width() / 2,
                                   s_display.height() / 2 + 18,
                                   1,
                                   TFT_WHITE);
    return ESP_OK;
}

extern "C" esp_err_t cyd_display_draw_touch_calibration_target(int32_t x, int32_t y, uint8_t radius, bool visible)
{
    ESP_RETURN_ON_ERROR(cyd_display_check_ready(), TAG, "display unavailable");
    ESP_RETURN_ON_ERROR(cyd_display_check_owner(), TAG, "display owner required");
    ESP_RETURN_ON_FALSE(radius > 0, ESP_ERR_INVALID_ARG, TAG, "radius required");

    cyd_display_draw_calibration_marker(s_display,
                                        x,
                                        y,
                                        radius,
                                        visible ? TFT_YELLOW : TFT_BLACK,
                                        TFT_BLACK);
    return ESP_OK;
}

extern "C" esp_err_t cyd_display_invalidate(void)
{
    ESP_RETURN_ON_ERROR(cyd_display_check_ready(), TAG, "display unavailable");
    s_has_previous_screen = false;
    return ESP_OK;
}

extern "C" int32_t cyd_display_get_width(void)
{
    return s_display.width();
}

extern "C" int32_t cyd_display_get_height(void)
{
    return s_display.height();
}

extern "C" esp_err_t cyd_display_show_text(const char *title, const char *message)
{
    ESP_RETURN_ON_ERROR(cyd_display_check_ready(), TAG, "display unavailable");
    cyd_display_screen_t screen = cyd_display_make_empty_screen();
    cyd_display_widget_t title_widget = cyd_display_make_widget(CYD_DISPLAY_WIDGET_TEXT);
    title_widget.col = 0;
    title_widget.row = 11;
    title_widget.span_cols = CYD_DISPLAY_GRID_COLS;
    title_widget.span_rows = 2;
    title_widget.align = CYD_DISPLAY_ALIGN_CENTER;
    title_widget.scale_x = 2;
    title_widget.scale_y = 2;
    title_widget.fg_color = TFT_YELLOW;
    title_widget.bg_color = TFT_BLACK;
    cyd_display_widget_t message_widget = cyd_display_make_widget(CYD_DISPLAY_WIDGET_TEXT);
    message_widget.col = 0;
    message_widget.row = 17;
    message_widget.span_cols = CYD_DISPLAY_GRID_COLS;
    message_widget.span_rows = 1;
    message_widget.align = CYD_DISPLAY_ALIGN_CENTER;
    message_widget.scale_x = 1;
    message_widget.scale_y = 1;
    message_widget.fg_color = TFT_WHITE;
    message_widget.bg_color = TFT_BLACK;

    cyd_display_copy_text(title_widget.text, sizeof(title_widget.text), title);
    cyd_display_copy_text(message_widget.text, sizeof(message_widget.text), message);
    cyd_display_add_widget(&screen, &title_widget);
    cyd_display_add_widget(&screen, &message_widget);
    return cyd_display_submit_screen(&screen);
}

extern "C" esp_err_t cyd_display_show_lines(const char *title, const char *const *lines, size_t line_count)
{
    ESP_RETURN_ON_ERROR(cyd_display_check_ready(), TAG, "display unavailable");
    cyd_display_screen_t screen = cyd_display_make_empty_screen();
    cyd_display_widget_t title_widget = cyd_display_make_widget(CYD_DISPLAY_WIDGET_TEXT);
    title_widget.col = 0;
    title_widget.row = SCREEN_TITLE_ROW;
    title_widget.span_cols = CYD_DISPLAY_GRID_COLS;
    title_widget.span_rows = 2;
    title_widget.align = CYD_DISPLAY_ALIGN_CENTER;
    title_widget.scale_x = 2;
    title_widget.scale_y = 2;
    title_widget.fg_color = TFT_YELLOW;
    title_widget.bg_color = TFT_BLACK;
    cyd_display_copy_text(title_widget.text, sizeof(title_widget.text), title);
    cyd_display_add_widget(&screen, &title_widget);

    size_t visible_count = line_count > SCREEN_MAX_LINE_COUNT ? SCREEN_MAX_LINE_COUNT : line_count;
    if (visible_count == 0 || lines == nullptr) {
        cyd_display_widget_t empty_widget = cyd_display_make_widget(CYD_DISPLAY_WIDGET_TEXT);
        empty_widget.col = 0;
        empty_widget.row = 17;
        empty_widget.span_cols = CYD_DISPLAY_GRID_COLS;
        empty_widget.span_rows = 1;
        empty_widget.align = CYD_DISPLAY_ALIGN_CENTER;
        empty_widget.scale_x = 1;
        empty_widget.scale_y = 1;
        empty_widget.fg_color = TFT_WHITE;
        empty_widget.bg_color = TFT_BLACK;
        cyd_display_copy_text(empty_widget.text, sizeof(empty_widget.text), "No data");
        cyd_display_add_widget(&screen, &empty_widget);
        return cyd_display_submit_screen(&screen);
    }

    for (size_t i = 0; i < visible_count; ++i) {
        cyd_display_widget_t line_widget = cyd_display_make_widget(CYD_DISPLAY_WIDGET_TEXT);
        line_widget.col = 1;
        line_widget.row = static_cast<uint8_t>(SCREEN_FIRST_LINE_ROW + i * SCREEN_LINE_HEIGHT_ROWS);
        line_widget.span_cols = static_cast<uint8_t>(CYD_DISPLAY_GRID_COLS - 2);
        line_widget.span_rows = 1;
        line_widget.align = CYD_DISPLAY_ALIGN_LEFT;
        line_widget.scale_x = 1;
        line_widget.scale_y = 1;
        line_widget.fg_color = TFT_WHITE;
        line_widget.bg_color = TFT_BLACK;
        cyd_display_copy_text(line_widget.text, sizeof(line_widget.text), lines[i]);
        cyd_display_add_widget(&screen, &line_widget);
    }

    return cyd_display_submit_screen(&screen);
}

extern "C" esp_err_t cyd_display_show_mode_screen(const char *title,
                                                  const char *const *lines,
                                                  size_t line_count,
                                                  const char *const *buttons,
                                                  size_t button_count,
                                                  size_t selected_idx)
{
    ESP_RETURN_ON_ERROR(cyd_display_check_ready(), TAG, "display unavailable");
    cyd_display_screen_t screen = cyd_display_make_empty_screen();
    cyd_display_widget_t title_widget = cyd_display_make_widget(CYD_DISPLAY_WIDGET_TEXT);
    title_widget.col = 0;
    title_widget.row = SCREEN_TITLE_ROW;
    title_widget.span_cols = CYD_DISPLAY_GRID_COLS;
    title_widget.span_rows = 2;
    title_widget.align = CYD_DISPLAY_ALIGN_CENTER;
    title_widget.scale_x = 2;
    title_widget.scale_y = 2;
    title_widget.fg_color = TFT_YELLOW;
    title_widget.bg_color = TFT_BLACK;
    cyd_display_copy_text(title_widget.text, sizeof(title_widget.text), title);
    cyd_display_add_widget(&screen, &title_widget);

    size_t visible_line_count = line_count > CYD_DISPLAY_MAX_STATUS_LINES ? CYD_DISPLAY_MAX_STATUS_LINES : line_count;
    if (visible_line_count == 0 || lines == nullptr) {
        cyd_display_widget_t empty_widget = cyd_display_make_widget(CYD_DISPLAY_WIDGET_TEXT);
        empty_widget.col = 0;
        empty_widget.row = 14;
        empty_widget.span_cols = CYD_DISPLAY_GRID_COLS;
        empty_widget.span_rows = 1;
        empty_widget.align = CYD_DISPLAY_ALIGN_CENTER;
        empty_widget.scale_x = 1;
        empty_widget.scale_y = 1;
        empty_widget.fg_color = TFT_WHITE;
        empty_widget.bg_color = TFT_BLACK;
        cyd_display_copy_text(empty_widget.text, sizeof(empty_widget.text), "No data");
        cyd_display_add_widget(&screen, &empty_widget);
    } else {
        for (size_t i = 0; i < visible_line_count; ++i) {
            cyd_display_widget_t line_widget = cyd_display_make_widget(CYD_DISPLAY_WIDGET_TEXT);
            line_widget.col = 1;
            line_widget.row = static_cast<uint8_t>(SCREEN_FIRST_LINE_ROW + i * SCREEN_LINE_HEIGHT_ROWS);
            line_widget.span_cols = static_cast<uint8_t>(CYD_DISPLAY_GRID_COLS - 2);
            line_widget.span_rows = 1;
            line_widget.align = CYD_DISPLAY_ALIGN_LEFT;
            line_widget.scale_x = 1;
            line_widget.scale_y = 1;
            line_widget.fg_color = TFT_WHITE;
            line_widget.bg_color = TFT_BLACK;
            cyd_display_copy_text(line_widget.text, sizeof(line_widget.text), lines[i]);
            cyd_display_add_widget(&screen, &line_widget);
        }
    }

    size_t visible_button_count = button_count > CYD_DISPLAY_MAX_MODE_BUTTONS ? CYD_DISPLAY_MAX_MODE_BUTTONS : button_count;
    for (size_t i = 0; i < visible_button_count; ++i) {
        cyd_display_grid_rect_t rect = cyd_display_make_empty_grid_rect();
        if (!cyd_display_mode_button_rect_for_count(visible_button_count, i, &rect)) {
            continue;
        }

        cyd_display_widget_t button_widget = cyd_display_make_widget(CYD_DISPLAY_WIDGET_BUTTON);
        button_widget.col = rect.col;
        button_widget.row = rect.row;
        button_widget.span_cols = rect.width;
        button_widget.span_rows = rect.height;
        button_widget.align = CYD_DISPLAY_ALIGN_CENTER;
        button_widget.scale_x = 1;
        button_widget.scale_y = 1;
        button_widget.fg_color = TFT_WHITE;
        button_widget.bg_color = static_cast<uint16_t>((i == selected_idx) ? TFT_BLUE : TFT_DARKGREY);
        button_widget.border_color = static_cast<uint16_t>((i == selected_idx) ? TFT_CYAN : TFT_LIGHTGREY);
        button_widget.action_id = static_cast<uint16_t>(i);
        button_widget.enabled = true;
        cyd_display_copy_text(button_widget.text, sizeof(button_widget.text), buttons != nullptr ? buttons[i] : "");
        cyd_display_add_widget(&screen, &button_widget);
    }

    return cyd_display_submit_screen(&screen);
}

extern "C" esp_err_t cyd_display_show_boot_screen(void)
{
    return cyd_display_show_text("CYD Display OK", "LovyanGFX initialized");
}
