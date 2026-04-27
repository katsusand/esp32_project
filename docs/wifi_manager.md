# Wi-Fi Manager

## Overview

`wifi_manager` は、Wi-Fi 接続状態と接続要求をまとめる上位コンポーネントです。

`wifi_manager_start()` は manager task を常駐させますが、通常起動では Wi-Fi radio を開始しません。初期状態は `WIFI_MANAGER_STATE_OFF` です。通信が必要なコンポーネントは `wifi_manager_request_connection()` を呼び、OFF 状態なら manager が Wi-Fi を ON にして接続を試みます。使用後に Wi-Fi を止める場合は `wifi_manager_disable()` を明示的に呼びます。

設定済み SSID へ接続できない場合は `WIFI_MANAGER_STATE_FAILED` または `WIFI_MANAGER_STATE_SETUP_REQUIRED` に遷移します。Wi-Fi setup app に入るかどうかは foreground app (`cyd_clock_app`) が判断します。接続後は STA 状態を定期監視し、切断時は再接続を試みます。manager 自身は LCD を触りません。

English supplement: `wifi_manager` is a network orchestration layer, not a display owner. It keeps the manager task alive while allowing the Wi-Fi STA/radio to be OFF.

## Public API

利用するファイルでは、次のヘッダーを include します。

```c
#include "wifi_manager.h"
```

起動は `wifi_manager_start()` で行います。内部で FreeRTOS task と EventGroup を作ります。通常起動では接続を開始せず、manager task は OFF 状態で要求を待ちます。

```c
ESP_ERROR_CHECK(wifi_manager_start());
```

通信要求として Wi-Fi 接続を開始または待機する場合は `wifi_manager_request_connection()` を使います。OFF 状態なら Wi-Fi を ON にして、接続完了を待ちます。接続できない場合は setup required 状態になります。

```c
esp_err_t err = wifi_manager_request_connection(pdMS_TO_TICKS(60000));
if (err == ESP_OK) {
    /* Run network-dependent work. */
    ESP_ERROR_CHECK(wifi_manager_disable());
}
```

接続に失敗しても setup UI に遷移させたくないバックグラウンド処理では、`wifi_manager_request_connection_without_setup()` を使います。OFF 状態なら接続を試みますが、失敗時は Wi-Fi を OFF に戻して `ESP_FAIL` または timeout を返します。

English supplement: Use `wifi_manager_request_connection_without_setup()` for background jobs that must not take over the display with Wi-Fi setup UI.

明示的に Wi-Fi を ON にするだけなら `wifi_manager_enable()` を使います。これは非同期の要求で、接続完了を待ちません。

接続済み状態を待つだけの場合は `wifi_manager_wait_connected()` を使えます。ただし OFF 状態では自動的に ON にせず、`ESP_ERR_INVALID_STATE` を返します。

Wi-Fi を明示的に OFF にする場合は `wifi_manager_disable()` を使います。接続中または setup UI 実行中の disable は、初期設計では `ESP_ERR_INVALID_STATE` です。

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
3. 起動時のタッチ IRQ が LOW なら、明示的な setup shortcut として setup mode を開始する
4. `wifi_manager_request_connection()`、`wifi_manager_request_connection_without_setup()`、または `wifi_manager_enable()` により、設定済み credential で接続を試みる
5. 接続できない場合は `FAILED` または `SETUP_REQUIRED` に入る
   `wifi_manager_request_connection_without_setup()` からの要求では、setup UI を直接起動せず、失敗状態または `OFF` に戻る
6. 接続後は `CONFIG_WIFI_MANAGER_MONITOR_INTERVAL_MS` ごとに STA 状態を監視する
7. 切断を検出したら `RECONNECTING` として再接続し、失敗したら `SETUP_REQUIRED` に入る
8. `wifi_manager_disable()` が呼ばれたら STA を停止し、manager task は常駐したまま `OFF` に戻る

English supplement: The manager task is long-lived. `STOPPED` means the manager task itself is not running; `OFF` means the manager task is alive and Wi-Fi STA/radio is stopped.

## Concurrency Policy

初期設計では、複数の Wi-Fi 利用タスクを同時に調停しません。Wi-Fi を使うコンポーネントは、自分の処理の前に `wifi_manager_request_connection()` を呼び、処理後に `wifi_manager_disable()` を呼べます。ただし、別のタスクが同時に通信していないことをアプリケーション側で保証してください。

English supplement: This initial design does not implement client acquire/release or reference counting. Introduce a client usage counter before adding concurrent network users that may independently enable and disable Wi-Fi.

## Configuration

主な設定項目は `idf.py menuconfig` の `Wi-Fi Manager` から変更できます。

- `CONFIG_WIFI_MANAGER_TASK_STACK_SIZE`: manager task の stack size
- `CONFIG_WIFI_MANAGER_TASK_PRIORITY`: manager task の priority
- `CONFIG_WIFI_MANAGER_MONITOR_INTERVAL_MS`: 接続後の STA 状態監視周期
- `CONFIG_WIFI_MANAGER_STACK_LOG_INTERVAL_MS`: stack high-water mark ログの最小間隔

`wifi_manager_try_connect()` は `CONFIG_ESP32_WIFI_STA_CONNECT_TIMEOUT_MS` を使って接続完了を待ちます。この値は `ESP32 Wi-Fi STA` 側の Kconfig です。

English supplement: The manager no longer runs the display setup flow itself, but its stack should still have room for Wi-Fi orchestration and event handling.

## Dependencies

`wifi_manager` は以下のプロジェクトコンポーネントに依存します。

- `cyd_status_led`
- `cyd_wifi_setup`
- `esp32_wifi_sta`

`cyd_wifi_setup` は現在、設定済み credential 接続 helper と shell 上の `wifi setup app` として参照しています。全画面 setup UI の描画 owner は `app_shell` task です。

起動順としては、`main/app_main()` で `cyd_display_init()` と `cyd_input_init()` が完了した後に `wifi_manager_start()` を呼びます。現在の `main/main.c` はこの順序になっています。
