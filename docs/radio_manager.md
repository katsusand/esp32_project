# Radio Manager

## Overview

`radio_manager` は、無線利用権の調停を行う軽量 service です。

現時点の実装では、`RADIO_MANAGER_CAP_INTERNET` を要求する client に対して、内部で `wifi_connection` を使って Wi-Fi STA 接続を準備し、接続可能になった時点で lease を許可します。通信 payload や transport 実装は持ちません。

English supplement: `radio_manager` is a lease arbiter, not a transport implementation. It owns access sequencing and hold timing, not HTTP, MQTT, or ESP-NOW payload flow.

この層は、将来 Wi-Fi 以外の radio 利用形態も扱えるようにするための小さな抽象化として導入しています。ただし、このプロジェクトでは現在 ESP-NOW は未使用であり、実装済み capability も `RADIO_MANAGER_CAP_INTERNET` のみです。

English supplement: Think of `radio_manager` as a small future-oriented boundary. The abstraction exists now, but only the internet-over-Wi-Fi path is implemented today.

## Current Scope

現在 `radio_manager` が実際に行っていることは以下です。

- client から lease request を queue で受ける
- 同時 owner を 1 つに制限する
- `wifi_connection` を使って Wi-Fi 接続を準備する
- 接続できたら lease token を発行する
- `release` または `max_hold_ticks` timeout まで owner を維持する
- queue が空で idle timeout が続いたら `wifi_connection` の利用権を release する

radio準備に失敗した要求は要求元へ即時reject通知します。要求元をtimeoutまで待たせないことで、setup完了後の新しい通信要求と古い失敗要求が重なるのを防ぎます。

English supplement: Preparation failure completes the pending acquire immediately with rejection; it is not left waiting for its caller-side timeout.

現在は Internet 利用の調停だけが責務です。ESP-NOW channel 管理、peer 管理、送受信 queue、payload 保持などは未実装です。

## Why It Exists

この層を入れた理由は、`wifi_connection` をそのまま各 app / service の公開入口にせず、「無線を一定時間借りる」という責務でまとめるためです。

現在は Wi-Fi STA 接続しか扱いませんが、設計意図としては以下のような拡張余地を残しています。

- Wi-Fi を使う HTTP / MQTT / NTP 系 service を同じ lease 方式で扱う
- 将来必要になれば、ESP-NOW のような別利用形態を追加する
- 無線の同時利用順、最大保持時間、idle release を一か所で管理する

ただし、ESP-NOW を近いうちに使う予定があるという意味ではありません。このプロジェクトでは、将来性のための最小限の工事として `radio_manager` を入れており、現状は保留中の拡張点です。

English supplement: The abstraction is intentional, but the future paths are not committed features yet.

## Public API

利用するファイルでは、次のヘッダーを include します。

```c
#include "radio_manager.h"
```

起動は `radio_manager_start()` で行います。

```c
ESP_ERROR_CHECK(radio_manager_start());
```

利用者は `radio_manager_request_t` を組み立てて `radio_manager_acquire()` を呼びます。

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

`required` は現在 `RADIO_MANAGER_CAP_INTERNET` のみ対応です。未対応 capability を要求した場合は `ESP_ERR_NOT_SUPPORTED` を返します。

Wi-Fiを保持するidle timeoutは秒単位の実行時APIで扱います。

```c
uint16_t seconds = radio_manager_get_idle_timeout_seconds();
ESP_ERROR_CHECK(radio_manager_set_idle_timeout_seconds(60));
ESP_ERROR_CHECK(radio_manager_save_idle_timeout_seconds());
```

初回値は `CONFIG_RADIO_MANAGER_IDLE_TIMEOUT_MS` を秒へ換算した値です。NVS保存値があればそちらを優先します。`0` はidleによるWi-Fi解放を無効化します。

English supplement: Runtime values use seconds; the existing Kconfig value remains in milliseconds for backward compatibility.

## Current Flow

現在の `time_sync` は以下の流れで `radio_manager` を使います。

```text
time_sync
  -> radio_manager_acquire(RADIO_MANAGER_CAP_INTERNET)
  -> SNTP sync
  -> radio_manager_release()

radio_manager
  -> wifi_connection_acquire(WIFI_CONNECTION_USER_RADIO_MANAGER)
  -> wait until Wi-Fi STA is connected
  -> grant lease
  -> hold owner until release/timeout
  -> idle timeout later releases Wi-Fi
```

`radio_manager` 自身は transport-specific な再送や payload queue を持ちません。そうした処理は `time_sync` や将来の uplink service 側に置く前提です。

## Relationship With Wi-Fi Connection

`wifi_connection` は Wi-Fi STA の接続状態と接続ポリシーを管理します。`radio_manager` はその上で「誰がいつ無線を使うか」を調停します。

このため、新しい通信 service を追加する場合は、通常は `wifi_connection` を直接使わず `radio_manager` を経由する想定です。`wifi_connection` 直呼びは低レベル統合や切り分け用途に寄せます。

English supplement: `wifi_connection` owns Wi-Fi state. `radio_manager` owns client-facing access sequencing.

## Future Direction

将来、別の radio 利用形態を追加する場合でも、`radio_manager` は payload の共通キューにはしない方針です。

たとえば:

- `net_uplink`
  HTTP / MQTT 用の payload queue と retry を内部で持つ
- `esp_now_transport`
  peer、tx queue、retry を内部で持つ
- `radio_manager`
  利用順、lease、保持時間、idle release を管理する

つまり、共有するのは「無線を借りる仕組み」であって、メッセージの形や retry policy ではありません。

## Notes

- 現在の public client id は `RADIO_MANAGER_CLIENT_TIME_SYNC` のみです
- 現在の public capability は `RADIO_MANAGER_CAP_INTERNET` のみです
- ESP-NOW 関連の runtime setting や UI は、このプロジェクトでは削除済みです
- 将来拡張を見据えた層ではありますが、当面は Wi-Fi / Internet lease 専用 service として扱って問題ありません

English supplement: For current users, it is accurate to think of `radio_manager` as the internet radio lease service, while keeping its broader name as a reserved abstraction boundary.
