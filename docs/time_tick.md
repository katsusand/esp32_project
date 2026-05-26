# Time Tick

## Overview

`time_tick` は、ローカル時刻の「秒変化」を購読者へ配信する軽量サービスです。

内部 task が現在時刻を短い周期で監視し、`time_t` の秒値が変わったタイミングで `time_tick_event_t` を publish します。各コンポーネントは `time_tick_subscribe()` で自分専用の queue を受け取り、`xQueueReceive()` で秒ごとの更新を受け取れます。

代表的な用途は以下です。

- 1 秒ごとに時計表示を再描画する
- 現在秒の偶奇で LED を点滅させる
- 現在時刻ベースの scheduler や UI 表示更新のトリガーにする

English supplement: `time_tick` is a fan-out service for second-level wall-clock changes. It is not an RTOS tick replacement and should not be used for precise sub-second timing.

## Public API

利用するファイルでは、次のヘッダーを include します。

```c
#include "time_tick.h"
```

公開 API は以下です。

- `time_tick_start()`: service task を起動する
- `time_tick_subscribe()`: 購読用 queue を1つ確保して返す
- `time_tick_unsubscribe()`: 購読解除し、内部で queue を破棄する
- `time_tick_get_latest()`: 最新 tick を即時参照する

イベント型は `time_tick_event_t` です。

```c
typedef struct {
    time_t epoch_sec;
    struct tm local_time;
    bool valid;
    bool jumped;
    int32_t delta_sec;
} time_tick_event_t;
```

- `epoch_sec`: Unix epoch 秒
- `local_time`: `localtime_r()` 済みのローカル時刻
- `valid`: 時刻が十分に有効とみなせるか
- `jumped`: 前回 tick から 1 秒以外の差分だったか
- `delta_sec`: 前回 tick との差分秒数

English supplement: `jumped=true` usually means time correction, delayed processing, or a startup gap. Consumers that care about continuity should inspect `delta_sec`.

## Startup

`time_tick_subscribe()` を呼ぶ前に、`time_tick_start()` が完了している必要があります。

このプロジェクトでは通常、`system_boot_start()` が起動時に一度だけ `time_tick_start()` を呼びます。そのため、通常の app や service は個別に start せず、subscribe だけ行えば十分です。

```c
ESP_ERROR_CHECK(time_tick_start());
```

English supplement: `time_tick_start()` is idempotent. Repeated calls return `ESP_OK`.

## Subscribe Pattern

秒変化を受け取りたいコンポーネントは、初期化または `enter()` のような開始処理で queue を購読します。

```c
static QueueHandle_t s_tick_queue;

esp_err_t my_component_init(void)
{
    return time_tick_subscribe(&s_tick_queue);
}
```

その後、task または step 処理で queue からイベントを読みます。

```c
time_tick_event_t tick = { 0 };

if (xQueueReceive(s_tick_queue, &tick, portMAX_DELAY) == pdTRUE) {
    if (tick.valid) {
        /* 秒が変わったので処理する */
    }
}
```

`wait_ticks=0` で non-blocking に読むこともできます。画面 app のように定期 step の中で「更新が来ていれば redraw する」用途ではこの形が向いています。

```c
while (xQueueReceive(s_tick_queue, &tick, 0) == pdTRUE) {
    redraw = true;
}
```

既存例として、`cyd_clock_app` は `enter()` で購読し、step 中に non-blocking で tick を吸って redraw 要否を判断します。

English supplement: Each subscriber owns its queue handle. Do not share one queue between unrelated components unless you intentionally want a single consumer.

## Queue Behavior

`time_tick` の購読 queue は内部で長さ `1` で作られます。publish 時は `xQueueOverwrite()` を使うため、保持されるのは常に最新の 1 件だけです。

このため、以下の性質があります。

- 過去の秒イベントをすべて蓄積する用途には向かない
- 一時的に consumer が遅れても、次に読むと最新時刻へ追従できる
- 「毎秒最新状態で十分」な UI や状態更新トリガーに向いている

subscribe 時点ですでに最新イベントが存在する場合、その時点の最新 1 件が queue に即時投入されます。

English supplement: This is intentionally lossy. The contract is latest-state delivery, not exact delivery count.

## Validity and Time Sync

`valid` は、ローカル時刻の年が一定値以上かどうかで判定されます。現在実装では 2024 年以上なら有効扱いです。

そのため、起動直後で NTP 未同期の間は `valid=false` になりえます。時刻表示や時刻依存制御が「未同期では動いてほしくない」場合は、consumer 側で `tick.valid` を確認してください。

```c
if (tick.valid) {
    /* 同期済み時刻として扱う処理 */
}
```

English supplement: `valid` is a coarse runtime guard, not a security or timezone guarantee.

## Unsubscribe

コンポーネントを終了するとき、または今後 tick を受け取らないときは `time_tick_unsubscribe()` を呼びます。

```c
if (s_tick_queue != NULL) {
    ESP_ERROR_CHECK(time_tick_unsubscribe(s_tick_queue));
    s_tick_queue = NULL;
}
```

`time_tick_unsubscribe()` は購読解除だけでなく queue の破棄まで行います。解除後の handle を再利用してはいけません。

## Latest Snapshot

queue を待たずに現時点の最新値だけ見たい場合は `time_tick_get_latest()` を使えます。

```c
time_tick_event_t tick = { 0 };
if (time_tick_get_latest(&tick) && tick.valid) {
    /* 最新時刻を参照できる */
}
```

まだ一度も tick が publish されていない場合は `false` を返します。

## Dependencies

自分の component から `time_tick` を使う場合は、`CMakeLists.txt` の `REQUIRES` または `PRIV_REQUIRES` に `time_tick` を追加します。

```cmake
idf_component_register(
    SRCS "my_component.c"
    INCLUDE_DIRS "include"
    REQUIRES time_tick
)
```

## Current Implementation Notes

現在の実装上の制約と前提は以下です。

- 最大購読数は `8`
- publish 判定は約 `50 ms` ごとの polling
- 秒値が 1 秒以外飛んだ場合は `jumped=true` になる

高精度周期制御や PWM のような用途には向きません。そのような用途では FreeRTOS timer、`esp_timer`、GPIO/LEDC などの専用機構を使ってください。

English supplement: `time_tick` is for user-facing wall-clock cadence and coarse state transitions, not for hard real-time output timing.
