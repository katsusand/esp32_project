# CYD Text Input

## Overview

`cyd_text_input` は、CYD画面上で再利用できる汎用文字入力frameworkコンポーネントです。アプリ固有serviceへ依存せず、`cyd_display`、`cyd_input`、`cyd_ui`だけを利用します。

## Modes

- `CYD_TEXT_INPUT_MODE_GENERIC`: 通常文字列
- `CYD_TEXT_INPUT_MODE_PASSWORD`: 初期状態で入力をマスクし、表示切替を提供
- `CYD_TEXT_INPUT_MODE_URL`: `http://`、`https://`、元の値を切り替える補助ボタンを提供

大小文字、数字・記号、追加記号、空白、末尾削除、保存、キャンセルを共通で扱います。最大長は `CYD_TEXT_INPUT_MAX_LEN` 以下で呼び出し側が指定します。

## Public API

`cyd_text_input_begin_session()` で入力を開始し、foreground appのstepから `cyd_text_input_poll_session()` を呼びます。入力イベントがないpollでは `event == NULL` を渡すとcursor blinkを更新します。

```c
cyd_text_input_config_t config = {
    .title = "Input",
    .input_label = "VALUE:",
    .initial_text = "",
    .max_len = 64,
    .mode = CYD_TEXT_INPUT_MODE_GENERIC,
};
ESP_ERROR_CHECK(cyd_text_input_begin_session(&config));
```

`CYD_TEXT_INPUT_RESULT_SAVED` では出力bufferへ値をコピーし、`CYD_TEXT_INPUT_RESULT_CANCELLED` では値を返しません。

English supplement: A session is foreground-only transient state and must be polled from the display-owner task.
