# CYD Clock App

## Overview

`cyd_clock_app` は、CYD 画面に現在時刻を表示するアプリケーションコンポーネントです。

現在は `app_shell` 上で動く foreground app です。周期的に `time()` / `localtime_r()` を読み、`cyd_ui` 経由で時計画面を更新します。時刻表示部分のタップで 24時間表示と 12時間表示を切り替えます。長押しはタップをキャンセルするだけで、Wi-Fi 設定への shortcut ではありません。

LCD owner は `app_shell` task であり、`cyd_clock_app` 自身はその task 上で実行されます。`wifi_manager` が `SETUP_REQUIRED` になった場合、明示的な起動時 setup shortcut、またはまだ一度も NTP 同期に成功していない状態なら自動的に Wi-Fi setup へ入ります。一度でも NTP 同期に成功した後は、通常の接続失敗だけでは自動遷移せず、時計画面を維持します。

English supplement: `cyd_clock_app` is no longer a standalone task. It is a shell-managed foreground app that can request transitions to other apps.

## Public API

利用するファイルでは、次のヘッダーを include します。

```c
#include "cyd_clock_app.h"
```

起動エントリとしては `cyd_clock_app_get_app()` を使います。

```c
ESP_ERROR_CHECK(app_shell_start(cyd_clock_app_get_app()));
```

`cyd_clock_app` は `app_shell_app_t` を返すだけで、task を自前で作りません。停止 API や表示形式を外部から設定する API はありません。

## Display Behavior

時計画面は以下の要素で構成されます。

- タイトル: `CYD CLOCK`
- 時刻: `HH:MM:SS` または `HH:MM:SS AM/PM`
- 日付: `YYYY-MM-DD`
- 状態: 最後の NTP 同期状態
- Wi-Fi 状態: `wifi: off`、`wifi: connecting`、`wifi: setup needed` など
- `SYNC NOW` ボタン
- `SETTINGS` / `INFO` ボタン

`SYNC NOW` は Wi-Fi が `connected`、`failed`、`off`、`setup needed` のときだけ有効です。ただし `time_sync_is_busy()` が `true` の間は、Wi-Fi が `connected` でも NTP 同期処理中または retry 待ちとして無効表示になり、タッチしても action は発火しません。

ローカル時刻の年が 2024 年未満の場合、未同期とみなし、時刻欄は `--:--:--`、日付欄は `Waiting for NTP` を表示します。状態欄は `time_sync_get_last_success_at()` と `time_sync_get_last_attempt_status()` を使い、`sync: pending`、`sync: failed`、または `sync: MM-DD HH:MM OK` を表示します。

`SETTINGS` は `settings app`、`INFO` は `info app` へ切り替えます。戻り先は `app_shell` が `enter()` に渡す `from_app` により、遷移先 app 側で保持されます。

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

- `cyd_display`
- `cyd_input`
- `cyd_ui`
- `cyd_wifi_setup`
- `time_sync`
- `wifi_manager`
- `app_shell`

このプロジェクトでは、`main/app_main()` は `system_boot_start(cyd_clock_app_get_app())` を呼びます。表示、入力、必要に応じた Wi-Fi/time sync 起動は `system_boot` 側で行われ、その後に `app_shell_start(...)` が呼ばれます。
