# UI Japanese Platform Plan

## Overview

親プロジェクト (esp32_project) の UI を日本語化する計画です。子プロジェクト cyd_time_puncher で打刻画面に対して行った日本語化 (katsusand/cyd_time_puncher PR #4、コミット b725b12 / 1f8885e) の描画基盤を親へ移し、親の全画面を日本語書体で組み直します。

English supplement: The rendering infrastructure is ported from the child almost verbatim so that the parent and the child share byte-identical `cyd_display` / `cyd_ui` / `cyd_ui_fonts` sources (except generated font tables). The parent then rewrites its own screens. Work happens in the parent first and flows to the child through the usual upstream merge.

## Decisions

子で合意済みの方針をそのまま引き継ぎます。

- 表示は日本語のみ。英語への切り替えは持たない
- 文字はアンチエイリアス。描画は LovyanGFX (`cyd_display`) のまま、LVGL は使わない
- 使いやすさを優先する。抵抗膜タッチなので、ボタンは最小 40px 程度
- ログ、識別子、エラーログの本文は英語のまま
- URL・SSID などのキー入力 (`cyd_text_input` のキー) は ASCII のまま

## Ported From The Child

子の PR 直前は、`cyd_display` と `cyd_ui` が親と同一でした。以下は子のファイルをそのまま持ち込み、打刻画面に固有の部分だけを除きます。

| 対象 | 内容 |
|---|---|
| `components/platform/cyd_display` | `cyd_display_render.hpp` (FreeRTOS 非依存の描画・差分判定・ヒットテスト)、`cyd_display_text.c` (`text_ref` と 40 バイトの文字境界コピー、`set_text_pinned`)、`cyd_display_port.h`、widget の `font` / `border_width` / `text_ref` |
| `components/framework/cyd_ui` | `CYD_UI_THEME_*`、`cyd_ui_add_label` / `add_label_pinned` / `add_styled_button` / `add_panel` |
| `components/support/cyd_ui_fonts` | フォント表のコンポーネントとビルド時の文字チェック。`generated/` は親で生成し直す |
| `scripts/ui_fonts` | `fetch_fonts.sh`、`gen_ui_fonts.py`、`ui_font_chars.py`、`check_ui_font_glyphs.py`、`extra_chars.txt` |
| `third_party/fonts/biz_udpgothic` | OFL.txt と取得元の記録 |
| `tools/cyd_sim` | Mac 用シミュレーター。シーンは書体見本 (`specimen`) だけ持ち込み、親の画面のシーンを足す |
| `test/host/test_ui_text.c` | UTF-8 の切り詰め・折り返しの検査。打刻画面の検査は除き、親の画面の検査を足す |
| docs | `cyd_display.md` / `cyd_ui.md` の追記、`cyd_ui_fonts.md`、`cyd_sim.md`、`license.md` の追記 |

持ち込まないもの:

- `cyd_text_input` の子側の変更 (生年月日入力)。親は親の版を元に日本語化する
- `partitions.csv` (親は親で `factory` 化する。下の Partition Layout を参照)
- `display_message` まわり (`message_font.json` を含む) と打刻アプリ

English contract: after Phase 1, `components/platform/cyd_display`, `components/framework/cyd_ui`, `components/support/cyd_ui_fonts/cyd_ui_fonts.c` and `CMakeLists.txt`, `check_ui_font_glyphs.py`, `fetch_fonts.sh`, `extra_chars.txt` and `third_party/fonts` are byte-identical to the child at 1f8885e. The only files that differ are the ones listed in First Merge Into The Child, each reshaped so the child can adopt the parent's version.

## Partition Layout

OTA 2 面構成をやめ、3MB の `factory` 区画にする (2026-10-08 にユーザーが決定)。OTA 更新はコード上どこからも使われていない (`esp_ota_*` / `esp_https_ota` の呼び出しが無い)。

現行 (アプリ約 1.06MB、区画 1.5MB) からの変更案:

```
# Name,   Type, SubType, Offset,   Size,     Flags
nvs,      data, nvs,     0xa000,   0x23000,
phy_init, data, phy,     0x2f000,  0x1000,
factory,  app,  factory, 0x30000,  0x300000,
storage,  data, fat,     0x330000, 0xd0000,
```

- `nvs` / `phy_init` / `storage` のオフセットとサイズは変えない。書き換え後も NVS (Wi-Fi 設定、タッチ較正など) と内部ストレージの中身が残る
- `otadata` を削除し、`ota_0` + `ota_1` (0x30000-0x330000) をまとめて 3MB の `factory` にする
- 0x2d000-0x2f000 は未使用の穴になる。アプリ区画のオフセットを 0x10000 境界に保つための意図的なもの
- README の区画の説明も合わせて直す

子への取り込みでは、`partitions.csv` が衝突する (子は `nvs` を縮めて `devid` 区画を足している)。子の版を採る (`git checkout --ours -- partitions.csv`)。`factory` と `storage` の位置は親子で同じ。

English supplement: Invariant — `nvs` and `storage` keep their offsets and sizes so a reflash preserves their contents. In the child, always keep the child's `partitions.csv` on merge: its `devid` partition at 0x29000 must survive. Reintroducing network OTA later would require going back to a two-slot layout.

## Font Profiles

生成スクリプトに「16px 標準書体へ JIS X 0208 全体を収録するか」の切り替えを付けます。

- 設定は `scripts/ui_fonts/font_profile.json` に置く。親は `{"body_jis_x0208": false}`、子は `true`
- `true` のときだけ `message_font.json` (サーバーの文言検査用) も書き出す
- 親は全書体ともテキスト用の文字集合 (ASCII、ソースの文字列リテラル、`extra_chars.txt`、かな・和文記号) だけを収録する。全書体で約 240KB の見込み

English supplement: the profile is a data file rather than a command-line flag so that regeneration is reproducible from the repository alone. The parent ships the file once and is not expected to change it, so the child's edit of it should not conflict on later merges.

## Merging Into The Child

`components/support/cyd_ui_fonts/generated/` は親と子で中身が違うため、取り込みのたびに衝突します。手順を次のように決めます。

1. 子で `git merge upstream/main` する
2. `generated/` が衝突したら、どちらの版でもよいので解決済みにする (`git checkout --ours -- components/support/cyd_ui_fonts/generated`)
3. 衝突の有無にかかわらず、`scripts/.venv/bin/python scripts/ui_fonts/gen_ui_fonts.py` で子の文字列とプロファイルから作り直す
4. 作り直した `generated/` をマージコミットに含める

手順 3 を衝突時だけにしてはいけません。子が前回の取り込みから `generated/` を変えていなければ、親の版が衝突なしで入ってきます。親の版は 16px 標準書体に JIS X 0208 を含まないので、`display_message` の文字が豆腐になります。

初回の取り込みでは `font_profile.json` が親の版 (`false`) で入るので、子で `true` に直してから作り直します。

取りこぼしの安全網として、親で増えた文字が子の表に無ければ、ビルド時の文字チェックがビルドを止めます。

これを `scripts/ui_fonts/merge_upstream.sh` にまとめる。

- 親で書いて親のリポジトリに置き、取り込みで子へ届ける。実行するのは子だけ (`upstream` リモートが無ければ何もせずに終わる)
- `git merge upstream/main` を実行し、`generated/` 以外の衝突があればそこで止めて、利用者に解決を任せる
- `generated/` だけの衝突、または衝突なしなら、上の 2〜3 を行い、作り直した結果をステージする。コミットは利用者が行う (衝突なしで自動コミットされないよう `git merge --no-commit` を使う)
- `.gitattributes` の `merge=ours` は使わない。各クローンで `git config` が必要で、設定漏れに気づきにくいため

English supplement: the generator must be deterministic (same TTF, same freetype-py version, same profile and literals → byte-identical output) for step 3 to be trustworthy. `requirements.txt` pins freetype-py for this reason.

## First Merge Into The Child

子は描画基盤を先に持っているので、初回の取り込みだけは同じファイルが双方で追加・変更されていて衝突します。親で次のように作り変えたため、子では「親の版を採り、子の固有部分を別ファイルへ移す」で解決します。2 回目以降はこれらのファイルで衝突しない見込みです。

| ファイル | 親での変更 | 子での解決 |
|---|---|---|
| `partitions.csv`、README の区画の節 | `factory` 化 (`devid` なし) | 子の版を採る |
| `tools/cyd_sim/sim_main.cpp`、`sim_catalog.h`、`CMakeLists.txt` | シーンは各ファイルが `CYD_SIM_REGISTER_SCENES` で自分を登録。view は `components/*/*/*_view.c` を自動で拾う | 親の版を採り、`scenes/time_punch.c` の末尾を `CYD_SIM_REGISTER_SCENES(time_punch, CYD_SIM_ORDER_MAIN_APP, k_scenes);` に替える |
| `tools/cyd_sim/scenes/specimen.c` | 文言を、かな・ASCII・`extra_chars.txt` の文字だけにした | 親の版を採る |
| `test/host/test_ui_text.c`、`run.sh` | 共通の検査道具を `ui_test_support.{h,c}` に出し、`test_ui_text.c` は汎用の検査だけにした。`run.sh` に `UI_TEST_INCLUDES` / `UI_TEST_SRCS` | 親の版を採り、打刻画面の検査 (UTC 時刻、お知らせの折り返し、全状態のレイアウト) を `test_time_punch_view.c` に移す |
| `scripts/ui_fonts/gen_ui_fonts.py`、`ui_font_chars.py` | プロファイル対応 | 親の版を採り、`font_profile.json` を `true` にする |
| `cyd_ui_fonts.h`、docs (`cyd_ui_fonts.md`、`cyd_sim.md`、`cyd_display.md`) | `display_message` への言及を一般化 | 親の版を採る。子の固有の説明は `display_message.md` に残す |
| `generated/` | 親の文字だけで生成 | `merge_upstream.sh` が作り直す |
| `cyd_ui.c` / `cyd_ui.h` (Phase 2) | 設定画面の共通枠と増減行を日本語書体にした | 親の版を採る。子の打刻設定画面の英語の見出し・ページ名は ASCII なのでそのまま描ける |
| `cyd_text_input` (Phase 2) | 子の機能 (`fixed_prefix`、`force_upper`、`digits_only`、区切りの自動挿入) を取り込んだうえで、画面を `cyd_text_input_view.c` に分けて日本語化した | 親の版を採る。子の機能はすべて入っている |
| `docs/ui_japanese_plan.md` | (衝突しない) 親の計画書は `ui_japanese_platform_plan.md` という別名にした | — |

English supplement: the files listed above were reshaped so that a derived project only ever adds files (scene files, `*_view.c`, its own host tests) instead of editing shared ones. After the first merge, the shared files should merge cleanly.

## Screen Migration

旧 ASCII 書体の API (`cyd_ui_add_text` / `cyd_ui_add_button*` に倍率を渡すもの) は残します。子の打刻設定画面などがまだ使っているため、親で消すと子の取り込みでビルドが壊れます。親の画面からの使用がなくなった時点で、ヘッダーに「新規使用禁止」と書きます。

対象の規模 (2026-10 時点):

| ファイル | 行数 | 備考 |
|---|---|---|
| `cyd_system_apps/system_settings_app.c` | 2,214 | 6 ページと確認ダイアログ 5 種 |
| `cyd_system_apps/system_info_app.c` | 521 | 診断値の画面 |
| `cyd_system_apps/cyd_system_apps_common.c` | 142 | Wi-Fi・時刻同期の状態文言 |
| `services/cyd_wifi_setup/cyd_wifi_setup.c` | 604 | AP 一覧、接続中・失敗画面 |
| `framework/cyd_text_input/cyd_text_input.c` | 319 | キーボード画面 |
| `apps/app_launcher/app_launcher.c` | 236 | ランチャー |
| `apps/cyd_clock_app`、`cyd_clock_settings_app`、`cyd_clock_alarm` | 約 1,550 | 時計 (仮のメインアプリ) |

### Layout Rules

- 画面は背景の板 (`cyd_ui_add_panel` で全面) から始める。差分描画が全面を描き直さずに済む
- 設定画面の行: 見出し 16px 太字、値 24px 太字、増減ボタンは 40px 角以上。コンテンツ領域 (4〜26 行目、184px) に 1 ページ 3〜4 項目まで
- 危険操作 (NVS 初期化、アプリデータ消去、再起動) の確認ダイアログは `CYD_UI_THEME_DANGER` を使い、「実行」ボタンと「やめる」ボタンを左右に離して置く
- 動的な数値 (ヒープ、RSSI、IP アドレス) は ASCII のまま。見出しだけ日本語にする

### Simulator-Friendly Split

シミュレーターとホストテストで画面を確かめるため、画面の組み立てとサービス呼び出しを分けます (子の `cyd_time_punch_view.c` と同じ形)。

English contract: a view function takes a model struct and a screen, calls only `cyd_ui_*` / `cyd_display_*` builders, and never reads services, the clock or app globals.

対象と分け方:

| 画面 | 分け方 | 理由 |
|---|---|---|
| 設定画面共通の枠、増減行 | 元から `cyd_ui` にあり、引数だけで組める | 分割不要 |
| `cyd_text_input` | セッション構造体から組む関数を公開 (内部用ヘッダー) | 全画面から使う部品なので必ず確認したい |
| `system_settings_app` | ページごとの model (明るさ、無操作時間、時刻、Wi-Fi 状態など) を集める関数と、model から組む `system_settings_view.c` に分ける | 文言と画面数が最も多い |
| `cyd_wifi_setup` | 状態 (検索中、AP 一覧、接続中、失敗) の model から組む view に分ける | 失敗画面などは実機で出しにくい |
| `app_launcher` | `app_registry` の項目の配列から組む view | 小さいので分けやすい |
| `system_info_app` | 分けない。文言だけ置き換える | 診断値の羅列で、見た目の確認価値が低い |
| 時計まわり | 分けない。文言と書体だけ置き換える | 時計は差し替え予定の仮アプリなので手をかけない |

## Phases

| # | 内容 | 確認方法 |
|---|---|---|
| 1 | (完了) 区画の `factory` 化。描画基盤の移植 (`cyd_display`、`cyd_ui`、`cyd_ui_fonts`、`scripts/ui_fonts`、`tools/cyd_sim` の書体見本、`test_ui_text.c`)。フォントのプロファイル切り替えを追加。画面はまだ変えない | ビルド、ホストテスト、シミュレーターの見本、アプリサイズ |
| 2 | (完了) 共通部品: `cyd_ui_add_settings_chrome` (戻る、見出し、ページ送り)、`cyd_ui_add_stepper_row`、`cyd_text_input` の見出しと操作ボタン (「保存」「削除」「空白」など)。キー自体は ASCII だがアンチエイリアス書体で描く | シミュレーター、ホストテスト |
| 3 | (完了) `system_settings_app` の view 分割と日本語化、`cyd_wifi_setup` | シミュレーター (全ページ・全確認ダイアログ)、ホストテスト |
| 4 | (完了) `system_info_app`、タッチ較正、`cyd_system_apps_common.c` の状態文言 | ビルド |
| 5 | ランチャー、時計、時計設定、アラーム。アプリ名 (`app_registry` の `title`) の日本語化 | シミュレーター (ランチャー)、ビルド |
| 6 | 旧 API に「新規使用禁止」の注記、docs の更新 | — |

各 Phase で以下を記録します。

- アプリサイズと `factory` 区画 (3MB) の使用率
- ホストテストの結果

実機での確認 (描画時間、表示タスクのスタック、空きヒープの最小値、RGB565 の色の見え方) はユーザーが行います。確認項目は PR の説明に書きます。

## Phase 1 Results

2026-10-08 時点:

- ビルド (`DEV=1 idf.py build`): 成功、警告なし。ビルド時の文字チェックも実行された
- アプリサイズ: 1,251,952 バイト (main の 1,062,656 バイトから約 189KB 増)。3MB `factory` 区画の 60% が空き
- フォント表: 5 書体で 187KB (16px 標準・太字が各約 40KB、24px 太字が約 84KB、数字 2 書体で約 21KB)
- 静的 RAM: main と同じ (DRAM 84,312 バイト、IRAM 107,298 バイト)
- ホストテスト: 全件成功 (`Japanese UI text` 27 件を含む)
- シミュレーター: 書体見本 3 シーンを書き出して目視確認
- フォント生成の再現性: 子のコミット済みの表を、こちらの venv で 1 バイトも違わず再現できた。親の生成スクリプトに `body_jis_x0208: true` を与えた場合も同じ
- `merge_upstream.sh`: 子の複製で初回取り込みを試し、想定どおりの衝突で止まること、プロファイルが `false` のままだと `--regen` が止まること、`true` にすると子の表と同一のものが作り直されることを確認した

画面はまだ旧 ASCII 書体のままなので、実機の見た目は変わらない。実機で確かめるのは、区画を変えたファームウェアを通常の `flash` で書き込み、NVS (Wi-Fi 設定、タッチ較正) と内部ストレージが残ること。

## Phase 2 Results

2026-10-08 時点:

- `cyd_text_input` は、まず子の版 (1f8885e) をそのまま取り込み、それから画面を `cyd_text_input_view.c` に分けた。子と親で別々に書き換えると、取り込みのたびに衝突するため
- 設定画面の共通枠は背景を塗らない。各ページは段階 3 以降に 1 ページずつ日本語化するので、それまでの旧書体の文字 (自分の枠を黒で塗る) がテーマ色の背景の上で黒い箱にならないようにした。背景はページを日本語化するときに塗る
- 共通枠の寸法は、ページの中身 (4〜26 行目) を動かさない範囲にした。「戻る」は 64×32px、「前へ」「次へ」は 80×24px。ページ送りの高さ 24px は目安の 40px に届かない。段階 3 でページを組み直すときに、コンテンツ領域を詰めて広げるか決める
- 増減行は、行の寸法に入る一番大きい書体を選ぶ。今の設定ページの小さいボタン (24×16px) と長い英語の項目名は旧書体に戻るので、段階 3 まで読める
- キーボードのキーは 16px 太字にそろえた。32px のキーで文字に使えるのは 20px で、24px 太字では W・M・@・% が入らないため
- キーボードの見出しは 16px 太字 (呼び出し側の文字列で長さが一定しない。11 文字まで)
- ビルド: 成功、警告なし。アプリ 1,262,848 バイト (Phase 1 から約 11KB 増)。静的 DRAM +64 バイト (キーボードの見出し・ラベルのバッファを 40 バイトに広げた分)
- ホストテスト: 全件成功 (`test_ui_common.c` 41 件を追加)
- シミュレーター: `settings_*` 3 シーン、`keyboard_*` 6 シーンを追加して目視確認

残っている見た目の問題: システム情報の NVS ページ (`system_info_app.c`) は、要約の行を 3 行目に置いているので、見出しの帯 (0〜3 行目) に重なる。段階 4 で直す

実機で確かめること: 設定画面 (各ページの見出し・戻る・前へ・次へ・増減ボタン) と、Wi-Fi のパスワード入力 (伏せ字の切り替え、保存、戻る)。

## Phase 3 Results

2026-10-08 時点:

- `system_settings_app` の画面を `system_settings_view.c` に、`cyd_wifi_setup` の画面を `cyd_wifi_setup_view.c` に分けた。どちらも model だけから組み、文言はすべて view にある
- Wi-Fi・時刻同期の状態は view 独自の enum で渡す。`cyd_system_apps_common.c` の状態文言 (英語) はシステム情報の画面がまだ旧書体で使っているので、段階 4 まで残す
- 設定画面の寸法: 増減行は高さ 32px、「−」「+」は 40×32px。項目名は 6 文字 (12 列)、値は 15 列 (120px)。ページ送りは 24px のまま (幅 80px で押しやすく、中身の行を減らすほうが不便なため)
- 確認ダイアログは、問いかけ (24px、12 文字まで) と説明 2 行、「やめる」を左端、赤い実行ボタンを右端に置く
- 値の書き方: 「同期の間隔」は 2 時間以上のちょうどの時間だけ「N時間」、それ以外は「N分」(「2時間10分」は 24px で入らない)。「無操作で戻る」「Wi-Fi切断」は「しない / N秒 / N分 / N分S秒」
- タイムゾーンは項目名を上の行に出し、値の欄を 208px にした (「ニュージーランド」が入る)
- 「アプリ」page は 1 画面 5 件から 4 件にした (ボタンを 32px にしたため)。登録済みの設定画面は時計の 1 件だけ
- Wi-Fi の一覧は 1 ページ 10 件 (16px) から 5 件 (32px) にした。電波は「強い / ふつう / 弱い」で表し、channel は出さない
- Wi-Fi の接続に失敗したとき、失敗理由 (`AUTH` など) に応じた一言を出すようにした (以前は `esp_err_to_name()` だけ)
- ビルド: 成功、警告なし。アプリ 1,313,296 バイト (段階 2 から約 50KB 増。半分以上はフォントの漢字)。静的 DRAM +240 バイト (設定画面の model を static に置いた)
- ホストテスト: 全件成功 (`test_system_settings_view.c` 79 件、`test_wifi_setup_view.c` 29 件を追加)。増減の全段階の値と全タイムゾーンが枠に収まることも確認した
- シミュレーター: `sys_*` 20 シーン、`wifi_*` 10 シーンを追加して目視確認
- docs: `cyd_system_apps.md` の設定画面の節、`cyd_wifi_setup.md`、`nvs_storage.md` の表、`app_shell.md` の 1 行を更新した。ほかの docs に残る英語のボタン名 (`SYNC NOW`、`Clear App Data` など) は段階 6 で直す

実機で確かめること: 設定の全ページ (増減ボタンの長押し、前へ・次へ、戻る)、各確認ダイアログの「やめる」と実行、保存済みのネットワーク (一番上にする、削除)、Wi-Fi の設定 (一覧、再検索、ページ送り、パスワード入力、接続成功、パスワード違いでの失敗表示)。

## Phase 4 Results

2026-10-08 時点:

- `system_info_app` の画面を `system_info_view.c` に分けた。見出しと項目名は日本語、値と専門用語 (heap、RSSI、NVS の namespace 名、エラー名) は英語のまま
- ページ送りを、「次のページ名」のボタン 1 つで巡回する形から、設定画面と同じ「前へ」「次へ」(端で止まる) に揃えた。ページ名は 概要 / 診断 / Wi-Fi 診断 / 電波 (RSSI) / NVS
- Wi-Fi・時刻同期の状態の文言は、設定の view と共有した。`cyd_system_apps_common.c` の英語の状態文言 (`cyd_system_apps_format_wifi_status()` など) は使われなくなったので削除し、view の enum へ変換する共通関数 (`cyd_system_apps_view_wifi()` など) に置き換えた
- NVS ページの要約が見出しの帯に重なる問題 (段階 2 で記録) は、組み直しで解消した。読み取り失敗のときはエラー名を別の行に出す (40 バイトに収まらないため)
- タッチ補正の案内文を日本語のアンチエイリアス書体にした。補正画面はパネルへ直接描くので、`cyd_display_draw_aa_text()` に下地の色 (`solid_bg`) を渡す引数を足し、パネルから読み戻さずに混色する。描画の共有コード (`cyd_display_render.hpp`、`cyd_display.cpp`) の変更なので、子へもそのまま取り込まれる
- ビルド: 成功、警告なし。アプリ 1,328,976 バイト (段階 3 から約 16KB 増)。静的 DRAM +448 バイト (システム情報の model と NVS の namespace 名を static に置いた)
- ホストテスト: 全件成功 (`test_system_info_view.c` 44 件を追加)
- シミュレーター: `info_*` 6 シーンを追加して目視確認。タッチ補正の画面はパネルへ直接描くのでシミュレーターでは確認できない

旧 API の画面で残っているのは、ランチャーと時計 (段階 5)、`app_shell` のサンプル (`hello_app`)、Wi-Fi 無効ビルドのスタブ (`cyd_wifi_setup_stub.c`) だけ。

実機で確かめること: システム情報の全ページ (前へ・次へ、戻る、電波グラフの更新)、タッチ補正の案内文の見え方 (文字の縁が黒く浮かないか、印と重ならないか)。

## Agreed Choices

2026-10-08 にユーザーと合意した事項:

1. 区画: 3MB `factory` にする (上の Partition Layout)
2. 技術情報の画面 (システム情報の診断値、NVS ページ): 見出しと項目名は日本語、値と専門用語 (NVS、RSSI、ヒープのバイト数) は英語のまま
3. 子への取り込み: 手順 (上の Merging Into The Child) に加え、子に `scripts/ui_fonts/merge_upstream.sh` を置く。スクリプトは親で書いて子へ取り込まれる形にし、子でだけ実行する
4. PR の分け方: 当初は Phase 1〜2 と Phase 3〜6 の 2 本の予定だったが、Phase 1 (katsusand/esp32_project#13) と Phase 2 を別の PR にした (2026-10-08 にユーザーが指示)。Phase 3〜6 は 3 本目
