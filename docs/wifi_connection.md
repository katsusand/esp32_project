# Wi-Fi Connection

## Overview

`wifi_connection` は、保存済み Wi-Fi profile の探索、STA接続、再接続、接続寿命、setup UI の scan と接続テストを担当する UI 非依存の service です。STA を操作するのは、この service の manager task だけです。

利用可能な AP を scan し、保存済み profile と照合して、最終接続成功順と RSSI に基づいて接続候補を選びます。接続処理は `esp32_wifi_sta` に委譲し、成功した profile は `wifi_profile_store` に記録します。

English supplement: `wifi_connection` owns STA state and lifetime, saved-profile selection, connection attempts, and the setup UI's STA work. It must not depend on display, input, application shell, status LED, or radio arbitration policy.

## Responsibility

- 保存済み profile と scan result の照合
- 接続候補の優先順位付け
- STA の stop / reconfigure / start による接続試行
- active user に基づく接続寿命管理
- 切断監視と再接続
- setup UI の要求（scan、接続テストと保存）の実行
- 接続進捗と失敗理由の公開

次の責務は持ちません。

- Wi-Fi setup UI
- LED の色や点滅パターン
- Internet / ESP-NOW の無線利用調停

## Threading Model

STA を操作するのは `wifi_connection_start()` が起動する manager task だけです。

他の task からの要求（acquire、release、retry、setup 系）と、`esp32_wifi_sta` が通知する接続イベントは、1本の queue で manager task に届きます。manager task はそれを到着順に1つずつ処理します。状態を書き換えるのも manager task だけで、他の task は getter で読むだけです。

| 関数 | 主な呼び出し元 | 戻るタイミング |
| --- | --- | --- |
| `wifi_connection_acquire()` | `radio_manager` | user を数えた後。状態は `CONNECTING` / `RECONNECTING` / `CONNECTED` / setup 系のどれか |
| `wifi_connection_release()` | `radio_manager` | user を外した後。STA は試行が終わってから止まる |
| `wifi_connection_retry_connection_without_setup_async()` | clock app、settings | 試行を受け付けた後（`CONNECTING`）。結果は待たない |
| `wifi_connection_begin_setup()` / `wifi_connection_complete_setup()` | setup UI | 処理の完了後 |
| `wifi_connection_setup_scan()` | setup UI | scan の完了後 |
| `wifi_connection_connect_and_save()` | setup UI | 接続テストと保存の完了後 |

接続試行の途中でも、acquire / release / retry は試行の合間（接続待ち、再試行の待ち時間）に応答します。STA を使う要求（setup 系）が届いた場合は試行を中断し、その要求を次に実行します。中断の後にその要求が STA を使わなかった場合（setup 外での誤った呼び出しなど）は、試行をやり直します。

以前は setup UI が app_shell task から直接 scan と接続を行い、ESP-IDF のイベント task も切断時に自分で再接続していました。3つの task が同じ STA を操作するため、`setup_starting` や `connect_in_progress` といったフラグで互いを止めていましたが、漏れがあると接続処理が setup の scan の下で STA を再起動していました（レビュー #4）。現在は操作する task が1つなので、止め合う必要がありません。

callback（connectivity、connected）は manager task 上で呼ばれます。そこから要求関数を呼ぶと、自分自身を待つことになるため `ESP_ERR_INVALID_STATE` を返します。

English contract: only the manager task operates the STA. Requests and STA events share one queue and are handled one at a time in arrival order. Request functions return after the manager has handled them, so the state a caller reads afterwards already reflects the request. A running attempt answers acquire/release/retry between its steps and stops only for a request that needs the STA. Never call request functions from a wifi_connection callback.

## Public API

```c
#include "wifi_connection.h"

/* Wi-Fi が必要な間だけ user として登録する。 */
ESP_ERROR_CHECK(wifi_connection_acquire(WIFI_CONNECTION_USER_RADIO_MANAGER));
esp_err_t err = wifi_connection_wait_connected(pdMS_TO_TICKS(15000));
/* ... */
ESP_ERROR_CHECK(wifi_connection_release(WIFI_CONNECTION_USER_RADIO_MANAGER));
```

接続中の表示が必要な上位 app は `wifi_connection_get_progress()` を参照できます。下位 service から画面を直接更新しません。

setup UI 用の API は `wifi_connection_begin_setup()`、`wifi_connection_setup_scan()`、`wifi_connection_connect_and_save()`、`wifi_connection_complete_setup()` です。scan と接続テストは `SETUP_RUNNING` の間だけ受け付けます。

`wifi_connection_connect_and_save()` は、入力された credential で最大 `CONFIG_ESP32_WIFI_STA_MAX_RETRY + 1` 回の fresh 接続を試し、接続できた場合だけ保存します。接続に失敗した credential は保存しません。呼び出し元は結果が出るまで待ちます。

English supplement: Credential persistence is gated by a successful live connection test.

呼び出し元が無かった次の関数は、manager task の外で STA を操作するか、廃止した event bit に依存していたため削除しました: `wifi_connection_connect_configured()`、`wifi_connection_enable()`、`wifi_connection_disable()`、`wifi_connection_request_connection()`、`wifi_connection_request_connection_without_setup()`、`wifi_connection_request_connection_without_setup_async()`、同期版の `wifi_connection_retry_connection_without_setup()`。

## State And Lifetime

`wifi_connection_start()` は manager task を起動しますが、保存済み profile があれば通常は `OFF` で待機します。profile が無ければ `SETUP_REQUIRED` です。

`wifi_connection_acquire()` で active user が生じると接続を開始し、最後の user が `wifi_connection_release()` すると、試行中ならそれが終わってから STA を停止して `OFF` になります。

接続中は、`esp32_wifi_sta` からの切断イベントですぐに再接続を始めます。イベントが queue あふれで失われた場合に備えて、`CONFIG_WIFI_CONNECTION_MONITOR_INTERVAL_MS` ごとに STA の状態も確認します。

保存済み SSID が scan で見つからない場合は、`CONFIG_WIFI_CONNECTION_SCAN_RETRY_DELAY_MS` を空けて最大 `CONFIG_WIFI_CONNECTION_SCAN_RETRY_ATTEMPTS` 回まで scan し直します。

接続に失敗すると、エラーログ（SD）に1行記録します。自動接続は一連の試行がすべて失敗したとき、setup は接続テストが失敗したときで、試行1回ごとには書きません。行には理由を付けます（`auth failed` はたいていパスワード違い、`AP not found` は圏外など）。SSID は設置場所を推測できる情報なので書きません。

English supplement: one error log line per failed connection sequence (automatic connect, or the setup connection test), with the failure reason and without the SSID. Attempts are not logged one by one.

## Setup

setup UI は `wifi_connection_begin_setup()` で通常の接続処理を止めて `SETUP_RUNNING` にし、完了時に `wifi_connection_complete_setup()` を呼びます。接続テストが成功した後なら `CONNECTED`、キャンセルなら STA を止めて `OFF` です。

`wifi_connection_begin_setup()` は、接続試行が途中ならそれを中断させます。試行は次のステップ（接続待ち、再試行の待ち時間、scan の直後）で manager の queue に setup の要求を見つけて抜けるので、長くても scan 1回分（数秒）で setup に入れます。以前のように、別 task から試行の終了を最大 10 秒待つ処理はありません。

English supplement: Setup no longer takes the STA from another task. begin_setup is a request like any other; a running attempt yields to it at its next step, and the setup UI's scan and connection test then run on the manager task.
