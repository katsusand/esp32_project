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
