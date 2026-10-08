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

`show_text()`、`show_lines()`、`show_mode_screen()` は、呼び出し側の `cyd_display_screen_t` に画面を組み立ててから送ります。app はその buffer でタッチを判定するので（[Hit Testing](#hit-testing)）、表示中の画面と判定に使う画面が常に一致します。app が持っている画面 buffer をそのまま渡してください。

```c
static cyd_display_screen_t s_screen;

ESP_ERROR_CHECK(cyd_display_show_text(&s_screen, "CYD", "Hello World"));
```

複数行のテキストを表示する場合は `cyd_display_show_lines()` を使います。

```c
const char *lines[] = {
    "Wi-Fi: connected",
    "ESP-NOW: ready",
    "Touch: enabled",
};

ESP_ERROR_CHECK(cyd_display_show_lines(&s_screen, "Status", lines, sizeof(lines) / sizeof(lines[0])));
```

ボタン付きのモード画面を表示する場合は `cyd_display_show_mode_screen()` を使います。ボタンの `action_id` は先頭から `0`、`1`、`2` … です。

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
    &s_screen,
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
    uint8_t type;          /* cyd_display_widget_type_t */
    uint8_t col;
    uint8_t row;
    uint8_t span_cols;
    uint8_t span_rows;
    uint8_t align;         /* cyd_display_align_t */
    uint8_t scale_x;
    uint8_t scale_y;
    uint16_t fg_color;
    uint16_t bg_color;
    uint16_t border_color;
    uint16_t action_id;
    bool enabled;
    uint8_t font;          /* cyd_display_font_t */
    uint8_t border_width;
    const cyd_display_bitmap_t *bitmap;
    const char *text_ref;
    union {
        char text[CYD_DISPLAY_TEXT_MAX_LEN + 1];
        cyd_display_rect_style_t rect;
        cyd_display_bar_t bar;
        cyd_display_sparkline_t sparkline;
    };
} cyd_display_widget_t;
```

現在使える主なウィジェット種別は以下です。

- `CYD_DISPLAY_WIDGET_TEXT`: テキスト
- `CYD_DISPLAY_WIDGET_BUTTON`: ボタン
- `CYD_DISPLAY_WIDGET_ICON`: ビットマップアイコン
- `CYD_DISPLAY_WIDGET_RECT`: 矩形 / 角丸パネル / 区切り
- `CYD_DISPLAY_WIDGET_BAR`: レベルメーター / ゲージ
- `CYD_DISPLAY_WIDGET_SPARKLINE`: 時系列折れ線グラフ

`type` と `align` は enum ではなく 1 バイトで持ちます。`font`、`border_width`、`text_ref` を追加しても `sizeof(cyd_display_widget_t)` を 72 バイトのまま保つためです (ESP32 で計測)。

テキストの持ち方は [Text And Fonts](#text-and-fonts) を参照してください。

### Payload Union

種別ごとのペイロードは排他なので anonymous union に入れています。**これは必須の制約です。**

画面バッファは 1 枚あたり `CYD_DISPLAY_MAX_WIDGETS` 個の widget を持つため、union に入れずに struct メンバーを増やすと、その分が全画面バッファに乗算されます。union 化により、種別を 3 つ追加しても `sizeof(cyd_display_widget_t)` は 72 バイトのまま変わっていません。

English contract: new widget payloads MUST go into the union. `cyd_display.cpp` holds static_asserts that fail the build if any payload variant outgrows the text buffer.

### Graph Widgets

`CYD_DISPLAY_WIDGET_SPARKLINE` はグリッドの箱の中を**ピクセル精度**で描きます。グリッドはレイアウト用の座標系であり、描画解像度の制約ではありません。

```c
typedef struct {
    const int16_t *samples;
    uint16_t count;
    uint16_t revision;
    int16_t min_value;
    int16_t max_value;
    bool fill;
    bool has_baseline;
    int16_t baseline_value;
    uint16_t baseline_color;
} cyd_display_sparkline_t;
```

使用時の規則が 2 つあります。両方守らないと正しく動きません。

1. **`samples` はコピーされません。** 画面は display task へ値渡しでキューイングされるため、配列は submit 後も生存している必要があります。app 所有の static 配列が想定パターンです。
2. **中身を書き換えたら `revision` を必ず加算してください。** dirty-rect 差分は widget を値で比較するため、ポインタの先までは見ません。`revision` が変わらないグラフは「変化なし」と判定され、**永久に再描画されません**。

English contract: both rules are load-bearing. Rule 2 in particular fails silently — the graph simply never updates.

`samples[]` への書き込みと display task の読み出しが競合しても、結果は「一部だけ新しい値のフレームが 1 回出る」だけです。トレンドグラフでは実害が無いため、ロックは不要です。

値が `[min_value, max_value]` の外にある場合は外挿せず飽和させるので、箱の外にはみ出して描画されることはありません。

### Pointer Lifetime

`ICON` の `cyd_display_bitmap_t` と `SPARKLINE` の `samples` は、**画面 submit 時にコピーされません**。

画面はキューへ値渡しされますが、ポインタはポインタのままです。実際にピクセルやサンプルが読まれるのは、数ミリ秒後の `cyd_display` task 上です。したがって両方とも、そのフレームが描画され終わるまで生存している必要があります。

```c
/* NG: submit() が返った時点で dangling */
void draw(void) {
    uint16_t pixels[16 * 16];
    cyd_display_bitmap_t icon = { .data = pixels, .width_px = 16, .height_px = 16 };
    cyd_ui_add_icon(screen, &icon, 2, 2, 2, 2);   /* icon も pixels もローカル */
    cyd_ui_submit(screen);
}
```

`static const`（flash 常駐）か、app 寿命で保持するヒープバッファを渡してください。

English contract: submit() copies the screen struct, not what its pointers reference. A local buffer is a use-after-free.

描画側では、参照前に `esp_ptr_in_drom()` / `esp_ptr_byte_accessible()` でアドレスの参照可否とサイズを検証しています。契約違反を*論理的に*検出することはできませんが、明らかに不正なポインタは warning ログを出して widget をスキップするので、いきなりクラッシュする代わりに原因が残ります。

```text
W (12345) cyd_display: icon bitmap unreadable (0x3ffb1234); check the lifetime contract
```

### Pixel Format

`cyd_display_bitmap_t.data` は `const uint16_t *`（RGB565）です。

LovyanGFX は**ポインタの型でソース形式を決めます**。[LGFXBase.hpp](third_party/lovyangfx_upstream/src/lgfx/v1/LGFXBase.hpp) の `create_pc()` を見ると、`const uint8_t *` は `rgb332_t`（8bit色）として解釈され、`const uint16_t *` が RGB565 です。`uint8_t` バッファを渡すと色が壊れるため、型で明示しています。

### Dropouts

欠測がある系列では `has_gap_value` / `gap_value` を使います。`gap_value` と一致するサンプルは描画されず、そこで折れ線が途切れます。

欠測を「記録しない」で済ませると、その区間が時間軸から消えてグラフが詰まってしまい、**「値が途切れていた」のか「その間ずっと安定していた」のか区別できなくなります**。欠測は欠測として記録し、描画側で途切れさせるのが正しい扱いです。

実例は `wifi_rssi_history` を参照してください。Wi-Fi 未接続時に `WIFI_RSSI_HISTORY_GAP_DBM` を記録しています。

`CYD_DISPLAY_WIDGET_BUTTON` で `enabled=false` の場合、描画はされますが `cyd_display_screen_hit_test()` の対象から外れます。無効状態の色は呼び出し側が指定します。

English supplement: Widget order is significant for dirty-rect comparison. Keep stable widget ordering between frames when updating only text or colors.

## Text And Fonts

### Font Faces

TEXT / BUTTON widget の `font` で書体を選びます。

| `cyd_display_font_t` | 書体 | 用途 |
|---|---|---|
| `CYD_DISPLAY_FONT_LEGACY` (0) | LovyanGFX 内蔵 6x8 ASCII を `scale_x` / `scale_y` 倍 | 従来の画面。`font` を設定しない画面はすべてこれ |
| `CYD_DISPLAY_FONT_BODY` | 16px 標準 (JIS X 0208 全体を収録) | 本文、注記、バックエンドから届く文言 |
| `CYD_DISPLAY_FONT_BODY_BOLD` | 16px 太字 | ヘッダー、小さいボタン |
| `CYD_DISPLAY_FONT_TITLE` | 24px 太字 | 見出し、主ボタン、結果 |
| `CYD_DISPLAY_FONT_CLOCK_MEDIUM` | 48px 太字 | 数字・`:/-. `・`✓!` のみ |
| `CYD_DISPLAY_FONT_CLOCK_LARGE` | 64px 太字 | 数字・`:/-. `・`✓!` のみ |

LEGACY 以外はアンチエイリアスの日本語フォントです。フォント表は `cyd_ui_fonts` にあり、flash に置いたまま描画します ([CYD UI Fonts](cyd_ui_fonts.md))。

アンチエイリアス書体の描画規則:

- 文字列は widget の枠内で縦中央に置き、`align` で左右を揃える
- 枠の外にははみ出さない (枠でクリップする)
- 枠より長い場合、まず一回り小さい書体に切り替える (`TITLE` → `BODY_BOLD`、`CLOCK_LARGE` → `CLOCK_MEDIUM`)。それでも入らなければ末尾を「…」にする
- TEXT widget の背景は塗らない。下に描いたものの上に混色して描く
- フォントに無い文字は、空白ではなく枠 (豆腐) として描く

English contract: the scale fields are ignored by anti-aliased faces. Wording that only fits after shrinking or ellipsizing is a layout bug; a screen's host test checks for it with `ui_test_check_screen()` (`test/host/ui_test_support.h`).

タッチ補正の画面 (`cyd_display_show_touch_calibration_screen()`) は、widget を使わずパネルへ直接描きます。案内文 (「タッチ位置の補正」「四隅の印を順にタッチしてください」) もアンチエイリアス書体ですが、下地が黒一色と分かっているので、`cyd_display_draw_aa_text()` に `solid_bg` を渡してその色と混色します。パネルからの読み戻しは基板によっては使えないため、直接描くときは読み戻しに頼りません。

English supplement: `cyd_display_draw_aa_text()` blends edge pixels with what it reads back from the target unless `solid_bg` is given. Strip sprites can always be read; the panel itself may not support reads (`CONFIG_CYD_DISPLAY_READABLE`), so direct drawing passes the known background colour.

### Text Storage

テキストは `cyd_display_widget_set_text()` で設定します (`cyd_ui` の関数は内部でこれを使います)。

- string literal など読み取り専用領域 (ESP32 では flash の rodata) にある文字列は、コピーせず `text_ref` で参照する。長さの制限はない
- それ以外 (スタック、ヒープ、書き換え可能な static) の文字列は `text` へコピーする。`CYD_DISPLAY_TEXT_MAX_LEN` (40 バイト) を超える分は、UTF-8 の文字の途中ではなく文字の境界で切る

日本語は 1 文字 3 バイトなので、コピーされる文字列は 13 文字までです。固定の文言は literal のまま渡し、`snprintf` で組み立てる文字列は 40 バイトに収まる短いもの (コード、UID、時刻など) に限ってください。

RAM 上の長い文字列 (バックエンドから届いたお知らせなど) は `cyd_display_widget_set_text_pinned()` で参照させます。ビットマップやスパークラインと同じく、**その画面が表示されているあいだ、文字列を書き換えてはいけません。** 差分判定は前の画面と今の画面の文字列を内容で比べるため、その場で書き換えると両方が新しい内容を指し、変化が描かれません。書き換えるのは、その文字列を使わない画面が表示されているあいだ (その画面に入る直前など) にしてください。

English contract: whether text is referenced or copied is decided by where it lives (`cyd_display_port_text_is_immutable()`), never by the caller - except through `cyd_display_widget_set_text_pinned()`, where the caller takes on the lifetime and no-in-place-edit rules above. The frame diff compares text by content, so a reused buffer whose words changed is redrawn even though its address did not.

### Borders

BUTTON と、塗りつぶしの RECT は `border_width` ピクセルの枠を持てます (0 は従来どおり 1px)。太い枠は、枠の色で塗ってから内側を枠幅ぶん小さく塗るので、角丸の角にすき間が出ません。

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

描画と差分判定のコードは `cyd_display_render.hpp` にまとめてあり、FreeRTOS・NVS・パネルに依存しません。実機の `cyd_display.cpp` と Mac 用シミュレーター ([CYD Simulator](cyd_sim.md)) は同じこのファイルで描画します。メモリに関する判定 (ポインタが読めるか、文字列が読み取り専用領域にあるか) だけは `cyd_display_port.h` を通して、それぞれの側が実装します。

English supplement: anything that decides what a pixel looks like or whether a widget is redrawn belongs in `cyd_display_render.hpp`, so the simulator cannot drift from the device.

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

## Hit Testing

タッチの判定は、app が自分で持っている画面 buffer に対して `cyd_display_screen_hit_test()` で行います。タッチ座標にある有効な `CYD_DISPLAY_WIDGET_BUTTON` の `action_id` を返します。

```c
uint16_t action_id = 0;
if (cyd_display_screen_hit_test(&s_screen, event.data.touch.x, event.data.touch.y, &action_id)) {
    /* action_id のボタンが押された */
}
```

`cyd_display` はボタンの情報を持ちません。以前は最後に送られた画面からボタン表を作って保持し、各 app がそれを lock 付きで参照していました。現在は app が自分の組み立てた画面をそのまま判定に使うので、表示 task との共有も lock も不要で、表示の進み具合とずれることもありません。

モード画面も同じ関数で判定します。ボタンの `action_id` がそのまま何番目のボタンかを表します。モード画面の配置を事前に知りたい場合は `cyd_display_get_mode_button_bounds()` で計算できます。

別の component に描画を任せる間（例: `cyd_text_input` のキーボード）は、タッチの判定もその component に任せます。app が自分の隠れた画面で判定すると、見えていないボタンが反応します。ログ表示（`cyd_display_log_show()`）はボタンを持たず入力も受けないので、表示中にタップへ反応しないようにするのは呼び出し側の責任です。

English contract: an app hit-tests the screen buffer it built and last submitted, on its own task; this component keeps no button state. Canned screens build into the caller's buffer so the buffer always matches the display. While another component draws, hand touch handling to it instead of testing your hidden screen.

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
