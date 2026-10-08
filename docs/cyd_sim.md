# CYD Simulator

## Overview

`tools/cyd_sim` は、CYD の画面を Mac 上で確認するためのシミュレーターです。実機がなくても、日本語 UI の見た目とボタンの位置を確かめられます。

実機と同じコードで描画します。

- `cyd_display_render.hpp` (widget の描画、差分判定、16px 帯への描き分け)
- `cyd_ui`、`cyd_ui_fonts`、`cyd_display_text.c`
- 各アプリの画面組み立てコード (`components/` 以下の `*_view.c`)

描画先だけが違い、LovyanGFX の SDL2 バックエンドでウィンドウに描きます。色は実機と同じく RGB565 に丸めた値です。

English supplement: the simulator compiles the device's own rendering sources rather than a copy. Only the two memory questions in `cyd_display_port.h` are answered differently: an immutable string is one inside the executable's `__TEXT` segment instead of ESP32 flash rodata.

## Build

SDL2 が必要です (`brew install sdl2`)。

```bash
cmake -S tools/cyd_sim -B tools/cyd_sim/build -G Ninja
```

```bash
ninja -C tools/cyd_sim/build
```

初回は LovyanGFX の内蔵フォント表もコンパイルするため時間がかかります。

## Usage

ウィンドウで表示する:

```bash
tools/cyd_sim/build/cyd_sim
```

- ← / →: シーンを切り替える
- クリック: タッチ。押したボタンの `action_id` をターミナルに表示する
- Ctrl + 1〜6: 表示倍率を変える
- `--scene ID`: 最初に表示するシーン
- `--scale N`: 初期の表示倍率 (既定 2)
- `--theme N`: 配色のテーマ (`cyd_ui_theme_id_t`。0 高コントラスト、1 標準、2 ライト、3 色覚配慮)。`--export` と組み合わせると、テーマごとに書き出せる

全シーンを PNG に書き出す (ウィンドウは開かない):

```bash
tools/cyd_sim/build/cyd_sim --export /tmp/cyd_screens --scale 2
```

書き出し先に `index.html` も作るので、ブラウザで一覧できます。シーン ID の一覧は `--list` で表示します。

## Scenes

シーンは `tools/cyd_sim/scenes/*.c` に書きます。1 シーンは「画面を組み立てる関数」1 つです。

```c
static void build_faces(cyd_display_screen_t *screen, unsigned frame)
{
    (void)frame;
    cyd_ui_add_panel(screen, 0, 0, 40, 30, CYD_UI_THEME_BG, 0, 0, 0);
    cyd_ui_add_label(screen, "24px タイトル", 1, 6, 38, 4,
                     CYD_DISPLAY_ALIGN_LEFT, CYD_DISPLAY_FONT_TITLE, CYD_UI_THEME_INFO);
}

static const cyd_sim_scene_t k_scenes[] = {
    { "specimen_faces", "書体見本", NULL, build_faces },
};

CYD_SIM_REGISTER_SCENES(specimen, CYD_SIM_ORDER_SPECIMEN, k_scenes);
```

`build` は毎フレーム呼ばれます。`frame` を使えば時間で変わる画面も作れ、実機と同じ差分描画の経路を通ります。

シーンのファイルは `CYD_SIM_REGISTER_SCENES` で自分のリストを登録します。ビルドは `scenes/*.c` をすべて拾うので、ファイルを置くだけでシーンが増えます。`sim_main.cpp` や `sim_catalog.h` を書き換える必要はありません。リストは `order` (`CYD_SIM_ORDER_*`) の小さい順、同じなら名前順に並びます。

English contract: the shared simulator sources (`sim_main.cpp`, `sim_catalog.h`, `sim_port.cpp`, `CMakeLists.txt`) name no app. A derived project adds its own scene files and `*_view.c` files only, so merging the upstream project never conflicts on the simulator itself.

現在のシーン:

- `launcher*`、`clock*`、`clock_settings_*`: ランチャー、時計 (時計画面、Wi-Fi に接続できない、再接続中)、時計の設定
- `settings_*`: 設定画面の共通枠と増減行。`settings_legacy_mix` は、旧書体のままのページが新しい枠の中でどう見えるか
- `keyboard_*`: キーボード画面 (`cyd_text_input`) の全状態 (パスワード、大文字、記号 2 種、URL、数字だけ)
- `sys_*`: システム設定 (`system_settings_view.c`) の全ページ、主な状態、全ダイアログ
- `info_*`: システム情報 (`system_info_view.c`) の全ページ
- `wifi_*`: Wi-Fi 設定 (`cyd_wifi_setup_view.c`) の一覧・検索中・見つからない・エラー・接続中・保存・失敗 (理由別)
- `specimen_*`: 書体見本、テーマ色とボタン、はみ出しの扱い

## Writing Simulator-Friendly Apps

アプリをシミュレーターで確認するには、画面の組み立てをサービス呼び出しから分けます。

- `<app>.c`: 状態遷移とサービス呼び出し。表示に必要な情報を model の構造体に集める
- `<app>_view.c`: model だけから画面を組み立てる。サービスを呼ばない。ヘッダーは同じコンポーネントの `include/` に置く

`components/` 以下にある `*_view.c` は、シミュレーターのビルドが自動で取り込みます。インクルードパスには、その view のディレクトリと `include/` が加わります。

English contract: a view file must not call services, read the clock or touch globals of the app. Everything it shows comes in through its model, which is what lets the simulator and the host tests build every state by hand.

## Limits

- フォントにはファームウェアの文字列リテラルにある文字しか入りません。シーンのファイルにしか無い文言は枠 (豆腐) で表示されます。シーンの文言はアプリと同じものを使ってください
- 描画速度、SPI 転送、タッチの読み取り精度は実機と異なります
- フォントの文字は親と派生プロジェクトで異なります (`scripts/ui_fonts/font_profile.json` と各プロジェクトの文字列)。シーンの文言は、そのプロジェクトのフォントで描ける文字にしてください。書体見本 (`specimen.c`) は、かな・ASCII・`extra_chars.txt` の文字だけを使っています
