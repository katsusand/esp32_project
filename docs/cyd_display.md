# CYD Display Driver

## Overview

`cyd_display` は、CYD の TFT 画面表示だけを扱う表示コンポーネントです。

内部では LovyanGFX を使って LCD を初期化し、表示要求を FreeRTOS キューに積んで、専用の表示タスクで描画します。アプリ側は、画面全体を表す `cyd_display_screen_t` を送るか、便利関数でテキスト画面、モード画面、ログビューを表示します。

English supplement: This component owns the display queue, render task, LovyanGFX device, and dirty-strip rendering state. Touch transport and calibration state are intentionally owned outside this component.

## Public API

利用するファイルでは、次のヘッダーを include します。

```c
#include "cyd_display.h"
```

初期化は、通常 `main/app_main()` から入る `system_boot_start()` の起動処理で一度だけ呼びます。

```c
ESP_ERROR_CHECK(cyd_display_init());
```

起動確認用の画面を表示する場合は `cyd_display_show_boot_screen()` を使います。

```c
ESP_ERROR_CHECK(cyd_display_show_boot_screen());
```

タイトルと本文だけの簡単な画面は `cyd_display_show_text()` で表示できます。

```c
ESP_ERROR_CHECK(cyd_display_show_text("CYD", "Hello World"));
```

複数行のテキストを表示する場合は `cyd_display_show_lines()` を使います。

```c
const char *lines[] = {
    "Wi-Fi: connected",
    "ESP-NOW: ready",
    "Touch: enabled",
};

ESP_ERROR_CHECK(cyd_display_show_lines("Status", lines, sizeof(lines) / sizeof(lines[0])));
```

ボタン付きのモード画面を表示する場合は `cyd_display_show_mode_screen()` を使います。

```c
const char *lines[] = {
    "Select mode",
};
const char *buttons[] = {
    "A",
    "B",
    "C",
};

ESP_ERROR_CHECK(cyd_display_show_mode_screen(
    "Mode",
    lines,
    sizeof(lines) / sizeof(lines[0]),
    buttons,
    sizeof(buttons) / sizeof(buttons[0]),
    0));
```

細かく配置した画面を出したい場合は `cyd_display_screen_t` を作り、`cyd_display_submit_screen()` で送ります。

```c
cyd_display_screen_t screen = { 0 };

screen.widgets[0] = (cyd_display_widget_t) {
    .type = CYD_DISPLAY_WIDGET_TEXT,
    .col = 1,
    .row = 1,
    .span_cols = 20,
    .span_rows = 2,
    .align = CYD_DISPLAY_ALIGN_LEFT,
    .scale_x = 1,
    .scale_y = 1,
    .fg_color = 0xffff,
    .bg_color = 0x0000,
};
snprintf(screen.widgets[0].text, sizeof(screen.widgets[0].text), "Hello");
screen.widget_count = 1;

ESP_ERROR_CHECK(cyd_display_submit_screen(&screen));
```

English supplement: `cyd_display_submit_screen()` copies the whole `cyd_display_screen_t` into the queue item. The caller may reuse or discard the local screen after the function returns.

ログを追記表示する場合は、ログビューAPIを使います。

```c
ESP_ERROR_CHECK(cyd_display_log_show("System Log"));
ESP_ERROR_CHECK(cyd_display_log_push("Wi-Fi connected"));
ESP_ERROR_CHECK(cyd_display_log_push("ESP-NOW ready"));
```

ログビューを操作するAPIは以下です。

- `cyd_display_log_show()`: ログビューを表示する
- `cyd_display_log_hide()`: ログビューを閉じて画面を空にする
- `cyd_display_log_clear()`: 内部リングバッファを空にする
- `cyd_display_log_push()`: 1行追加する
- `cyd_display_log_scroll()`: 表示位置を上下にずらす。正の値で古い行へ、負の値で新しい行へ戻る

English supplement: Log view commands are small queue messages. The log lines are stored in a fixed-size ring buffer owned by `cyd_display`.

## Screen Model

画面は `cyd_display_screen_t` で表します。

```c
typedef struct {
    uint8_t widget_count;
    cyd_display_widget_t widgets[CYD_DISPLAY_MAX_WIDGETS];
} cyd_display_screen_t;
```

現在の最大ウィジェット数は `CYD_DISPLAY_MAX_WIDGETS`、つまり `48` です。テキスト行、ボタン、ログビューを同じ画面モデルで扱えるように固定長配列で保持します。

各ウィジェットは `cyd_display_widget_t` です。

```c
typedef struct {
    cyd_display_widget_type_t type;
    uint8_t col;
    uint8_t row;
    uint8_t span_cols;
    uint8_t span_rows;
    cyd_display_align_t align;
    uint8_t scale_x;
    uint8_t scale_y;
    uint16_t fg_color;
    uint16_t bg_color;
    uint16_t border_color;
    uint16_t action_id;
    bool enabled;
    const cyd_display_bitmap_t *bitmap;
    char text[CYD_DISPLAY_TEXT_MAX_LEN + 1];
} cyd_display_widget_t;
```

現在使える主なウィジェット種別は以下です。

- `CYD_DISPLAY_WIDGET_TEXT`: テキスト
- `CYD_DISPLAY_WIDGET_BUTTON`: ボタン
- `CYD_DISPLAY_WIDGET_ICON`: ビットマップアイコン

テキストは `CYD_DISPLAY_TEXT_MAX_LEN`、つまり 40 文字までです。超える場合は呼び出し側で短くしてください。

`CYD_DISPLAY_WIDGET_BUTTON` で `enabled=false` の場合、描画はされますが `cyd_display_hit_test_action()` とモードボタンマップの対象から外れます。無効状態の色は呼び出し側が指定します。

English supplement: Widget order is significant for dirty-rect comparison. Keep stable widget ordering between frames when updating only text or colors.

## Grid Layout

このドライバーは 8 px 単位のグリッドを使います。

- `CYD_DISPLAY_GRID_COLS=40`
- `CYD_DISPLAY_GRID_ROWS=30`
- `CYD_DISPLAY_GRID_CELL_PX=8`

`col` と `row` はグリッド座標です。たとえば `col=1`、`row=2` は、画面上ではおおよそ X=8 px、Y=16 px の位置になります。

`span_cols` と `span_rows` はウィジェットの幅と高さをグリッド単位で表します。

English supplement: The logical grid assumes a 320x240 landscape layout. If display rotation or panel configuration changes, verify that the grid still matches the expected visible orientation.

## Queue Model

`cyd_display` は内部に FreeRTOS キューを持ちます。通常画面用キューの長さは `3`、ログコマンド用キューの長さは `8` です。

`cyd_display_submit_screen()` は表示要求をキューへ送ります。キューが満杯の場合は、古い表示要求を1つ捨ててから新しい表示要求を積み直します。

この挙動により、表示更新が詰まった場合でも、古い画面を順番に全部描くより、できるだけ新しい画面へ追従します。

ログビューAPIは、小さいログコマンドを別キューへ送ります。表示タスクは FreeRTOS Queue Set で通常画面キューとログコマンドキューを同時に待ちます。

English supplement: The display screen queue is latest-state oriented. The log command queue carries small commands such as push, clear, show, hide, and scroll.

## Log View

ログビューは、`cyd_display` 内部の固定長リングバッファを使います。

- 最大保持行数: `CYD_DISPLAY_LOG_MAX_LINES`、現在は `32`
- 1行の最大文字数: `CYD_DISPLAY_TEXT_MAX_LEN`、現在は `40`
- 表示可能行数: タイトル下のグリッド行数と残りウィジェット数の小さい方。現在は最大 `27` 行

`cyd_display_log_push()` は、リングバッファへ1行追加します。バッファが満杯の場合は、最も古い行が上書きされます。

新しい行を追加すると、表示位置は最新行へ戻ります。古い行を見たい場合は `cyd_display_log_scroll()` を使います。

English supplement: `cyd_display_log_push()` keeps the view tailing the newest line. A scroll command changes `scroll_offset`, but the next push returns the view to the newest lines.

## Render Behavior

表示タスクはキューから画面を受け取り、前回画面との差分を調べます。

初回表示では画面全体を描画します。2回目以降は、変更されたウィジェットの矩形から dirty rect を作り、16 px 高の strip sprite に描いて LCD へ転送します。

この仕組みにより、毎回全画面を描き直すよりも描画量を抑えます。

English supplement: Dirty rendering relies on comparing current and previous widget structs. Avoid leaving uninitialized bytes in widgets because they may cause unnecessary redraws.

## Hit Test Helpers

`cyd_display` は、画面上の座標に対する hit-test helper を提供します。低レベルのタッチ読み取り自体は `cyd_input` と `xpt2046_softspi` 側の責務です。

グリッド座標へ変換する場合は `cyd_display_touch_to_grid()` を使います。

```c
uint8_t col = 0;
uint8_t row = 0;

if (cyd_display_touch_to_grid(x, y, &col, &row)) {
    printf("grid col=%u row=%u\n", col, row);
}
```

モード画面のボタン判定には以下を使えます。

- `cyd_display_hit_test_action()`
- `cyd_display_hit_test_mode_button()`
- `cyd_display_get_mode_button_grid_rect()`
- `cyd_display_get_mode_button_bounds()`

`cyd_display_hit_test_action()` は、現在画面の有効な `CYD_DISPLAY_WIDGET_BUTTON` を対象に、タッチ座標が含まれるボタンの `action_id` を返します。カスタム画面でボタンごとに独自 action id を割り当てた場合に使います。

`cyd_display_get_mode_button_grid_rect()` と `cyd_display_hit_test_mode_button()` は、最後に送信されたモード画面またはボタンウィジェットの内部マップを参照します。

English supplement: Generic action hit testing reads button widgets from the current screen. Mode button hit testing uses the most recently rendered button map maintained by the display component.

## Calibration Drawing Helpers

タッチ補正フローそのものは `cyd_input` が担当しますが、補正ターゲットの描画は `cyd_display` が行います。

```c
ESP_ERROR_CHECK(cyd_display_claim_owner());
ESP_ERROR_CHECK(cyd_display_show_touch_calibration_screen());
ESP_ERROR_CHECK(cyd_display_draw_touch_calibration_target(0, 0, 14, true));
ESP_ERROR_CHECK(cyd_display_draw_touch_calibration_target(0, 0, 14, false));
ESP_ERROR_CHECK(cyd_display_invalidate());
ESP_ERROR_CHECK(cyd_display_release_owner());
```

補助 API は以下です。

- `cyd_display_show_touch_calibration_screen()`: 補正導入画面を即時描画する
- `cyd_display_draw_touch_calibration_target()`: 1点ぶんのターゲットを即時描画または消去する
- `cyd_display_invalidate()`: 次の通常画面を full redraw させる
- `cyd_display_get_width()` / `cyd_display_get_height()`: 現在の論理表示サイズを返す

English supplement: These helpers are display-only primitives used by `cyd_input_run_touch_calibration()`. They do not read the touch controller or store calibration data.

## Brightness

バックライトの現在値を参照する場合は `cyd_display_get_brightness()` を使います。

```c
uint8_t brightness = cyd_display_get_brightness();
```

バックライトを変更する場合は `cyd_display_set_brightness()` を使います。

```c
ESP_ERROR_CHECK(cyd_display_set_brightness(160));
```

変更後の値を不揮発保存したい場合は `cyd_display_save_brightness()` を呼びます。

```c
ESP_ERROR_CHECK(cyd_display_save_brightness());
```

設定値は 0 から 255 の範囲で扱います。`cyd_display_init()` は、保存済みの値があれば NVS から読み込み、なければ `CONFIG_CYD_DISPLAY_BACKLIGHT_BRIGHTNESS` を初期値として使います。

English supplement: `cyd_display_set_brightness()` applies the change immediately in RAM and on hardware. Persist it explicitly with `cyd_display_save_brightness()` when the UI decides the change is final.

## Calibration

通常の補正フローは `cyd_input_run_touch_calibration()` を使います。`cyd_display` はその中で target 描画だけを担当します。

注意:

- touch transport は `xpt2046_softspi` が所有する
- 補正 4点の raw 取得、affine 計算、NVS 保存/復元は `cyd_input` が所有する
- `cyd_display` は display-only を保つため、補正値の保存や touch controller 読み取りを行わない

English supplement: Keep touch transport and calibration math out of `cyd_display`. The display component should remain reusable for display-only products.

## Configuration

主な設定項目は `idf.py menuconfig` の `CYD Display` から変更できます。

- `CONFIG_CYD_DISPLAY_PANEL_ILI9341`: ILI9341 パネルを使う
- `CONFIG_CYD_DISPLAY_PANEL_ILI9341_2`: ILI9341 variant 2 を使う
- `CONFIG_CYD_DISPLAY_PANEL_ST7789`: ST7789 パネルを使う
- `CONFIG_CYD_DISPLAY_ROTATION`: 表示回転
- `CONFIG_CYD_DISPLAY_BACKLIGHT_GPIO`: バックライト GPIO
- `CONFIG_CYD_DISPLAY_BACKLIGHT_BRIGHTNESS`: バックライト明るさ
- `CONFIG_CYD_DISPLAY_SPI_HOST`: TFT の SPI host
- `CONFIG_CYD_DISPLAY_PIN_SCLK`: TFT SCLK GPIO
- `CONFIG_CYD_DISPLAY_PIN_MOSI`: TFT MOSI GPIO
- `CONFIG_CYD_DISPLAY_PIN_MISO`: TFT MISO GPIO
- `CONFIG_CYD_DISPLAY_PIN_DC`: TFT DC GPIO
- `CONFIG_CYD_DISPLAY_PIN_CS`: TFT CS GPIO
- `CONFIG_CYD_DISPLAY_PIN_RST`: TFT RST GPIO
- `CONFIG_CYD_DISPLAY_OFFSET_X`: TFT X offset
- `CONFIG_CYD_DISPLAY_OFFSET_Y`: TFT Y offset
- `CONFIG_CYD_DISPLAY_OFFSET_ROTATION`: TFT rotation offset
- `CONFIG_CYD_DISPLAY_RGB_ORDER`: 赤青が入れ替わる場合に有効化
- `CONFIG_CYD_DISPLAY_INVERT`: 色反転が必要な場合に有効化
- `CONFIG_CYD_DISPLAY_READABLE`: SPI readback 対応の有無

English supplement: Clone boards may require different panel type, RGB order, inversion, readback, or rotation settings even when the board name looks the same.
