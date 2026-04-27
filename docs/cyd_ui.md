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

English supplement: Disabled buttons keep their `action_id` for screen state clarity, but `cyd_display` excludes them from action hit testing.

最後に `cyd_ui_submit()` で `cyd_display` へ渡します。

```c
ESP_ERROR_CHECK(cyd_ui_submit(&screen));
```

## Scope

`cyd_ui` は以下を提供します。

- 画面構造体の初期化
- text widget の追加
- button widget の追加
- disabled button の追加
- 共通色定義
- `cyd_display_submit_screen()` への薄い wrapper

layout engine、画面遷移、focus 管理、入力 dispatch は持ちません。画面遷移は `cyd_clock_app` のようなアプリケーションコンポーネントが管理します。

English supplement: Keep screen ownership and mode transitions in the application layer. `cyd_ui` should stay stateless.
