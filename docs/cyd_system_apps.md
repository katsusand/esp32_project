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

- `system_info_app.c`: `INFO` / `DIAG` / `DIAG2` / `RSSI` の描画と lifecycle
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

settings は「settings 自身が開いた画面」から戻ってきた場合だけ、戻り先を更新しません。判定は `cyd_settings_is_own_subscreen()` で、Wi-Fi Setup、Touch Calibration、登録済み app の固有設定画面が対象です。`cyd_settings_is_app_settings_screen()` は、そのうち app 固有設定画面だけを判定する内部 helper です。registry の app 本体まで対象にすると、clock から settings に入ったときに戻り先が記録されず `<<` が効かなくなります。

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

`info app` は参照用の情報画面です。

- app 名 / version
- ESP-IDF version
- chip revision / core count
- free heap
- Wi-Fi manager state / active users / last user
- Wi-Fi connected duration / max duration / warning / last failure
- Wi-Fi RSSI トレンドグラフ

ページは `INFO -> DIAG -> DIAG2 -> RSSI` の順に、画面下部のボタンで巡回します。

左上の `<<` ボタンで、`enter()` の `from_app` として受け取った return app へ戻ります。

### RSSI Page

`RSSI` ページは `wifi_rssi_history` が集めた RSSI を sparkline widget で描きます。スケールは -100 〜 -30 dBm 固定で、-75 dBm に赤の基準線を引いています。自動スケールにしないのは、時間をまたいで見比べられるようにするためです。

他のページがタッチ時にしか再描画しないのに対し、このページだけは `step()` で `wifi_rssi_history_get()` の `revision` を監視し、変化があったときだけ再描画します。毎回描き直さないことで、dirty-rect 差分がそのまま効きます。

`APP_WIFI_STA=0` ビルドや Wi-Fi 未接続時は、グラフの代わりに「データなし」を表示します。

English supplement: the RSSI page is the reference example of a live graph driven by a sampling service. See `docs/wifi_rssi_history.md`.

## Settings App

`settings app` は設定入口です。

- `GENERAL` page
  `LcdBrightness`: LCD バックライトの明るさを変更する
  `Touch Calib`: touch calibration app へ切り替える
- `TIME` page
  現在時刻表示
  現在日付表示
  `Timezone`: POSIX timezone 設定をプリセットから切り替える
  RTC / 内部時計ベースの状態表示
- `NETWORK1` page
  現在の Wi-Fi 状態表示
  `Stored SSIDs`: 保存済みSSID一覧、優先化、削除
  `Wi-Fi Setup`: `wifi_setup app` へ切り替える
- `NETWORK2` page
  `TimeSyncInterval`: NTP 同期間隔を分単位で変更する
  `SYNC NOW`: その場で同期を要求する
  NTP / 時刻同期状態表示
- `NVS` page
  `Clear Touch Calib`: 保存済みタッチ補正だけ消す
  `Initialize NVS`: 保存済み NVS データを全消去して再起動する
- `APPS` page
  設定画面を持つ app の一覧。ボタンでその app の設定画面へ遷移する
  app 自体ではなく **app 固有設定への導線**であり、設定画面を持たない app は出ない
- `<<`: `enter()` の `from_app` として受け取った return app へ戻る

ページ切り替えは画面下部の `<` / `>` ボタンで行います。settings は固定ページ列ではなく、有効な page を組み立てて並べます。Wi-Fi build feature が無効な場合は `NETWORK*` page 群が列ごと消えます。`APPS` page は、設定画面を持つ app が 1 つも無いときだけ消えます。

`LcdBrightness` は `100 / 75 / 50 / 40 / 30 / 25 / 20 / 15 / 10 / 5` の 10 段階です。`TimeSyncInterval` は 1 から 1440 分の範囲で、現在値に応じて `1 / 5 / 30 / 60 / 180` 分ステップで増減します。`Timezone` は内蔵プリセットから切り替えます。これらは `-` / `+` ボタンで変更すると、その場で反映されます。`SYNC NOW` は `NETWORK` 側から `time_sync` に即時同期要求を送り、進行状況も `NETWORK` page 上に反映されます。`TIME` page はローカル時刻表示と timezone 操作だけを持ち、Wi-Fi 非依存で使えます。保存は `settings app` を離れるタイミングで行われます。

`Stored SSIDs` は `NETWORK1` page から入るサブ画面です。保存済みSSIDを優先順で表示し、選択したSSIDを最優先にしたり、削除確認を経て削除したりできます。

`NVS` page の `Clear Touch Calib` は、`cyd_input` が保存しているタッチ補正だけを削除します。Wi-Fi profile や他の設定値には触れません。`Initialize NVS` は確認画面を経て `nvs_flash_erase()` を実行し、保存済み Wi-Fi profile や各種設定値も含めて初期化したうえで再起動します。

NVS blob の version / size / 文字列終端などが現在 firmware の想定フォーマットと一致しない場合は、起動時に warning 付きの `Initialize NVS` 画面へ強制遷移します。この場合、通常の clock home には入らず、`Initialize` 実行後の再起動が必要です。

English supplement: Structurally incompatible persistent data now routes the product into a forced initialize flow instead of silently trusting or rewriting the broken payload.

`Wi-Fi Setup` へ入ると、`wifi_setup app` は `from_app` として `settings app` を受け取ります。これにより、Wi-Fi 設定完了後は settings 画面へ戻ります。

### Returning From A Sub-Screen

settings が自分で開いた画面（`Wi-Fi Setup` / `Touch Calib` / app 固有設定）から戻ったときは、**離れたときのページを復元**します。判定は `cyd_settings_is_own_subscreen()` です。

`Stored SSIDs` のようなサブ*ビュー*は同じ app 内に留まるため `enter()` を通らず、もともとページが保持されていました。一方で別 app へ遷移する `Wi-Fi Setup` や `Clock Settings` は `enter()` を通るため、以前は無条件に `GENERAL` へ戻っていました。`NETWORK1` から Wi-Fi 設定へ入って戻ると 1 ページ目に飛ばされる、という非対称な挙動になっていたのを揃えています。

English contract: a sub-screen round trip resumes the page it started from. Entering settings fresh from another app starts at the first page. Preserved pages that became disabled fall back to the first page via the existing enabled check.

app 固有設定がある場合は、app の registry entry に `settings_app` を付けると `APPS` page に並びます。時計アプリでは `Clock` entry の `settings_app` として `Clock Settings` が付いています。

設定画面を独立した entry として登録しないのは意図的です。そうすると launcher に `Clock` と `Clock Settings` が対等に並んでしまい、また clock を載せない製品でも設定画面だけ残り得るためです。

以前は `system_settings_set_extension()` という 1 スロットの API で、**設定を拡張できる app は 1 つだけ**でした。registry 化により件数の制限が `CYD_SETTINGS_APPS_VISIBLE_MAX` まで緩和されています。詳細は `docs/app_registry.md` を参照してください。

時計固有の alarm 設定と scheduler 診断表示は `Clock Settings` 側にあります。`cyd_system_apps` は `app_scheduler` に依存しません。

settings 画面が `wifi_setup app` から戻ってきた場合は、元の return app を保持します。これにより `clock -> settings -> wifi_setup -> settings -> <<` は `clock` へ戻ります。

English supplement: Settings is a menu app, not persistent configuration storage. Add storage-backed settings in dedicated components when values need to survive reboot.

## Input Handling

`settings app` の touch handler には、意図的に 2 系統あります。

1. 通常ボタン経路
   `cyd_system_apps_touch_confirmed_action()` が使われます。
   これは `PRESS` 時に候補 action を記録し、`RELEASE` 時に同じボタン上で離された場合だけ確定します。
   `<<`、`Wi-Fi Setup`、`Stored SSIDs`、ページ移動 `<` / `>` のような普通の button はこの経路です。

2. ステッパー経路
   `cyd_settings_touch_stepper_action()` が使われます。
   これは `PRESS` と `REPEAT` をそのまま action として返します。
   `-` / `+` の長押し連続変更を成立させるため、`RELEASE` を待ちません。

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
- `Stored SSIDs` の direct view のような network 遷移は、対応 page が enable のときだけ使う

English supplement: Treat settings pages as a composed list of enabled page definitions. This keeps Wi-Fi-free products natural while allowing future `NETWORK3+` expansion without reworking the navigation model.
