# ESP32 Wi-Fi STA

## Overview

`esp32_wifi_sta` は、センサー値の送信や時刻同期の土台として使うための Wi-Fi station component です。

主な役割は次の通りです。

- ESP-IDF の `esp_wifi` / `esp_netif` / `esp_event` を初期化する
- Kconfig または明示的な `esp32_wifi_sta_config_t` から STA 接続を開始する
- FreeRTOS `EventGroup` を使って接続完了または失敗を待てるようにする
- 接続状態とIPアドレス情報をアプリケーション側から取得できるようにする
- SSID未設定または接続失敗時に、アプリケーション側からWi-Fi scan modeへ入れるようにする

English supplement: This component owns the ESP-IDF Wi-Fi STA driver setup for the project. Higher-level UI/provisioning logic belongs to `cyd_wifi_setup` and `wifi_connection`.

## Ownership

STA を操作する関数（`esp32_wifi_sta_init()`、`esp32_wifi_sta_init_with_config()`、`esp32_wifi_sta_start()`、`esp32_wifi_sta_stop()`、`esp32_wifi_sta_enter_scan_mode()`）は、1つの task からしか呼べません。最初に呼んだ task が所有者になり、それ以外の task からの呼び出しは `ESP_ERR_INVALID_STATE` で拒否してログに残します。このプロジェクトでは所有者は `wifi_connection` の manager task です。

ESP-IDF のイベント handler も STA を操作しません。以前は `WIFI_EVENT_STA_START` と切断時に handler 自身が `esp_wifi_connect()` を呼んでいたため、イベント task が別の task の停止と競合していました。現在の handler は状態を記録して、`esp32_wifi_sta_set_event_callback()` で登録された callback に `CONNECTED` / `DISCONNECTED` を通知するだけです。再接続するかどうかは所有者が決めます。

getter（`esp32_wifi_sta_get_status()`、`esp32_wifi_sta_get_scan_records()` など）はどの task からでも呼べます。

English contract: operating functions must come from a single owner task, enforced at runtime. The event handler never operates the driver; it records state and reports through the event callback, which runs on the ESP-IDF event task and must not block or call back into this component.

## Public API

利用するファイルでは、次のヘッダーを include します。

```c
#include "esp32_wifi_sta.h"
```

Kconfig と NVS の保存済み credential から初期化する場合は `esp32_wifi_sta_init()` を使います。

```c
esp_err_t err = esp32_wifi_sta_init();
if (err == ESP_OK) {
    ESP_ERROR_CHECK(esp32_wifi_sta_start());
}
```

明示的な設定で初期化する場合は `esp32_wifi_sta_init_with_config()` を使います。

```c
esp32_wifi_sta_config_t config = {
    .ssid = "ssid",
    .password = "password",
    .authmode_threshold = WIFI_AUTH_WPA2_PSK,
    .sae_pwe_h2e = WPA3_SAE_PWE_BOTH,
    .sae_h2e_identifier = "",
};

ESP_ERROR_CHECK(esp32_wifi_sta_init_with_config(&config));
ESP_ERROR_CHECK(esp32_wifi_sta_start());
```

`esp32_wifi_sta_start()` は、必要なら driver を開始してから、呼び出した task 上で `esp_wifi_connect()` を1回だけ呼びます。自動の再試行はしません。

接続完了待ちは `esp32_wifi_sta_wait_connected()` で行います。戻り値は、接続成功なら `ESP_OK`、その試行が失敗したら `ESP_FAIL`、待ち時間切れなら `ESP_ERR_TIMEOUT` です。`wifi_connection` はこの関数ではなく、event callback 経由の通知で待ちます。

状態取得には `esp32_wifi_sta_get_status()` または `esp32_wifi_sta_is_connected()` を使います。

```c
esp32_wifi_sta_status_t status = { 0 };
ESP_ERROR_CHECK(esp32_wifi_sta_get_status(&status));
```

現在設定されている SSID を表示用に取得する場合は `esp32_wifi_sta_get_configured_ssid()` を使います。

```c
char ssid[33] = { 0 };
ESP_ERROR_CHECK(esp32_wifi_sta_get_configured_ssid(ssid, sizeof(ssid)));
```

default configuration に SSID が入っているかだけを確認する場合は `esp32_wifi_sta_has_configured_ssid()` を使います。この helper は Wi-Fi radio を開始しません。`CONFIG_ESP32_WIFI_STA_SSID` が空の場合は、NVS の保存済み credential を確認します。

English supplement: This helper checks persisted/default credentials without starting Wi-Fi. It does not treat an unsaved runtime setup attempt as configured credentials.

scan mode は `esp32_wifi_sta_enter_scan_mode()` で開始し、直近のスキャン結果は `esp32_wifi_sta_get_scan_records()` で取得します。

```c
ESP_ERROR_CHECK(esp32_wifi_sta_enter_scan_mode());

esp32_wifi_sta_scan_record_t records[8];
size_t record_count = 0;
ESP_ERROR_CHECK(esp32_wifi_sta_get_scan_records(records, 8, &record_count));
```

`esp32_wifi_sta_get_scan_records()` は、`record_count` に保持中の総件数を返します。`records` へコピーされる件数は `record_capacity` までです。

Credential の接続テストと保存は上位の `wifi_connection_connect_and_save()` を使います。保存済み credential は、`CONFIG_ESP32_WIFI_STA_SSID` が空のときだけ default configuration として読み込まれます。

Wi-Fi を停止する場合は `esp32_wifi_sta_stop()` を使います。停止時は `started=false`、IP 情報なし、状態 `STOPPED` に戻り、connected/fail bit をクリアします。自分で止めたことによる切断は失敗として通知しません。

English supplement: Saved credentials are fallback defaults, not a replacement for explicit runtime configuration passed to `esp32_wifi_sta_init_with_config()`.

## Configuration

`idf.py menuconfig` で `ESP32 Wi-Fi STA` を開き、必要に応じて以下を設定します。

- `ESP32_WIFI_STA_ENABLED`
- `ESP32_WIFI_STA_AUTO_START`: clock compositionから`wifi_connection`と`time_sync`を起動する
- `ESP32_WIFI_STA_SSID`
- `ESP32_WIFI_STA_PASSWORD`
- `ESP32_WIFI_STA_MAX_RETRY`
- `ESP32_WIFI_STA_RETRY_DELAY_MS`
- `ESP32_WIFI_STA_WAIT_ON_BOOT`: 互換用の legacy option。現在の起動経路は Wi-Fi 接続待ちで boot をブロックしない
- `ESP32_WIFI_STA_SCAN_LIST_SIZE`
- `ESP32_WIFI_STA_CONNECT_TIMEOUT_MS`
- `ESP32_WIFI_STA_WPA3_SAE_PWE_HUNT_AND_PECK`
- `ESP32_WIFI_STA_WPA3_SAE_PWE_HASH_TO_ELEMENT`
- `ESP32_WIFI_STA_WPA3_SAE_PWE_BOTH`
- `ESP32_WIFI_STA_SAE_H2E_IDENTIFIER`
- `ESP32_WIFI_STA_AUTH_OPEN`
- `ESP32_WIFI_STA_AUTH_WEP`
- `ESP32_WIFI_STA_AUTH_WPA_PSK`
- `ESP32_WIFI_STA_AUTH_WPA2_PSK`
- `ESP32_WIFI_STA_AUTH_WPA_WPA2_PSK`
- `ESP32_WIFI_STA_AUTH_WPA3_PSK`
- `ESP32_WIFI_STA_AUTH_WPA2_WPA3_PSK`
- `ESP32_WIFI_STA_AUTH_WAPI_PSK`

SSID / password はリポジトリにコミットしない運用を推奨します。
共有したい初期値がある場合は、実値ではなく `sdkconfig.defaults.example` のようなサンプルファイルに記載してください。

English intent: credentials are configuration/runtime inputs, not application source constants.

`ESP32_WIFI_STA_MAX_RETRY` は、`wifi_connection` が初回試行の後に行う fresh retry の回数です。この component 自身は再試行しません。各 retry は STA を停止して設定を再適用し、`ESP32_WIFI_STA_RETRY_DELAY_MS` 待ってから開始します。

English supplement: Retries rebuild operational STA state instead of repeatedly calling `esp_wifi_connect()` from one failed state, and they are the owner's decision, not the driver's.

## Build Switch

Wi-Fi STA を使わない派生ビルドでは、CMake 再構成時に `APP_WIFI_STA=0` を指定します。

```bash
source ~/.espressif/tools/activate_idf_v5.4.3.sh || true
APP_WIFI_STA=0 python "$IDF_PATH/tools/idf.py" reconfigure
APP_WIFI_STA=0 DEV=1 ninja -C build
```

この切り替えは component の source/dependency selection に使います。`APP_WIFI_STA=0` では `esp32_wifi_sta` は stub 実装になり、`esp_wifi` / `esp_netif` / `esp_event` を初期化しません。

通常の Wi-Fi 有効ビルドへ戻す場合は、`APP_WIFI_STA` を未指定にして再構成します。

English supplement: Use `APP_WIFI_STA` for binary composition. Use Kconfig for Wi-Fi behavior when the Wi-Fi feature is included.

source/dependency selection に Kconfig を使わない理由は [Build Feature Switches](build_feature_switches.md) に記録しています。

## Scan Mode

SSIDが未設定の場合、`esp32_wifi_sta_init()` は `ESP_ERR_NOT_FOUND` を返します。
また、接続リトライに失敗した場合、`esp32_wifi_sta_wait_connected()` は `ESP_FAIL` を返します。
上位コンポーネントはこれらを見て `esp32_wifi_sta_enter_scan_mode()` を呼び、近くのAPをスキャンできます。

このプロジェクトでは、対話的な scan/password UI は `cyd_wifi_setup` の `wifi setup app` が担当します。`wifi_connection` は SSID 未設定や起動時 setup shortcut を `SETUP_REQUIRED` として表し、`clock app` や `settings app` が必要に応じて `wifi setup app` へ切り替えます。setup UI はこの component を直接呼ばず、`wifi_connection_setup_scan()` で manager task に scan を依頼します。

scan mode画面は、画面下部の「再検索」ボタンを押した時だけスキャン結果を更新します。
SSID行をタッチするとパスワード入力画面へ進みます。
パスワード入力画面の見出しの帯には「戻る」と「保存」を表示します。
「戻る」はscan mode画面へ戻ります。
「保存」は接続テストを行い、成功した場合だけSSID/passwordをNVSに保存します。
失敗した場合は「OK」ボタンだけを持つ失敗ダイアログ (失敗理由つき) を表示し、「OK」後にscan mode画面へ戻ります。

scan mode中にRAMへ保持される情報は以下です。

- SSID
- RSSI
- channel
- authmode

scan modeでSSIDを見つけただけではNVSには保存しません。
ユーザーがSSIDを選び、パスワードを入力し、接続テストに成功したあとにcredentialを保存します。
SSIDはAPが発信している公開識別子ですが、設置環境を推測できる情報でもあるため、ログの扱いには注意してください。

English intent: scan mode is an interactive provisioning step. Discovery results are temporary RAM state, and credentials must only be persisted after a successful user-selected connection.

保存したcredentialは、次回起動時に `ESP32_WIFI_STA_SSID` が空の場合のdefault configurationとして読み込まれます。

最後にスキャンした結果は `esp32_wifi_sta_get_scan_records()` で読み出せます。

## State Model

状態は `esp32_wifi_sta_state_t` で表します。

```c
typedef enum {
    ESP32_WIFI_STA_STATE_STOPPED = 0,
    ESP32_WIFI_STA_STATE_CONNECTING,
    ESP32_WIFI_STA_STATE_CONNECTED,
    ESP32_WIFI_STA_STATE_FAILED,
    ESP32_WIFI_STA_STATE_SCAN_MODE,
} esp32_wifi_sta_state_t;
```

接続を試している間（`esp32_wifi_sta_start()` の後）に `WIFI_EVENT_STA_DISCONNECTED` を受けると、再接続はせずに `FAILED` にし、fail bit を立てて `DISCONNECTED` を通知します。確立済みの接続が切れた場合も同じです。

`IP_EVENT_STA_GOT_IP` を受けると、`has_ip=true` と IP 情報を保存し、状態を `CONNECTED` にして `CONNECTED` を通知します。所有者がすでに止めた試行の分は無視します。

English supplement: `CONNECTED` means the station got an IP address, not merely that 802.11 association completed.

## NVS Storage

Credential は `wifi_connection` を通じて以下の場所へ保存されます。

- NVS namespace: `esp32_wifi_sta`
- SSID key: `ssid`
- password key: `password`

SSID は最大 32 文字、password は最大 64 文字です。password に `NULL` を渡した場合は空文字として保存します。

## API Usage

`esp32_wifi_sta` を直接使う場合は、NVS 初期化後にアプリケーションから次の順で呼びます。

```c
ESP_ERROR_CHECK(esp32_wifi_sta_init());
ESP_ERROR_CHECK(esp32_wifi_sta_start());

esp_err_t wait_ret = esp32_wifi_sta_wait_connected(pdMS_TO_TICKS(15000));
if (wait_ret == ESP_OK) {
    /* HTTP送信やSNTP同期を開始できる */
} else {
    ESP_ERROR_CHECK(esp32_wifi_sta_enter_scan_mode());
}
```

`esp32_wifi_sta_wait_connected()` は `ESP_OK`、`ESP_FAIL`、`ESP_ERR_TIMEOUT` を返します。
接続が必須でない処理では、タイムアウトしてもアプリケーション全体を停止しない設計にしてください。

このプロジェクトでは、アプリケーションは `esp32_wifi_sta` を直接呼びません。`wifi_connection_acquire()` / `wifi_connection_release()` を通じて Wi-Fi 利用期間を表し、STA を操作するのは `wifi_connection` の manager task だけです（[Ownership](#ownership)）。`esp32_wifi_sta` は低レベル STA wrapper、`wifi_connection` は接続寿命の orchestration、`cyd_wifi_setup` はユーザー操作 UI という分担です。

English intent: Wi-Fi availability is a runtime condition. Local UI tasks should usually remain alive even when network setup fails.
