# CYD Clock App

## Overview

`cyd_clock_app` は、CYD 画面に現在時刻を表示するアプリケーションコンポーネントです。

現在は `app_shell` 上で動く foreground app です。周期的に `time()` / `localtime_r()` を読み、`cyd_ui` 経由で時計画面を更新します。時刻表示部分のタップで 24時間表示と 12時間表示を切り替えます。長押しはタップをキャンセルするだけで、Wi-Fi 設定への shortcut ではありません。

LCD owner は `app_shell` task であり、`cyd_clock_app` 自身はその task 上で実行されます。`wifi_connection` が `SETUP_REQUIRED` になった場合、まだ一度も NTP 同期に成功していない状態なら自動的に Wi-Fi setup へ入ります。一度でも NTP 同期に成功した後は、通常の接続失敗だけでは自動遷移せず、時計画面を維持します。

English supplement: `cyd_clock_app` is no longer a standalone task. It is a shell-managed foreground app that can request transitions to other apps.

アラーム (`cyd_clock_alarm`) と設定画面 (`cyd_clock_settings_app`) は時計の一部です。どちらも `cyd_clock_app_register()` がまとめてインストールするので、時計を載せない製品やメインアプリを差し替えた製品には入りません。

English contract: the clock owns its alarm and its settings screen. Registering the clock installs both; nothing else installs them.

## Public API

利用するファイルでは、次のヘッダーを include します。

```c
#include "cyd_clock_app.h"
```

起動エントリとしては `cyd_clock_app_get_app()` を使います。

```c
ESP_ERROR_CHECK(app_shell_start(cyd_clock_app_get_app()));
```

`cyd_clock_app_register()` は時計を `app_registry` に登録し、あわせて設定画面を時計の子として紐付け、`cyd_clock_alarm_register()` でアラームをインストールします。アラームが `app_scheduler` を使うため、`app_scheduler_init()` の後に呼びます。

アラームの登録だけが失敗した場合も時計の登録は済んでおり、時計として動作します。戻り値はエラーになるので、composition はそれを記録して起動を続けます。

`cyd_clock_app` は `app_shell_app_t` を返すだけで、task を自前で作りません。停止 API や表示形式を外部から設定する API はありません。

## Display Behavior

時計画面は以下の要素で構成されます。画面の組み立ては `cyd_clock_view.c` にあり、`cyd_clock_app.c` は時刻・同期・Wi-Fi・アラームの状態を model (`cyd_clock_view_model_t`) に集めて渡します。

- 日付: `2026年10月8日（木）`
- 12 時間表示のときは「午前」「午後」(数字の書体に AM/PM が無いため、時刻の上に 16px で出す)
- 時刻: `HH:MM:SS` (48px の数字)。64px にしないのは、時刻によって画面幅に入ったり入らなかったりし、毎秒の描き直しで大きさが変わってしまうため
- 時刻同期: `時刻同期: まだ` / `時刻同期: 失敗` / `時刻同期: 10/8 09:41 成功`
- Wi-Fi の状態: `Wi-Fi: 接続済み` など
- 「設定」「情報」ボタン
- アラームのボタン: 「アラーム オフ」→「アラーム 1」→「アラーム 2」→「アラーム 1と2」の順に有効状態を切り替える。有効なときは注意色 (橙)
- SD カードのアイコン（右上の角。カードが未挿入・未フォーマット・空き容量なし・異常のときだけ）。[SD Card Status](sd_card_status.md) が決める状態で、正常なときは何も描きません。時計は毎秒描き直すので、カードを抜き差ししても 1 秒以内に反映されます

時刻の部分をタップすると 12 / 24 時間表示を切り替えます (位置は `CYD_CLOCK_VIEW_TIME_*`)。

ローカル時刻の年が 2024 年未満の場合、未同期とみなし、時刻欄は `--:--:--`、日付欄は「時刻を合わせています…」を表示します。同期の状態は `time_sync_get_last_success_at()` と `time_sync_get_last_attempt_status()` から決めます。

アラームのボタンは `cyd_clock_alarm_set_enabled()` で有効/無効だけを切り替えます。時刻と曜日は時計の設定画面 (`cyd_clock_settings_app`) で変更します ([cyd_clock_alarm.md](cyd_clock_alarm.md))。

「設定」は `settings app`、「情報」は `info app` へ切り替えます。戻り先は `app_shell` が `enter()` に渡す `from_app` により、遷移先 app 側で保持されます。

Wi-Fi に接続できないときの画面は「Wi-Fi に接続できません」と理由を出し、「もう一度」(保存済みの AP へ再接続) と「Wi-Fi を設定」(Wi-Fi の設定へ) を選ばせます。再接続中は「Wi-Fi に接続しています…」と、探している / 試している SSID を出します。

シミュレーターの `clock*` シーンと `test/host/test_launcher_clock_view.c` が確認します。テストは 1 日の全分を 12 / 24 時間表示で組み、時刻が縮まずに入ることを見ます。

English supplement: Time sync detection is intentionally simple. The app treats years before 2024 as unsynchronized, while the status line is based on `time_sync`'s last-attempt/last-success status APIs.

## Touch Behavior

`cyd_clock_app` は `cyd_input_read_event()` を非ブロッキングで読み、タップと長押しを検出します。

タップ判定は以下の条件です。

- `PRESS` を受けている
- 途中で `LONG_PRESS` を受けていない
- `RELEASE` 位置が press 位置から X/Y ともに `24` px 以内

時刻表示部分でタップが確定すると `s_clock_use_24_hour` を反転し、次の描画で 24時間/12時間表示を切り替えます。

English supplement: The 12H/24H toggle only accepts taps whose press and release both land inside the time display grid rectangle.

長押しが出た場合は tap をキャンセルします。時刻表示の長押しでは 12H/24H 表示切り替えも Wi-Fi setup への遷移も行いません。

English supplement: Long press and large movement cancel the tap. Manual Wi-Fi setup is reached from Settings, not by long-pressing the clock face.

## Display Mode

表示モードは内部状態として管理します。

- `CYD_CLOCK_APP_MODE_CLOCK`: 通常の時計画面
- `CYD_CLOCK_APP_MODE_WIFI_FAILED`: 未同期時の接続失敗画面
- `CYD_CLOCK_APP_MODE_WIFI_RETRYING`: 保存済み AP 自動再試行の進捗画面

Wi-Fi setup 自体は `cyd_clock_app` 内部モードではなく、別 app (`cyd_wifi_setup_get_app()`) へ切り替えて実行します。

English supplement: Clock app owns clock/failure/retrying modes only. Wi-Fi setup is a separate app switched by the shell.

## Configuration

主な設定項目は `idf.py menuconfig` の `CYD Clock App` から変更できます。

- `CONFIG_CYD_CLOCK_APP_UPDATE_INTERVAL_MS`: 時計画面の更新周期
- `CONFIG_CYD_CLOCK_APP_STACK_LOG_INTERVAL_MS`: stack high-water mark ログの最小間隔

現在の stack high-water log は `cyd_clock_app` 名義で出ていますが、実際に観測しているのは `app_shell` task 上で実行中の shared stack です。

## Dependencies

`cyd_clock_app` は以下のコンポーネントに依存します。

- `app_diagnostics`
- `app_registry`
- `app_shell`
- `cyd_clock_alarm`
- `cyd_clock_settings_app`
- `cyd_display`
- `cyd_input`
- `cyd_system_apps`
- `cyd_ui`
- `cyd_wifi_setup`
- `time_sync`
- `time_tick`
- `wifi_connection`

このプロジェクトでは、`main/app_main()` は `cyd_clock_composition_start()` を呼びます。表示・入力は`system_boot`、時計固有serviceと必要に応じたWi-Fi/time sync起動はcompositionが担当し、その後に`app_shell_start(cyd_clock_app_get_app())`を呼びます。
