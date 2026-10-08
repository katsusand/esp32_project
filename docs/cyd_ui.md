# CYD UI

## Overview

`cyd_ui` は、`cyd_display_screen_t` と widget を組み立てるための薄い helper コンポーネントです。

`cyd_display` は低レベルの表示 submit と描画 driver を担当し、`cyd_ui` はアプリケーションや全画面 UI が使う text/button widget の定型生成だけを担当します。

English supplement: This is not a UI framework. It is a small screen-builder helper that keeps application code from duplicating widget initialization details.

## Public API

利用するファイルでは、次のヘッダーを include します。

```c
#include "cyd_ui.h"
```

画面を初期化する場合は `cyd_ui_screen_clear()` を使います。

```c
cyd_display_screen_t screen = { 0 };
cyd_ui_screen_clear(&screen);
```

text widget と button widget は以下で追加します。

```c
cyd_ui_add_text(&screen, "Hello", 0, 0, CYD_DISPLAY_GRID_COLS, 2,
                CYD_DISPLAY_ALIGN_CENTER, 2, CYD_UI_COLOR_WHITE);

cyd_ui_add_button(&screen, "OK", 10, 20, 20, 4,
                  CYD_UI_COLOR_BLUE, CYD_UI_COLOR_CYAN, 1);
```

押せない状態のボタンは `cyd_ui_add_button_enabled()` または `cyd_ui_add_button_with_fg_enabled()` で追加します。`enabled=false` のボタンは表示だけ行われ、タッチ hit-test の対象になりません。

```c
cyd_ui_add_button_enabled(&screen, "NEXT", 27, 26, 12, 3,
                          CYD_UI_COLOR_DIMGREY, CYD_UI_COLOR_DARKGREY,
                          ACTION_NEXT, false);
```

English supplement: Disabled buttons keep their `action_id` for screen state clarity, but `cyd_display_screen_hit_test()` skips them.

最後に `cyd_ui_submit()` で `cyd_display` へ渡します。

```c
ESP_ERROR_CHECK(cyd_ui_submit(&screen));
```

## Themed Japanese UI

アンチエイリアスの日本語フォントを使う画面は、以下の関数とテーマ色で組み立てます。

```c
cyd_ui_add_panel(&screen, 0, 0, 40, 30, CYD_UI_THEME_BG, 0, 0, 0);  /* 背景 */
cyd_ui_add_label(&screen, "カードをタッチ", 2, 19, 36, 8,
                 CYD_DISPLAY_ALIGN_CENTER, CYD_DISPLAY_FONT_TITLE, CYD_UI_THEME_INFO);
cyd_ui_add_styled_button(&screen, "出勤", 1, 4, 18, 9, CYD_DISPLAY_FONT_TITLE,
                         CYD_UI_THEME_ON_SUCCESS, CYD_UI_THEME_SUCCESS, CYD_UI_THEME_SUCCESS,
                         0, ACTION_IN, true);
```

- `cyd_ui_add_label()`: 書体を指定するテキスト。枠内で縦中央、長すぎれば縮小または「…」
- `cyd_ui_add_label_pinned()`: RAM 上の長い文字列を参照で表示する。表示中は書き換え禁止 ([CYD Display Driver](cyd_display.md#text-storage))
- `cyd_ui_add_styled_button()`: 書体と枠幅を指定するボタン。塗りのボタンは `border_color == bg_color`、枠線のボタンは暗い塗り (`*_TINT` か `CYD_UI_THEME_SURFACE`) に色付きの枠と文字
- `cyd_ui_add_panel()`: 枠幅と角丸を指定する塗りの板。`radius` を一辺の半分にすると円になる

色は `CYD_UI_THEME_*` を使います。成功・注意・エラーなどの意味を持つ色は、その意味以外に使わないでください。`*_TINT` は同名の色と組み合わせる暗い塗り、`ON_*` はその色で塗った上に載せる文字色です。

画面全体を背景の板から始めると、画面が切り替わっても最初の widget が同じなので、差分描画が全面を描き直さずに済みます。

English supplement: string literals passed to these calls are referenced, not copied, so they may be any length. Formatted text is copied and capped at `CYD_DISPLAY_TEXT_MAX_LEN` bytes.

## Settings Chrome

設定画面の共通の枠 (`cyd_ui_add_settings_chrome()`、または `cyd_ui_add_settings_title()` / `_back()` / `_page_nav()`) は、日本語書体とテーマ色で描きます。

```text
rows 0-3    [戻る] 見出し                       (見出しの帯)
rows 4-26   ページの中身 (呼び出し側が描く)
rows 27-29  [前へ]     ページ名 n/N     [次へ]
```

- 枠は画面の背景を塗らない。旧 ASCII 書体のままのページは背景が黒のままなので、旧書体の文字 (自分の枠を黒で塗る) が浮かない
- 見出しの帯は `cyd_ui_add_settings_title()` が描くので、ページの中身より先に呼ぶ
- ページ名が長いときは文字境界で切り、` n/N` は必ず残す

増減行 (`cyd_ui_add_stepper_row()`) は、項目名を 16px 太字、値を 24px 太字 (行の高さが 24px 未満なら 16px 太字) で描きます。`−` / `+` のボタンは、入る中で一番大きい書体を使います。旧寸法の小さいボタン (幅 24px、高さ 16px) と、16px 太字に入らない項目名は旧 ASCII 書体に戻すので、ページを日本語化するまでも読めます。新しく作る行は、抵抗膜タッチのため高さ 4 行 (32px) 以上にしてください。

English supplement: the chrome deliberately leaves the background to the page. Pages are converted one at a time; a themed background under a legacy page would show its text boxes as black blocks.

## Graph Widgets

グラフ系は `cyd_ui_add_rect()` / `cyd_ui_add_bar()` / `cyd_ui_add_sparkline()` で追加します。いずれもグリッドで箱を指定し、中身はピクセル精度で描かれます。

```c
static int16_t s_watt_samples[120];
static cyd_display_sparkline_t s_watt_graph = {
    .samples = s_watt_samples,
    .count = 120,
    .min_value = 0,
    .max_value = 2000,
    .fill = true,
    .has_baseline = true,
    .baseline_value = 1500,
    .baseline_color = CYD_UI_COLOR_RED,
};

/* 新しい測定値を入れたら revision を必ず加算する */
s_watt_samples[write_index] = latest_watt;
s_watt_graph.revision++;

cyd_ui_add_sparkline(&screen, 0, 12, 40, 10, &s_watt_graph,
                     CYD_UI_COLOR_GREEN, CYD_UI_COLOR_BLACK, CYD_UI_COLOR_DARKGREY);
```

`samples` はコピーされないため static 配列を使い、内容を変えたら `revision` を加算します。詳細な契約は `docs/cyd_display.md` の Graph Widgets を参照してください。

English supplement: forgetting to bump `revision` fails silently — the graph is submitted but never redrawn.

## Scope

`cyd_ui` は以下を提供します。

- 画面構造体の初期化
- text widget の追加
- button widget の追加
- disabled button の追加
- rect / bar / sparkline widget の追加
- 共通色定義
- `cyd_display_submit_screen()` への薄い wrapper

layout engine、画面遷移、focus 管理、入力 dispatch は持ちません。画面遷移は `cyd_clock_app` のようなアプリケーションコンポーネントが管理します。

English supplement: Keep screen ownership and mode transitions in the application layer. `cyd_ui` should stay stateless.
