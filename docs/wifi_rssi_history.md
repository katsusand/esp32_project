# Wi-Fi RSSI History

## Overview

`wifi_rssi_history` は、接続中の AP の RSSI を一定間隔でサンプリングし、直近 N 件を保持する service です。

`components/services/wifi_rssi_history/` に置いています。

このコンポーネントは、`cyd_display` の sparkline widget を実データで検証するための最初の利用例でもあります。**app が「値を集める service」と「それを描く view」に分かれる形の参照実装**として読んでください。

English supplement: this is the reference implementation of the sampling-service pattern. A service owns the task and the buffer; the foreground app is a thin view over it.

## Public API

```c
#include "wifi_rssi_history.h"

esp_err_t wifi_rssi_history_start(void);
bool wifi_rssi_history_get(const int16_t **samples, uint16_t *count, uint16_t *revision);
bool wifi_rssi_history_get_latest(int16_t *rssi_dbm);
```

`wifi_rssi_history_get()` の出力は、そのまま `cyd_display_sparkline_t` に渡せる形になっています。

```c
const int16_t *samples = NULL;
uint16_t count = 0;
uint16_t revision = 0;

if (wifi_rssi_history_get(&samples, &count, &revision)) {
    cyd_display_sparkline_t graph = {
        .samples = samples,
        .count = count,
        .revision = revision,
        .min_value = -100,
        .max_value = -30,
    };
    cyd_ui_add_sparkline(screen, 2, 9, 36, 13, &graph,
                         CYD_UI_COLOR_GREEN, CYD_UI_COLOR_BLACK, CYD_UI_COLOR_DARKGREY);
}
```

出力ポインタはいずれも NULL を渡せます。再描画が必要かどうかだけ知りたい場合は `revision` だけ取得してください。

## Sampling Model

サンプリングは専用 task で行います。

- 間隔: `CONFIG_WIFI_RSSI_HISTORY_SAMPLE_INTERVAL_MS`
- 保持数: `CONFIG_WIFI_RSSI_HISTORY_MAX_SAMPLES`
- 取得元: `esp_wifi_sta_get_ap_info()` の `rssi`

**task を持つのは必須です。** `radio_manager_acquire()` は呼び出しタスクをブロックするため、これを `app_shell` の `step()` 経路から呼ぶと、Wi-Fi 起動が終わるまで描画も入力処理も止まります（実測で約2.8秒 UI が固まりました）。ブロックする処理はこの service 自身の task に閉じ込め、公開 API は非ブロッキングにしています。

English contract: the blocking radio acquire must never run on the app_shell task. That is the entire reason this service owns a task.

**未接続時は欠測値 `WIFI_RSSI_HISTORY_GAP_DBM` を記録します。**

```c
#define WIFI_RSSI_HISTORY_GAP_DBM INT16_MIN
```

これを sparkline の `gap_value` に渡すと、その区間は線が途切れて描かれます。サンプルを単に飛ばすと欠測が時間軸から消えて詰まってしまい、「電波が途切れていた」のか「その間ずっと安定していた」のか区別できなくなるためです。

`wifi_rssi_history_get_latest()` は末尾の欠測を読み飛ばし、最後の実測値を返します。

## Radio Lease

`radio_manager` は、どの client も lease を保持しなくなると `CONFIG_RADIO_MANAGER_IDLE_TIMEOUT_MS` 後に radio を落とします。これは省電力設計として正しい挙動ですが、**そのままだと RSSI サンプリングも一緒に止まります。**

実際、初期実装では time_sync が lease を返した約30秒後に radio が落ち、グラフが 33 サンプル前後で停止していました。

```text
I (38257) wifi:state: run -> init (0x0)
I (38357) wifi_connection: Wi-Fi connection disabled
```

そのため、**グラフを表示している間だけ** lease を保持します。

```c
esp_err_t wifi_rssi_history_set_monitoring(bool monitoring);
```

- **非ブロッキングかつ冪等**です。`step()` から毎回呼んでも UI は止まりません。実際の lease 取得は service の task 側で行われます
- 画面に出ている間だけ有効にしてください。常時保持すると `radio_manager` の意味が無くなります
- app の `leave()` と対にしてください。`system_info_app` は idle 復帰経路も `leave()` を通るため、lease が view より長生きしません
- 解放漏れの保険として `CONFIG_WIFI_RSSI_HISTORY_RADIO_MAX_HOLD_MS` で上限を掛けています

English contract: the lease scope is the view, not the service lifetime. Enabling it permanently defeats radio_manager.

この設計上、ページを離れている間はサンプルが増えません。再訪時はそれまでの履歴の続きから描かれるため、時間軸に不連続が生じます。診断ビューとしては許容範囲と判断しています。

lease 取得中（radio 起動待ち）は接続前なので、その間のサンプルは欠測値になります。結果として、ページを開いた直後は左端から線が始まらず、Wi-Fi が上がった時点から描画が始まります。これは意図した表示です。

サンプルは ring buffer ではなく**線形配列で、新しいものが末尾**です。1 サンプルごとに数百バイトの `memmove` が発生しますが、これは「読み手全員に ring の順序を教える」よりも安いという判断です。sparkline widget にそのまま渡せる形を優先しています。

## Concurrency

sampler は esp_timer task から書き、読み手は `app_shell` task（および描画時は `cyd_display` task）です。

`count` と `revision` の整合性だけ critical section で保護し、**サンプル配列そのものは描画中にロックしません**。書き込みと描画が競合した場合の結果は「一部だけ新しい値のフレームが 1 回出る」だけで、トレンドグラフでは実害が無いためです。

English contract: this is the same trade-off documented for `cyd_display_sparkline_t`. A torn frame is acceptable; a lock held across rendering is not.

## Revision

`revision` はサンプル内容が変わるたびに加算されます。

これは必須の仕組みです。`cyd_display` の dirty-rect 差分は widget を値で比較するため、ポインタの先までは見ません。`revision` を渡さないと、グラフは submit されても**永久に再描画されません**。

同じ値は「再描画が必要か」の判定にも使えます。`system_info_app` の RSSI ページは、前回描画した revision と比較して変化があったときだけ再描画しています。

## Build Feature Switch

`APP_WIFI_STA=0` ビルドでは stub 実装に切り替わります。

- `wifi_rssi_history_start()` は `ESP_ERR_NOT_SUPPORTED` を返す
- `wifi_rssi_history_get()` / `_get_latest()` は `false` を返す

app 側に `#if` を書かずに済むよう、API は維持されます。RSSI ページは「データなし」表示になります。

## Configuration

`idf.py menuconfig` の `Wi-Fi RSSI History` から変更できます。

- `CONFIG_WIFI_RSSI_HISTORY_TASK_STACK_SIZE` (既定 3072)
- `CONFIG_WIFI_RSSI_HISTORY_TASK_PRIORITY` (既定 4)
- `CONFIG_WIFI_RSSI_HISTORY_SAMPLE_INTERVAL_MS` (既定 1000)
- `CONFIG_WIFI_RSSI_HISTORY_MAX_SAMPLES` (既定 120)

消費 RAM は `MAX_SAMPLES x 2` バイトです。グラフの横幅はディスプレイ上 320 px なので、320 を超える値を指定しても表示の情報量は増えません。

## Notes

`cyd_clock_composition` は、他の Wi-Fi service と同じく optional service として起動します。起動に失敗しても degraded mode で継続し、ブートループにはなりません。
