# CYD System Apps

## Overview

`cyd_system_apps` は、`app_shell` 上で動く小さなシステム系 foreground app をまとめるコンポーネントです。

現在は以下の app を提供します。

- `info`: firmware / IDF / chip / heap / Wi-Fi 状態を表示する
- `settings`: 設定系画面への入口を表示する
- `touch_calibration`: touch calibration を実行して遷移元へ戻る

English supplement: These apps are intentionally lightweight shell apps. They should not own background services; they only present UI and switch to domain apps such as `wifi_setup`.

## Source Layout

公開 API は `include/cyd_system_apps.h` にまとめたまま、実装は app の責務ごとに分離しています。

- `system_info_app.c` / `system_info_view.c`: 概要・診断・Wi-Fi 診断・電波・NVS の各ページの値の取得と描画、lifecycle
- `system_settings_app.c`: settings の state、描画、action dispatch、direct-view API
- `system_touch_calibration_app.c`: touch calibration の実行と戻り遷移
- `cyd_system_apps_common.c`: info / settings が共有する入力確定処理と状態表示整形
- `cyd_system_apps_internal.h`: component 内だけで使う定数・型・宣言

English supplement: The source split does not create separate ESP-IDF components. Pointer identity, app IDs, and the public API remain unchanged inside the existing `cyd_system_apps` component.

## Public API

利用するファイルでは、次のヘッダーを include します。

```c
#include "cyd_system_apps.h"
```

`info app` へは `app_shell_switch_to()` で遷移します。

```c
ESP_ERROR_CHECK(app_shell_switch_to(system_info_app_get_app()));
```

`settings app` も同様です。

```c
ESP_ERROR_CHECK(app_shell_switch_to(system_settings_app_get_app()));
```

English supplement: Return apps come from the `from_app` pointer passed to `enter()`, avoiding compile-time dependency from system apps back to the clock app.

戻る操作は `app_shell_return_to()` を通します。`from_app` が NULL のときは home app へフォールバックするため、戻り先が無い画面でも `<<` が死にません。

settings の root page の「戻る」は、**settings にどう入ったか**で振る舞いが変わります。

| 入り方 | `from_app` | root の「戻る」 |
|---|---|---|
| 起動時のタッチ長押し、NVS初期化の強制 | `NULL` | **再起動確認画面** を開く。「やめる」で page に戻り、「再起動する」で保存してから `esp_restart()` |
| 通常画面からの遷移 | 遷移元 app | これまで通り return app へ戻る |

判定は「return app が記録されていないこと」です。`app_shell_start()` は最初の app の `enter()` を `from_app = NULL` で呼ぶため、起動直後に settings へ入った場合だけこうなります。これは「戻る先が無い」という状態そのものであり、そこで `app_shell_return_to()` に任せると home app へフォールバックしますが、起動時に settings へ来た端末の home app は、たいていこの画面の奥にある Wi-Fi や backend の設定がまだ無くて動けない app です。再起動の方が、今設定した内容を実際に反映させる操作になります。

逆に通常経路では、利用者は動いている画面から来て「戻る」と言っているので、再起動を返すのは答えとしてずれます。

sub-view (「保存済みのネットワーク」や各確認画面) の「戻る」/「やめる」はこれまで通り、1 階層戻る、あるいは直接遷移で入った場合は return app へ戻ります。

English contract: the back button (「戻る」) on the settings root reboots only when settings is where the device booted to; otherwise it navigates back. Sub-views keep their own back semantics.

settings は「settings 自身が開いた画面」から戻ってきた場合だけ、戻り先を更新しません。判定は `cyd_settings_is_own_subscreen()` で、Wi-Fi Setup、Touch Calibration、登録済み app の固有設定画面が対象です。`cyd_settings_is_app_settings_screen()` は、そのうち app 固有設定画面だけを判定する内部 helper です。registry の app 本体まで対象にすると、clock から settings に入ったときに戻り先が記録されず「戻る」が効かなくなります。

保存済みSSID一覧、touch calibration消去確認、NVS消去確認は、次のAPIで次回のsettings遷移先として直接指定できます。

```c
system_settings_open_stored_ssids();
ESP_ERROR_CHECK(app_shell_switch_to(system_settings_app_get_app()));
```

確認画面を開く場合は、1行目を `system_settings_open_clear_touch_calib_confirm()` または
`system_settings_open_clear_nvs_confirm()` に置き換えます。

指定は次回の `settings app` の `enter()` で一度だけ消費されます。直接開いた画面の戻る操作は、通常のsettings pageではなく遷移元アプリへ戻ります。

English supplement: Direct-view selection is one-shot and thread-safe; callers still request the shell transition explicitly.

## Info App

`info app` は参照用の情報画面です。見出しと項目名は日本語、値と専門用語 (heap のバイト数、RSSI、NVS の namespace 名、エラー名) は英語のままです。

| 表示名 | 内容 |
|---|---|
| 概要 | アプリ名、バージョン、ESP-IDF、チップ (rev / cores)、空きヒープ、Wi-Fi の状態 |
| 診断 | 空きヒープ、最小/最大塊、Wi-Fi の失敗理由、時刻同期の状態と前回の結果、保存SSID の件数、タッチ補正 |
| Wi-Fi 診断 | 状態、利用中、最後の利用、接続時間、最長の接続、警告、失敗の理由 |
| 電波 (RSSI) | 現在の RSSI とトレンドグラフ |
| NVS | フラッシュ上の NVS namespace 一覧 |

ページは画面下部の「前へ」「次へ」で切り替えます。設定画面と同じく端で止まります (以前は「次のページ名」のボタン 1 つで巡回していました)。左上の「戻る」で、`enter()` の `from_app` として受け取った return app へ戻ります。

画面の組み立ては `system_info_view.c` にあり、`system_info_app.c` はサービスの値を model (`system_info_view_model_t`) に集めて渡すだけです。Wi-Fi・時刻同期の状態の文言は設定画面の view (`system_settings_view_wifi_text()` など) と共有します。シミュレーターの `info_*` シーンと `test/host/test_system_info_view.c` が全ページを確認します。

### RSSI Page

「電波 (RSSI)」ページは `wifi_rssi_history` が集めた RSSI を sparkline widget で描きます。スケールは -100 〜 -30 dBm 固定で、-75 dBm に赤の基準線を引いています。自動スケールにしないのは、時間をまたいで見比べられるようにするためです。

他のページがタッチ時にしか再描画しないのに対し、このページだけは `step()` で `wifi_rssi_history_get()` の `revision` を監視し、変化があったときだけ再描画します。毎回描き直さないことで、dirty-rect 差分がそのまま効きます。

**この page は Wi-Fi を起動しません。** 未接続時は「Wi-Fi はオフです」と表示し、それまでの履歴があればグラフはそのまま描きます。グラフを見るためだけに radio を起こすのは過剰という判断です。

Wi-Fi を長く保ちたい場合は設定の「ネットワーク2」page の「Wi-Fi切断」を使ってください。

English supplement: the RSSI page is the reference example of a live graph driven by a sampling service. See `docs/wifi_rssi_history.md`.

### NVS Page

フラッシュに実在する NVS namespace を、scope とエントリ数つきで一覧します。**コンポーネントの自己申告ではなくフラッシュを走査**するため、どの component も開かなくなった孤児 namespace がここに現れます。アプリを載せ替えたあとに前のデータが残っていないかを確認する用途です。

prefix を持たない namespace は `unknown` になります。ここには ESP-IDF 自身の `phy` と `nvs.net80211` も含まれるため、`unknown` は「消してよいもの」を意味しません。詳細は `docs/nvs_storage.md` を参照してください。

表示は `SYSTEM_INFO_VIEW_NVS_MAX` (10 件) までで、超えると「namespace 12 個 (10 個を表示)」のように出ます。

## Settings App

`settings app` は設定入口です。画面の組み立ては `system_settings_view.c` にあり、`system_settings_app.c` はサービスの値を model (`system_settings_view_model_t`) に集めて渡すだけです ([Simulator-Friendly Split](#simulator-friendly-split))。

表示名と内部の page ID の対応:

| 表示名 | page ID | 内容 |
|---|---|---|
| 一般 | `GENERAL` | 「画面の明るさ」「無操作で戻る」の増減、「タッチ位置の補正」(touch calibration app へ) |
| 時刻 | `TIME` | 現在時刻、日付と曜日、時計が合っているか、「タイムゾーン」の増減 |
| ネットワーク1 | `NETWORK1` | Wi-Fi の状態、「保存済みのネットワーク」(サブ画面)、「Wi-Fi を設定する」(`wifi_setup app` へ) |
| ネットワーク2 | `NETWORK2` | 「同期の間隔」「Wi-Fi切断」の増減、「今すぐ時刻を合わせる」、時刻同期の状態と前回の結果 |
| 初期化 | `NVS` | 「タッチ補正を消去」「アプリのデータを消去」「すべて初期化」 |
| アプリ | `APPS` | 設定画面を持つ app の一覧。ボタンでその app の設定画面へ |

- 左上の「戻る」: 起動時に settings へ入った場合は再起動確認画面を開く (「やめる」で page へ戻り、「再起動する」で設定を保存してから `esp_restart()`)。通常の遷移で入った場合は `enter()` の `from_app` として受け取った return app へ戻る
- 「アプリ」page は app 自体ではなく **app 固有設定への導線**であり、設定画面を持たない app は出ない。1 画面に 4 件まで (`SYSTEM_SETTINGS_VIEW_APPS_MAX`)

ページ切り替えは画面下部の「前へ」「次へ」で行います。settings は固定ページ列ではなく、有効な page を組み立てて並べます。Wi-Fi build feature が無効な場合は `NETWORK*` page 群が列ごと消えます。`APPS` page は、設定画面を持つ app が 1 つも無いときだけ消えます。

値の範囲と表示:

| 項目 | 範囲 | 表示 |
|---|---|---|
| 画面の明るさ | `100 / 75 / 50 / 40 / 30 / 25 / 20 / 15 / 10 / 5` の 10 段階 | `75%` |
| 無操作で戻る | 10 秒刻みで 0〜1800 秒 | `しない` / `50秒` / `5分` / `29分50秒` |
| 同期の間隔 | 1〜1440 分。現在値に応じて `1 / 5 / 30 / 60 / 180` 分ステップ | 2 時間未満と端数は `90分`、2 時間以上のちょうどの時間は `9時間` |
| Wi-Fi切断 | `0 / 30s / 1min / 3min / 5min / 10min / 15min / 20min / 30min / 45min / 60min` の 11 段階 | `しない` / `30秒` / `5分` |
| タイムゾーン | 内蔵プリセット 19 件 | `日本` / `米国東部` など |

「Wi-Fi切断」は無通信で Wi-Fi を落とすまでの時間 (`radio_manager` の idle timeout) です。`しない` は `radio_manager` が idle を理由に radio を解放しなくなります (内部的には待ち時間 `portMAX_DELAY`)。「無操作で戻る」は等差なので段階テーブルは持たず、加減算で扱います。これらは「−」「+」で変更すると、その場で反映されます。「今すぐ時刻を合わせる」は `time_sync` に即時同期要求を送り、進行状況も同じ page に反映されます。「時刻」page はローカル時刻表示と timezone 操作だけを持ち、Wi-Fi 非依存で使えます。保存は `settings app` を離れるタイミングで行われます。

「保存済みのネットワーク」は「ネットワーク1」page から入るサブ画面です。保存済み SSID を優先順で表示し、選択した SSID を「一番上にする」、または削除確認を経て「削除」できます。

「初期化」page の 3 つは破壊範囲の小さい順に並べています。「アプリのデータを消去」は `app_` scope の namespace だけを消して再起動します。**再起動は必須です** — app は起動時に自分の NVS データを読むため、消したあとも動き続けると古い状態を保持したままになります。

「タッチ補正を消去」は、`cyd_input` が保存しているタッチ補正だけを削除します。Wi-Fi profile や他の設定値には触れません。「すべて初期化」は確認画面を経て `nvs_flash_erase()` を実行し、保存済み Wi-Fi profile や各種設定値も含めて初期化したうえで再起動します。

確認画面は、問いかけ (16px 太字、18 文字まで) と影響の説明を出し、「やめる」を左端、実行ボタン (赤の塗り) を右端に離して置きます。

設定・システム情報・Wi-Fi の設定の画面は、文字をすべて 16px (標準と太字) にしています。項目の多い画面では、24px の文字は収まっても周りの 16px の行と釣り合わないためです (2026-10-08、実機確認でのユーザーの判断)。ボタンや行の高さは、抵抗膜タッチのため 32〜40px のままです。

NVS blob の version / size / 文字列終端などが現在 firmware の想定フォーマットと一致しない場合は、起動時に「保存データが読めません」の確認画面へ強制遷移します。この画面には「やめる」が無く、「初期化する」のあとの再起動が必要です。原因 (`nvs_health_get_summary()`、英語) は小さく表示します。

English supplement: Structurally incompatible persistent data now routes the product into a forced initialize flow instead of silently trusting or rewriting the broken payload.

### Simulator-Friendly Split

`system_settings_view.c` は model だけから画面を組み立て、サービスを呼びません。Wi-Fi や時刻同期の状態は view 独自の enum (`system_settings_view_wifi_t` など) で受け取るので、view のヘッダーは `cyd_display` 以外に依存しません。ボタンの action id もこのヘッダーにあります。

文言はすべて view にあります。`cyd_system_apps_common.c` の状態文言 (英語) はシステム情報の画面だけが使っています (段階 4 で日本語化する予定)。

シミュレーターの `sys_*` シーンと `test/host/test_system_settings_view.c` が、全ページ・全ダイアログと、増減の全段階の値が枠に収まるかを確認します。

English contract: `system_settings_view_build()` calls no service, reads no clock and touches no global state.

「Wi-Fi を設定する」で入ると、`wifi_setup app` は `from_app` として `settings app` を受け取ります。これにより、Wi-Fi 設定完了後は settings 画面へ戻ります。

### Returning From A Sub-Screen

settings が自分で開いた画面 (Wi-Fi の設定 / タッチ位置の補正 / app 固有設定) から戻ったときは、**離れたときのページを復元**します。判定は `cyd_settings_is_own_subscreen()` です。

「保存済みのネットワーク」のようなサブ*ビュー*は同じ app 内に留まるため `enter()` を通らず、もともとページが保持されていました。一方で別 app へ遷移する Wi-Fi の設定や `Clock Settings` は `enter()` を通るため、以前は無条件に `GENERAL` へ戻っていました。「ネットワーク1」から Wi-Fi 設定へ入って戻ると 1 ページ目に飛ばされる、という非対称な挙動になっていたのを揃えています。

English contract: a sub-screen round trip resumes the page it started from. Entering settings fresh from another app starts at the first page. Preserved pages that became disabled fall back to the first page via the existing enabled check.

app 固有設定がある場合は、app の registry entry に `settings_app` を付けると `APPS` page に並びます。時計アプリでは `Clock` entry の `settings_app` として `Clock Settings` が付いています。

設定画面を独立した entry として登録しないのは意図的です。そうすると launcher に `Clock` と `Clock Settings` が対等に並んでしまい、また clock を載せない製品でも設定画面だけ残り得るためです。

以前は `system_settings_set_extension()` という 1 スロットの API で、**設定を拡張できる app は 1 つだけ**でした。registry 化により件数の制限が `SYSTEM_SETTINGS_VIEW_APPS_MAX` (4) まで緩和されています。詳細は `docs/app_registry.md` を参照してください。

時計固有の alarm 設定と scheduler 診断表示は `Clock Settings` 側にあります。`cyd_system_apps` は `app_scheduler` に依存しません。

settings 画面が `wifi_setup app` から戻ってきた場合は、元の return app を保持します。これにより `clock -> settings -> wifi_setup -> settings -> 戻る` は `clock` へ戻ります。

English supplement: Settings is a menu app, not persistent configuration storage. Add storage-backed settings in dedicated components when values need to survive reboot.

## Input Handling

`settings app` の touch handler には、意図的に 2 系統あります。

1. 通常ボタン経路
   `cyd_system_apps_touch_confirmed_action()` が使われます。
   これは `PRESS` 時に候補 action を記録し、`RELEASE` 時に同じボタン上で離された場合だけ確定します。
   「戻る」「Wi-Fi を設定する」「保存済みのネットワーク」、ページ移動「前へ」「次へ」のような普通の button はこの経路です。

2. ステッパー経路
   `cyd_settings_touch_stepper_action()` が使われます。
   これは `PRESS` と `REPEAT` をそのまま action として返します。
   「−」「+」の長押し連続変更を成立させるため、`RELEASE` を待ちません。

実装上の入口は `cyd_settings_app_step()` です。最初にステッパー経路を評価し、該当しなければ通常ボタン経路へ進みます。

どちらの経路も、最終的には `cyd_settings_handle_active_screen_action()` に集約されます。この関数はサブビューが表示中ならそのビューのハンドラへ、そうでなければ**アクティブなページのハンドラだけ**へ振り分けます。ページ送り `<` / `>` だけは、どのページにも属さない shell chrome として先に処理します。

English supplement: Stepper buttons are handled on `PRESS`/`REPEAT`, while normal buttons are handled on confirmed `RELEASE`. They are not interchangeable.

### Maintenance Rule

旧構造ではページの描画、action 処理、dispatch 配線が別々の場所にあり、配線漏れによって「描画されるのに押せないボタン」が発生していました。現在は renderer / handler の実装自体は分かれていますが、ページの shell metadata と両者の binding は `CYD_SETTINGS_PAGES[]` に集約されています。サブビューだけはページとは別に、`cyd_settings_handle_active_screen_action()` の view switch で管理します。

#### 新しいページを追加するとき

1. `cyd_settings_page_t` に page ID を追加する
2. render と handle_action を実装する
3. `CYD_SETTINGS_PAGES[]` に 1 行追加する

新しい group や action が必要な場合だけ、それぞれの enum / 定義も追加します。

```c
{
    .id = CYD_SETTINGS_PAGE_FOO,
    .title = "FOO",
    .group = CYD_SETTINGS_PAGE_GROUP_FOO,
    .uses_live_status = false,
    .has_steppers = false,
    .is_enabled = NULL,                       /* NULL は常に表示 */
    .render = cyd_settings_render_foo_page,
    .handle_action = cyd_settings_handle_foo_page_action,
},
```

テーブルの並び順がページの巡回順です。ページ順、タイトル、グループ、live status ポーリングの要否、ステッパー経路の有効化、ビルド構成での有無、描画とアクション処理の binding が、この 1 行から引かれます。

`handle_action` は**アクティブなページに対してだけ**呼ばれます。したがって「他ページのハンドラに間借りさせて動かない」という状態は作れません。ハンドラ側で `s_settings_page != 自分のページ` を確認する必要もありません。

以前はこれが 7 箇所に散っており、1 箇所忘れると「描画されるのに押せないボタン」ができました。`APPS` page の不具合が実例です。

English contract: page order, shell metadata, and render/action binding have one source of truth. Page IDs and action semantics remain explicit in the enum and handlers. `handle_action` runs only for the active page.

#### サブビュー（確認画面など）を追加するとき

サブビューは**ページではなくビューで**振り分けます。`cyd_settings_handle_active_screen_action()` の `switch (s_settings_view)` に足してください。

ページ側に間借りさせてはいけません。direct view は現在、確認画面には `NVS`、保存済みSSID一覧には `NETWORK1` を対応ページとして選びますが、サブビューはページ画面を置き換えて入力全体を所有するため、dispatch は `s_settings_view` を基準にします。

#### 新しいステッパー項目を追加するとき

1. `cyd_settings_is_stepper_action()` に action id を登録する
2. テーブルの該当ページで `.has_steppers = true` にする
3. そのページの `handle_action` で処理する

過去の不具合は、旧構造で 3 の配線が漏れていたため発生しました。

#### 検証方法

- `ESP_STATIC_ASSERT` が page enum 件数とテーブル行数の不一致を build 時に検出する
- render / handle_action が未設定なら `ESP_ERR_INVALID_STATE` を返し、空画面や無反応として隠さない
- review 時は `CYD_SETTINGS_PAGES[]` の page ID が重複していないことと、並び順が意図どおりであることを確認する
- サブビュー追加時は `cyd_settings_handle_active_screen_action()` の view switch への配線を確認する

English supplement: the table count is checked at compile time, required callbacks fail loudly, and only sub-view routing remains a separate dispatch list.

### Page Composition Rule

`NETWORK` page は 1 枚固定ではなく、`NETWORK1`, `NETWORK2`, ... の連番 page 群として増やせる前提です。

- Wi-Fi 依存 page は `APP_WIFI_STA_ENABLED` に連動して enable/disable する
- page title / page count / prev-next navigation は、有効 page 列から動的に決める
- 「保存済みのネットワーク」の direct view のような network 遷移は、対応 page が enable のときだけ使う

English supplement: Treat settings pages as a composed list of enabled page definitions. This keeps Wi-Fi-free products natural while allowing future `NETWORK3+` expansion without reworking the navigation model.
