# Wi-Fi Connection

## Overview

`wifi_connection` は、保存済み Wi-Fi profile の探索、STA接続、再接続、接続寿命、setupへの所有権移譲を担当する UI 非依存の service です。

利用可能な AP を scan し、保存済み profile と照合して、最終接続成功順と RSSI に基づいて接続候補を選びます。接続処理は `esp32_wifi_sta` に委譲し、成功した profile は `wifi_profile_store` に記録します。

English supplement: `wifi_connection` owns STA state and lifetime, saved-profile selection, connection attempts, and setup handoff. It must not depend on display, input, application shell, status LED, or radio arbitration policy.

## Responsibility

- 保存済み profile と scan result の照合
- 接続候補の優先順位付け
- STA の stop / reconfigure / start による接続試行
- active user に基づく接続寿命管理
- 切断監視と再接続
- setup UIへの排他的なSTA所有権移譲
- 接続進捗と失敗理由の公開

次の責務は持ちません。

- Wi-Fi setup UI
- LED の色や点滅パターン
- Internet / ESP-NOW の無線利用調停

## Public API

```c
#include "wifi_connection.h"

esp32_wifi_sta_failure_reason_t reason = ESP32_WIFI_STA_FAILURE_NONE;
esp_err_t err = wifi_connection_connect_configured(
    pdMS_TO_TICKS(15000),
    &reason
);
```

接続中の表示が必要な上位 app は `wifi_connection_get_progress()` を参照できます。下位 service から画面を直接更新しません。

setup UI で入力されたcredentialは、接続テストと保存を一体化した `wifi_connection_connect_and_save()` に渡します。接続に失敗したcredentialは保存しません。

English supplement: Credential persistence is gated by a successful live connection test.

## State And Lifetime

`wifi_connection_start()` は管理taskを起動しますが、通常は `OFF` で待機します。`wifi_connection_acquire()` でactive userが生じると接続を開始し、最後のuserが `wifi_connection_release()` した時点でSTAを停止します。

setup UIは `wifi_connection_begin_setup()` で通常接続を停止してSTA所有権を取得し、完了時に `wifi_connection_complete_setup()` を呼びます。

English supplement: `wifi_connection` is the single owner of normal STA lifetime. Setup receives an explicit exclusive handoff rather than controlling STA concurrently.
