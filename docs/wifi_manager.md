# Wi-Fi Manager

## Overview

`wifi_manager` は、Wi-Fi 接続状態と接続要求をまとめる低レベルの接続管理コンポーネントです。

`wifi_manager_start()` は manager task を常駐させますが、通常起動では Wi-Fi radio を開始しません。初期化後は `WIFI_MANAGER_STATE_OFF` で待機します。内部的には `wifi_manager_acquire()` / `wifi_manager_release()` により active user を管理し、active user が 1 つ以上ある間だけ manager が Wi-Fi を ON にして接続を維持します。

SSID 未設定、または起動時の明示的な setup shortcut では `WIFI_MANAGER_STATE_SETUP_REQUIRED` に遷移します。設定済み SSID へ接続できない場合は `WIFI_MANAGER_STATE_FAILED` になり、Wi-Fi setup app に入るかどうかは foreground app (`cyd_clock_app`) が判断します。接続後は STA 状態を定期監視し、切断時は再接続を試みます。manager 自身は LCD を触りません。

English supplement: `wifi_manager` owns Wi-Fi STA state and connection policy, but new application-level clients should usually go through `radio_manager` rather than calling it directly.

## Public API

利用するファイルでは、次のヘッダーを include します。

```c
#include "wifi_manager.h"
```

起動は `wifi_manager_start()` で行います。内部で FreeRTOS task と EventGroup を作ります。通常起動では接続を開始せず、manager task は `OFF` 状態で要求を待ちます。

```c
ESP_ERROR_CHECK(wifi_manager_start());
```

通常のアプリ/サービスは `wifi_manager` を直接 acquire せず、`radio_manager` から通信利用権を取得します。`wifi_manager` は STA 接続の実処理を担当し、radio 利用順や idle timeout は `radio_manager` が管理します。

English supplement: New communication services should prefer `radio_manager` leases. Direct `wifi_manager` acquire/release is now a lower-level integration point used by `radio_manager`.

```c
radio_manager_request_t request = {
    .client = RADIO_MANAGER_CLIENT_TIME_SYNC,
    .required = RADIO_MANAGER_CAP_INTERNET,
    .max_hold_ticks = pdMS_TO_TICKS(30000),
};
radio_manager_lease_t lease = { 0 };

ESP_ERROR_CHECK(radio_manager_acquire(&request, &lease, pdMS_TO_TICKS(10000)));
/* 通信処理 */
ESP_ERROR_CHECK(radio_manager_release(&lease));
```

`wifi_manager_acquire()` / `wifi_manager_release()` は現在も公開されていますが、これは `radio_manager` や低レベル統合のための API と考えてください。新しい HTTP client や別用途の通信 service を追加する場合は、まず `radio_manager` 側へ client / capability を追加する方針を優先します。

接続済み状態を待つ場合は `wifi_manager_wait_connected()` を使えます。ただし `OFF` 状態では自動的に ON にせず、`ESP_ERR_INVALID_STATE` を返します。この API は主に `radio_manager` 内部や低レベル切り分け用です。

English supplement: `wifi_manager_wait_connected()` is still useful for lower-level integration and diagnostics, but it is not the primary application-level entry point.

明示的に Wi-Fi を ON にするだけなら `wifi_manager_enable()` を使います。これは非同期の要求で、active user は増やさず、接続完了を待ちません。

`wifi_manager_request_connection_without_setup()` と `wifi_manager_request_connection_without_setup_async()` は、時計画面の `SYNC NOW` や未同期失敗画面の retry のように、setup UI へ遷移させずに保存済み credential で再試行したい経路で使います。

Wi-Fi を明示的に OFF にする場合は `wifi_manager_disable()` を使えます。ただし通常は `release` による自動 OFF を優先します。接続済み、失敗後、または OFF のときだけ disable でき、setup UI 実行中の disable は `ESP_ERR_INVALID_STATE` です。

Status LED は Wi-Fi credential と最新の接続結果を表します。SSID 未設定では赤点灯、SSID 設定済みで最新接続が失敗または未成功なら赤点滅、最新接続が成功して IP を取得できた後は緑点灯です。Wi-Fi radio を OFF に戻しても、直近の接続結果は維持します。

English supplement: The status LED represents Wi-Fi reachability, not NTP/time synchronization state.

foreground app が Wi-Fi setup app に入る場合は、setup app 側が `wifi_manager_begin_setup()` と `wifi_manager_complete_setup()` を呼びます。

```c
ESP_ERROR_CHECK(app_shell_switch_to(cyd_wifi_setup_get_app()));
```

Wi-Fi setup を `<<` などでキャンセルした場合、これは接続失敗として扱いません。既存 credential や直近の接続成功状態は維持し、status LED の base pattern も失敗側へ更新しません。

現在状態は `wifi_manager_get_state()` で取得できます。

```c
wifi_manager_state_t state = WIFI_MANAGER_STATE_STOPPED;
ESP_ERROR_CHECK(wifi_manager_get_state(&state));
```

全画面 setup UI が実行中かどうかは `wifi_manager_is_setup_active()` で判定します。この関数は `WIFI_MANAGER_STATE_SETUP_RUNNING` の間だけ `true` を返します。

English supplement: `SETUP_REQUIRED` is only a request/state signal. `SETUP_RUNNING` means an application has actually entered the full-screen setup flow.

Wi-Fi が有効化中かどうかは `wifi_manager_is_enabled()` で判定できます。`STOPPED` と `OFF` では `false` を返します。

診断表示向けには `wifi_manager_get_active_users()`、`wifi_manager_get_last_user()`、`wifi_manager_get_connected_duration_seconds()`、`wifi_manager_get_connected_duration_high_water_seconds()`、`wifi_manager_get_warning()` を使えます。現在の通常通信経路で使う user bit は `WIFI_MANAGER_USER_RADIO_MANAGER` です。

## State Model

状態は `wifi_manager_state_t` で表します。

```c
typedef enum {
    WIFI_MANAGER_STATE_STOPPED = 0,
    WIFI_MANAGER_STATE_INIT,
    WIFI_MANAGER_STATE_OFF,
    WIFI_MANAGER_STATE_CONNECTING,
    WIFI_MANAGER_STATE_CONNECTED,
    WIFI_MANAGER_STATE_RECONNECTING,
    WIFI_MANAGER_STATE_FAILED,
    WIFI_MANAGER_STATE_SETUP_REQUIRED,
    WIFI_MANAGER_STATE_SETUP_RUNNING,
} wifi_manager_state_t;
```

通常の流れは以下です。

1. `wifi_manager_start()` が task を作り、`INIT` へ入る
2. 通常起動では Wi-Fi を開始せず、`OFF` で要求を待つ
3. 起動時のタッチ IRQ が LOW なら、明示的な setup shortcut として `SETUP_REQUIRED` に入る
4. `radio_manager` 経由の acquire、retry API、または `wifi_manager_enable()` により、設定済み credential で接続を試みる
5. SSID 未設定なら `SETUP_REQUIRED`、設定済み credential で接続できない場合は `FAILED` に入る
   `wifi_manager_request_connection_without_setup()` からの要求では、setup UI を直接起動せず、失敗状態で待つ
6. 接続後は `CONFIG_WIFI_MANAGER_MONITOR_INTERVAL_MS` ごとに STA 状態を監視する
7. 切断を検出したら `RECONNECTING` として再接続し、失敗したら `FAILED` に入る
8. `wifi_manager_release()` で active user が 0 になったら STA を停止し、manager task は常駐したまま `OFF` に戻る

English supplement: The manager task is long-lived. `STOPPED` means the manager task itself is not running; `OFF` means the manager task is alive and Wi-Fi STA/radio is stopped.

## Concurrency Policy

Wi-Fi 利用者は内部的には bit mask で調停されます。各 low-level client は `wifi_manager_user_t` bit で `wifi_manager_acquire()` し、処理完了時に同じ bit を `wifi_manager_release()` します。active user が残っている間は Wi-Fi を維持し、最後の user が release したときだけ OFF に戻します。

現在の通常通信経路では `radio_manager` が `WIFI_MANAGER_USER_RADIO_MANAGER` を使って `wifi_manager` を保持します。新しい通信コンポーネントはまず `radio_manager` に client / capability を追加し、`wifi_manager` への直接 user 追加は低レベル統合が必要な場合だけ検討してください。

English supplement: Active users are represented as bits, not a nesting counter per user. In the current architecture, `radio_manager` is the normal owner of Wi-Fi lifetime for communication features.

## Configuration

主な設定項目は `idf.py menuconfig` の `Wi-Fi Manager` から変更できます。

- `CONFIG_WIFI_MANAGER_TASK_STACK_SIZE`: manager task の stack size
- `CONFIG_WIFI_MANAGER_TASK_PRIORITY`: manager task の priority
- `CONFIG_WIFI_MANAGER_MONITOR_INTERVAL_MS`: 接続後の STA 状態監視周期
- `CONFIG_WIFI_MANAGER_STACK_LOG_INTERVAL_MS`: stack high-water mark ログの最小間隔
- `CONFIG_WIFI_MANAGER_SCAN_RETRY_ATTEMPTS`: 保存済み SSID が見つからない場合の scan retry 回数
- `CONFIG_WIFI_MANAGER_SCAN_RETRY_DELAY_MS`: scan retry 間の待ち時間

`wifi_manager_try_connect()` は `CONFIG_ESP32_WIFI_STA_CONNECT_TIMEOUT_MS` を使って接続完了を待ちます。この値は `ESP32 Wi-Fi STA` 側の Kconfig です。

English supplement: The manager no longer runs the display setup flow itself, but its stack should still have room for Wi-Fi orchestration and event handling.

## Dependencies

`wifi_manager` は以下のプロジェクトコンポーネントに依存します。

- `cyd_status_led`
- `cyd_wifi_setup`
- `esp32_wifi_sta`

`cyd_wifi_setup` は現在、設定済み credential 接続 helper と shell 上の `wifi setup app` として参照しています。全画面 setup UI の描画 owner は `app_shell` task です。

起動順としては、`main/app_main()` から `system_boot_start()` に入り、`system_boot` の中で `cyd_display_init()` と `cyd_input_init()` が完了した後に `wifi_manager_start()` を呼びます。
