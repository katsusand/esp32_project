# CYD Text Input

## Overview

`cyd_text_input` は、CYD画面上で再利用できる汎用文字入力frameworkコンポーネントです。アプリ固有serviceへ依存せず、`cyd_display`、`cyd_input`、`cyd_ui`だけを利用します。

## Modes

- `CYD_TEXT_INPUT_MODE_GENERIC`: 通常文字列
- `CYD_TEXT_INPUT_MODE_PASSWORD`: 初期状態で入力をマスクし、表示切替を提供
- `CYD_TEXT_INPUT_MODE_URL`: `http://`、`https://`、元の値を切り替える補助ボタンを提供

大小文字、数字・記号、追加記号、空白、末尾削除、保存、戻る (キャンセル) を共通で扱います。最大長は `CYD_TEXT_INPUT_MAX_LEN` 以下で呼び出し側が指定します。

## Public API

`cyd_text_input_begin_session()` で入力を開始し、foreground appのstepから `cyd_text_input_poll_session()` を呼びます。入力イベントがないpollでは `event == NULL` を渡すとcursor blinkを更新します。

```c
cyd_text_input_config_t config = {
    .title = "サーバー URL",
    .input_label = "URL",
    .fixed_prefix = NULL,
    .initial_text = "",
    .force_upper = false,
    .auto_separator = '\0',
    .auto_groups = NULL,
    .auto_group_count = 0,
    .max_len = 64,
    .mode = CYD_TEXT_INPUT_MODE_GENERIC,
};
ESP_ERROR_CHECK(cyd_text_input_begin_session(&config));
```

## Screen

キーボード画面はアンチエイリアスの日本語書体とテーマ色で描きます。キー自体は ASCII です (URL・SSID・パスワード・コードの入力用)。

```text
rows 0-3    [戻る]        見出し (16px 太字)        [保存]
rows 4-6    文脈の行 (例: SSID MyHome-5G)。無ければ空ける
rows 7-10   | 入力ラベル  値|                              |
rows 11-22  文字キー 3 段 (各 32px 角、16px 太字)
rows 23-26  [ABC] [123] [      空白      ] [削除]
rows 27-29  [文字を表示] / [https://]  (パスワード / URL のときだけ)
```

- 文脈の行が無くてもキーの位置は変わらない。指が覚えた位置を動かさないため
- 長い値は「…」と末尾を表示する。打っている場所が常に見える。カーソルの幅は常に確保するので、点滅しても文字が動かない
- `title`、`context_label`、`context_value`、`input_label` は 40 バイトまで、UTF-8 の文字境界でコピーする (日本語なら 13 文字)。見出しは 16px 太字で 11 文字まで入り、それより長いと「…」で切れる

English supplement: `cyd_text_input.c` owns the session (typing, paging, cursor blink) and fills a `cyd_text_input_view_model_t`; `cyd_text_input_view.c` builds the screen from that model only, so the simulator (`keyboard_*` scenes) and `test/host/test_ui_common.c` can build every keyboard state. `cyd_text_input_view.h` is not an app API.

## Fixed Prefix

`fixed_prefix` を指定すると、その文字列が値の先頭に固定されます。表示上は値の一部として現れ、`削除` でも消せません。保存結果にも含まれ、`max_len` にも含まれます。

admin card の issue key のように、発行形式の先頭が常に同じ（`A-XXXX-XXXX` の `A-`）場合に、入力の手間と打ち間違いを減らすために使います。`initial_text` が既に prefix で始まっている場合は二重付与しません。

English contract: the prefix is part of the value, not decoration. It is saved with the value and cannot be deleted by the operator.

## Upper Case Lock And Auto Separator

`force_upper` を立てると、入力文字は常に大文字化され、キーボードも大文字ページに固定されます。左下のボタンは `ABC` 固定表示になり、大小切り替えではなく **記号ページから英字ページへ戻る** 役割になります。記号ページへの切り替え（`123` / `()[]`）は従来通りです。

`auto_groups` に group 長の配列を渡すと、`fixed_prefix` より後ろの入力がその区切りで `auto_separator` を挟んで**表示**されます。配列の最後の要素は繰り返し使われるので、`{4}` なら `XXXX-XXXX-...`、`{4, 2, 2}` なら `YYYY-MM-DD` になります。group 数の上限は `CYD_TEXT_INPUT_MAX_GROUPS` です。

区切り文字は表示上のもので、入力値そのものには入りません。そのため `削除` は常に1文字削除で、group の最後の1文字を消せば区切り文字も一緒に消えます。group が埋まった時点で区切り文字を表示するため、4文字入力した直後は `XXXX-` と出ます。これ以上入力できない長さでは末尾の区切り文字を出しません。

`max_len` は区切り文字を含めた表示長で、実際に入力できる文字数はそこから逆算されます（`YYYY-MM-DD` なら `max_len` 10 に対して入力は8文字）。

保存時に返る文字列は区切り文字を含んだ表示形と同じです（末尾に取り残された区切り文字だけ落とします）。日付のように区切り自体が書式の一部である値をそのまま扱えます。key 側の normalize は元々ハイフンを除去するので影響ありません。

## Digits Only

`digits_only` を立てると、キーボードは `0` から `9` だけになり、`ABC` / `123` / `空白` は無効表示になります。誕生日のように数字以外を打つ意味がない入力に使います。

registration key（`XXXX-XXXX`）と admin card issue key（`A-XXXX-XXXX`）はどちらも大文字のみのアルファベットで、ハイフン位置が固定なので、この2つを使って入力ミスの余地を無くしています。

`CYD_TEXT_INPUT_RESULT_SAVED` では出力bufferへ値をコピーし、`CYD_TEXT_INPUT_RESULT_CANCELLED` では値を返しません。

English supplement: A session is foreground-only transient state and must be polled from the display-owner task.
