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

画面は共有の画面バッファを `cyd_ui_screen_borrow()` で借りて組み立て、最後に `cyd_ui_submit()` で `cyd_display` へ渡します ([Shared Screen Buffer](#shared-screen-buffer))。

```c
static const cyd_ui_screen_owner_t s_owner = { .name = "hello" };

cyd_display_screen_t *screen = cyd_ui_screen_borrow(&s_owner);
cyd_ui_add_panel(screen, 0, 0, CYD_DISPLAY_GRID_COLS, CYD_DISPLAY_GRID_ROWS, CYD_UI_THEME_BG, 0, 0, 0);
cyd_ui_add_label(screen, "こんにちは", 1, 10, 38, 3, CYD_DISPLAY_ALIGN_CENTER,
                 CYD_DISPLAY_FONT_BODY_BOLD, CYD_UI_THEME_TEXT);
cyd_ui_add_styled_button(screen, "OK", 12, 24, 16, 5, CYD_DISPLAY_FONT_BODY_BOLD,
                         CYD_UI_THEME_ON_PRIMARY, CYD_UI_THEME_PRIMARY, CYD_UI_THEME_PRIMARY,
                         0, ACTION_OK, true);
ESP_ERROR_CHECK(cyd_ui_submit(screen));
```

`enabled=false` のボタンは無効の色で描かれ、タッチ hit-test の対象になりません。

English supplement: Disabled buttons keep their `action_id` for screen state clarity, but `cyd_display_screen_hit_test()` skips them.

## Shared Screen Buffer

`cyd_display_screen_t` は 1 枚で約 3.4KB (ESP32 で 3,460 バイト) あります。以前はアプリがそれぞれ 1 枚を static に持っていて、時計の製品で 7 枚 (約 24KB)、派生の打刻端末ではそれ以上が、使っていないあいだも RAM を占めていました。

`cyd_display_submit_screen()` は画面を表示キューへ値で写すので、アプリのバッファが要るのは「組み立て」と「自分が最後に組み立てた画面でのタップ判定 (`cyd_display_screen_hit_test()`)」だけです。`app_shell` で前面にいるアプリは常に 1 つなので、`cyd_ui` が共有の画面バッファを 1 枚だけ持ち、どのアプリもそれを借りて組み立てます。

- `cyd_ui_screen_borrow(&owner)`: バッファを空にし、`owner` を「最後に組み立てた者」として覚えて返す
- `cyd_ui_screen_of(&owner)`: `owner` が最後に組み立てた者ならバッファを、そうでなければ `NULL` を返す。`cyd_display_screen_hit_test()` は `NULL` では何にも当たらない

```c
static const cyd_ui_screen_owner_t s_owner = { .name = "my_app" };

static esp_err_t my_app_show(void)
{
    my_app_view_build(cyd_ui_screen_borrow(&s_owner), &s_model);
    return cyd_ui_submit(cyd_ui_screen_of(&s_owner));
}

/* タップの判定 */
if (cyd_display_screen_hit_test(cyd_ui_screen_of(&s_owner), x, y, &action_id)) {
    ...
}
```

別のアプリが前面に来たり、`cyd_text_input` のように別の部品が画面を描いたりすると、バッファの持ち主が変わり、前のアプリの `cyd_ui_screen_of()` は `NULL` になります。そのため、見えていないボタンにタップが反応することはありません。前のアプリは、もう一度組み立てるまでタップに反応しません。

English contract:

- `cyd_ui_screen_borrow()` clears the buffer. Build the whole screen every time; never patch what was built last, because another owner may have built in between.
- Hit-test only through `cyd_ui_screen_of()`. It returns NULL once someone else has built, so a replaced app sees no buttons until it builds again.
- Shell task only, like every draw ([Display Ownership](app_shell.md#display-ownership)). There is no lock.
- Each owner is its own `static const cyd_ui_screen_owner_t`; the address is the identity, the name is for reading only.
- Pinned text, bitmaps and sparkline samples live outside the buffer and are unaffected (their own lifetime rules still apply).

### Migrating An App

派生プロジェクトのアプリを共有のバッファに移す手順です。親のアプリ (設定、システム情報、PIN、時計、時計の設定、Wi-Fi 設定、文字入力、ランチャー) は移行済みなので、差分の見本になります。

1. 画面バッファの宣言を owner に置き換える

   ```c
   /* 前 */
   static cyd_display_screen_t s_punch_screen;
   /* 後 */
   static const cyd_ui_screen_owner_t s_punch_screen_owner = { .name = "time_punch" };
   ```

2. 組み立ては `cyd_ui_screen_borrow()` で借りたバッファに、submit は `cyd_ui_screen_of()` で行う

   ```c
   /* 前 */
   punch_view_build(&s_punch_screen, &model);
   return cyd_ui_submit(&s_punch_screen);
   /* 後 */
   punch_view_build(cyd_ui_screen_borrow(&s_punch_screen_owner), &model);
   return cyd_ui_submit(cyd_ui_screen_of(&s_punch_screen_owner));
   ```

   `cyd_display_show_text()` などの定型画面も、`cyd_ui_screen_borrow()` で借りたバッファを渡します。

3. タップの判定に渡す画面を `cyd_ui_screen_of()` にする

   ```c
   /* 前 */
   cyd_display_screen_hit_test(&s_punch_screen, x, y, &action_id);
   /* 後 */
   cyd_display_screen_hit_test(cyd_ui_screen_of(&s_punch_screen_owner), x, y, &action_id);
   ```

   バッファへのポインタを変数やタッチの tracker に保存しないこと。毎回 `cyd_ui_screen_of()` で取り直します。

4. 次を確かめる
   - 組み立ての関数 (`*_view_build()`) が、毎回画面の全体を組み立てている。前の画面の widget を書き換えて出し直す所 (部分の描き直し) があれば、全体を組み立てる形に直す
   - バッファの中身を、次に描くまで覚えておいて使う所がない (前の画面との比べなど)。比べたいなら、view model の方を覚えておく
   - 別の部品 (`cyd_text_input` など) に画面を任せたあと、戻ったら自分の画面を組み立て直している。組み立て直すまではタップに反応しない
   - 描くのは shell task (アプリの `enter` / `step` / `leave` の中) だけ
   - `grep -n "cyd_display_screen_t s_" ` で、static の画面バッファが残っていない

5. 計測: `python -m esp_idf_size build/<project>.map` の `Used static DRAM` が、移したバッファ 1 枚につき 3,460 バイト減る

English supplement: the migration is mechanical (declaration, build/submit, hit-test). The two things to check by hand are partial rebuilds of a previous screen and pointers to the screen kept across builds; both break once another owner can build in between.

## Screen Rules

新しい画面は、次の決まりで組みます。`components/framework/app_shell/sample/hello_app` が最小の見本です。

- 画面は全面の背景の板 (`cyd_ui_add_panel(..., CYD_UI_THEME_BG, ...)`) から始める
- 色は `CYD_UI_THEME_*` だけを使う。設定の配色に追従する ([Color Themes](#color-themes))
- 文字は `cyd_ui_add_label()` と `cyd_ui_add_styled_button()` の日本語書体で描く
- 設定・情報・入力など項目の多い画面は、文字をすべて 16px (`BODY` / `BODY_BOLD`) にする。24px は収まっても周りの 16px の行と釣り合わない。大きな書体 (24px、数字の 48px・64px) は、時計や打刻結果のように一目で読ませる画面にだけ使う
- 抵抗膜タッチなので、ボタンは高さ 32〜40px 以上にする
- ボタンは隙間を空けずにマスいっぱいに並べてよい。ボタンの枠はマスの四方 2px 内側に描かれるので、隣のマスのボタン (設定の「戻る」とそのすぐ下の行、キーボードのキー、曜日のボタンなど) とは 4px 離れて見える。タッチの当たり判定はマス全体のまま
- 画面の組み立ては、サービスを呼ばない `*_view.c` に分ける。シミュレーター (`tools/cyd_sim`) とホストテストで全状態を確かめられる ([CYD Simulator](cyd_sim.md#writing-simulator-friendly-apps))
- 表示する文言はすべて日本語にする。ログ、識別子、技術的な値 (エラー名、heap のバイト数など) は英語のままでよい

English contract: new screens use the themed calls and colours only. The legacy calls below exist for screens in derived projects that have not been converted yet.

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
- `cyd_ui_add_styled_button()`: 書体と枠幅を指定するボタン。塗りのボタンは `border_color == bg_color`、枠線のボタンは `*_TINT` か `CYD_UI_THEME_SURFACE` の塗りに色付きの枠と文字 (赤なら `DANGER_SOFT`)。無効なボタンは `DISABLED` の文字と枠で描く
- `cyd_ui_add_panel()`: 枠幅と角丸を指定する塗りの板。`radius` を一辺の半分にすると円になる

色は `CYD_UI_THEME_*` を使います。成功・注意・エラーなどの意味を持つ色は、その意味以外に使わないでください。`*_TINT` は同名の色と組み合わせる暗い塗り、`ON_*` はその色で塗った上に載せる文字色です。

画面全体を背景の板から始めると、画面が切り替わっても最初の widget が同じなので、差分描画が全面を描き直さずに済みます。

English supplement: string literals passed to these calls are referenced, not copied, so they may be any length. Formatted text is copied and capped at `CYD_DISPLAY_TEXT_MAX_LEN` bytes.

## Color Themes

色はテーマとして 4 種類から選べます。設定の「一般」ページの「配色」で切り替え、その場で全画面に反映されます。

| `cyd_ui_theme_id_t` | 表示名 | 内容 |
|---|---|---|
| `CYD_UI_THEME_ID_HIGH_CONTRAST` (0、既定) | 高コントラスト | 黒地に白文字と原色 (黄・水色・緑・赤)。CYD の TN 液晶で一番はっきり見える |
| `CYD_UI_THEME_ID_STANDARD` | 標準 | 紺の地。枠線・青・赤は段階 5 までより少し濃くし、コントラストの目標を満たすようにした |
| `CYD_UI_THEME_ID_LIGHT` | ライト | 白地に黒文字。明るい場所向け |
| `CYD_UI_THEME_ID_COLOR_SAFE` | 色覚配慮 | Okabe-Ito の配色。成功は青緑、危険は朱色、注意は黄、案内は赤紫で、赤と緑の区別に意味を持たせない |

`CYD_UI_THEME_*` の色の名前は、選ばれているテーマの表を引く式です (`cyd_ui_theme()->bg` など)。画面を組むたびに今のテーマの色が入ります。そのため `static const` の初期化子や `switch` の `case` には使えません。

文字と背景の組は、どのテーマでもコントラスト比の目標を満たします。WCAG 2.x の式を RGB565 に丸めた色で計算し、`test/host/test_ui_themes.c` が検査します。

| 組 | 高コントラスト | ほかのテーマ |
|---|---|---|
| 文字と背景 | 7:1 以上 | 4.5:1 以上 |
| 枠線などの文字以外 | 3:1 以上 | 3:1 以上 |
| 無効なボタンの文字 | 4.5:1 以上 | 3:1 以上 |

色の使い分け:

- 赤は、塗り (`DANGER`、上に `ON_DANGER` の文字) と文字・枠 (`DANGER_SOFT`) で色を分けている。中間の明るさの赤では、白文字を載せる塗りと暗い背景に載せる文字の両方は満たせないため
- 無効なボタンは `DISABLED` の文字と枠で描く。高コントラストでは補助文字も白なので、無効を見分ける色が別に要る

選択は NVS (`sys_ui` 名前空間の `theme`) に保存します。`system_boot` が `cyd_display_init()` の直後に `cyd_ui_theme_load()` で読み、設定画面を離れるときに `cyd_ui_theme_save()` で書きます (値が変わったときだけ)。知らない値 (新しいファームで足したテーマを古いファームで読んだ場合など) は既定に戻すだけで、NVS の初期化は求めません。システムの設定なので、「アプリのデータを消去」では消えません。

テーマ番号は保存される値なので、並べ替えてはいけません。新しいテーマは末尾に足します。

English contract: the theme ids are persisted and must never be renumbered. `cyd_ui_theme_store.c` (NVS) is firmware only; the simulator and the host tests build `cyd_ui.c` without it.

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

増減行 (`cyd_ui_add_stepper_row()`) は、項目名・値・`−` / `+` のボタンをすべて 16px 太字で描きます。設定画面は項目が多いので、24px の値やボタンは収まっても周りの行と釣り合いません。旧寸法の小さいボタン (幅 24px、高さ 16px) と、16px 太字に入らない項目名は旧 ASCII 書体に戻すので、ページを日本語化するまでも読めます。共通枠の見出しも 16px 太字です。新しく作る行は、抵抗膜タッチのため高さ 4 行 (32px) 以上にしてください。

English supplement: the chrome deliberately leaves the background to the page. Pages are converted one at a time; a themed background under a legacy page would show its text boxes as black blocks.

## Legacy ASCII API

`cyd_ui_add_text()` と `cyd_ui_add_button*()` は旧 ASCII 書体で描く API です。**新しいコードでは使わないでください。** 日本語を描けず、配色に追従せず、文字の大きさは整数の倍率 (`scale`) で決まります。このリポジトリの画面はもう使っていませんが、派生プロジェクトの未変換の画面のために残しています。`cyd_display_show_text()` / `show_lines()` / `show_mode_screen()` も同じ扱いです。

```c
/* 旧 API。新規使用禁止 */
cyd_ui_add_text(&screen, "Hello", 0, 0, CYD_DISPLAY_GRID_COLS, 2,
                CYD_DISPLAY_ALIGN_CENTER, 2, CYD_UI_COLOR_WHITE);
cyd_ui_add_button(&screen, "OK", 10, 20, 20, 4,
                  CYD_UI_COLOR_BLUE, CYD_UI_COLOR_CYAN, 1);
```

English contract: do not use the legacy calls in new code; they cannot draw Japanese and ignore the colour theme.

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
- 共有の画面バッファ ([Shared Screen Buffer](#shared-screen-buffer))
- text widget の追加
- button widget の追加
- disabled button の追加
- rect / bar / sparkline widget の追加
- 共通色定義
- `cyd_display_submit_screen()` への薄い wrapper

layout engine、画面遷移、focus 管理、入力 dispatch は持ちません。画面遷移は `cyd_clock_app` のようなアプリケーションコンポーネントが管理します。

English supplement: Keep screen ownership and mode transitions in the application layer. Apart from the theme and the shared screen buffer (which only remembers its last builder), `cyd_ui` stays stateless.
