# App Scheduler

## Overview

`app_scheduler` は、アプリ共通で使う時刻ベースのスケジューラーサービスです。

時計アプリ専用のアラーム処理を一般化し、時計以外のアプリからも「指定時刻に1回だけ実行する」「指定時間帯だけ動作させる」といった処理を扱えるようにします。

代表的な用途は以下です。

- 08:00 に時計アラームを鳴らす
- 12:00 に時報を1回だけ鳴らす
- 月火水の 09:00 から 10:00 までリレーやポンプを ON にする
- Info アプリで現在登録されているスケジュールを確認する

English supplement: The scheduler is a system service. Applications should identify schedules by `owner/tag` and should not depend on physical slot numbers for ownership or persistence.

## Concepts

スケジュールは最大 `APP_SCHEDULER_MAX_ENTRIES` 件、現在は `5` 件まで登録できます。

各スケジュールは `owner` と `tag` で識別します。

- `owner`: アプリまたは機能の所有者名。例: `"clock"`, `"pump"`
- `tag`: owner 内での用途名。例: `"alarm1"`, `"alarm2"`, `"pumpUp"`

`slot_id` は内部の格納位置です。Info 表示や診断には使えますが、アプリの管理キーとしては使いません。

English supplement: `owner/tag` is the stable application contract. `slot_id` is diagnostic metadata and may change when entries are removed and recreated.

## Modes

`app_scheduler` には2つの mode があります。

| Mode | Event | Purpose |
| --- | --- | --- |
| `APP_SCHEDULER_MODE_INSTANT` | `FIRED` | 指定時刻に1回発火する |
| `APP_SCHEDULER_MODE_WINDOW` | `STARTED`, `ENDED` | 指定時間帯の開始と終了を通知する |

`INSTANT` はアラームや時報のようなワンショット通知に使います。

`WINDOW` は「09:00 から 10:00 まで動かす」のような継続動作に使います。現在の実装では日をまたぐ window、例えば `23:00` から `01:00` は未対応です。

English supplement: Overnight windows are rejected by validation. For now, split such behavior into two same-day windows if it becomes necessary.

## Behavior

`mode` は「時刻の形」を表し、`behavior` は「発火後の扱い」を表します。

| Behavior | Meaning |
| --- | --- |
| `APP_SCHEDULER_BEHAVIOR_EVENT` | イベントを出してすぐ待機または無効へ戻る |
| `APP_SCHEDULER_BEHAVIOR_LATCHED` | `app_scheduler_stop()` されるまで `ACTIVE` のまま保持する |

通常の時報や従来のアラームは `EVENT` を使います。

目覚まし時計のように、ユーザーが止めるまで鳴動中として扱いたい場合は `INSTANT` + `LATCHED` を使います。指定時刻に `WAITING` から `ACTIVE` へ遷移し、`STARTED` イベントを出します。その後、アプリが `app_scheduler_stop()` を呼ぶまで `ACTIVE` のままです。

現在 `LATCHED` は `INSTANT` 用です。`WINDOW` は開始/終了を時間帯で扱うため、`APP_SCHEDULER_BEHAVIOR_EVENT` のみ対応します。

English supplement: `LATCHED` belongs to scheduler state, not output policy. Alarm mute, snooze, and sound repetition should stay in the alarm application.

## Repeat and Weekdays

`repeat` は、発火後も次回以降に残すかどうかを表します。

- `repeat = true`: 繰り返し。`weekday_mask` に一致する日に発火する
- `repeat = false`: 一度だけ。発火または終了後に自動で `enabled = false` になる

`weekday_mask` は `repeat = true` のときに使います。`0` を指定した場合は `APP_SCHEDULER_WEEKDAY_ALL` と同じ扱いになります。

曜日 bit は `struct tm.tm_wday` と同じで、日曜が bit 0 です。

```c
uint8_t weekdays =
    APP_SCHEDULER_WEEKDAY_MONDAY |
    APP_SCHEDULER_WEEKDAY_TUESDAY |
    APP_SCHEDULER_WEEKDAY_WEDNESDAY;
```

English supplement: Non-repeating schedules ignore weekday matching and behave as one-shot entries.

## States

スケジュールの状態は `app_scheduler_state_t` で確認できます。

| State | Meaning |
| --- | --- |
| `APP_SCHEDULER_STATE_DISABLED` | 無効。発火しない |
| `APP_SCHEDULER_STATE_WAITING` | 待機中。次の時刻または window 開始を待っている |
| `APP_SCHEDULER_STATE_ACTIVE` | window または latched instant が継続中 |
| `APP_SCHEDULER_STATE_STOPPED` | active 状態をユーザーまたはアプリが停止した |

`STOPPED` は window 用です。たとえば 09:00-10:00 のポンプ動作を 09:30 に手動停止した場合、10:00 までは `STOPPED` のまま再 ON しません。10:00 で `ENDED` が出た後、繰り返し schedule なら次回に向けて `WAITING` に戻ります。

`INSTANT` + `LATCHED` の場合、`STOPPED` はその発火を止めた状態です。繰り返し schedule なら、指定時刻の秒を抜けた後に次回へ向けて `WAITING` に戻ります。繰り返さない schedule は停止後に disabled になります。

English supplement: `STOPPED` suppresses reactivation only until the current window ends. It is not a permanent disable.

## Events

発火はキューと owner handler の両方へ通知されます。

| Event | Mode | Meaning |
| --- | --- | --- |
| `APP_SCHEDULER_EVENT_FIRED` | `INSTANT` + `EVENT` | 指定時刻に到達した |
| `APP_SCHEDULER_EVENT_STARTED` | `WINDOW`, `INSTANT` + `LATCHED` | active 状態に入った |
| `APP_SCHEDULER_EVENT_ENDED` | `WINDOW` | window から出た |
| `APP_SCHEDULER_EVENT_STOPPED_BY_USER` | active schedule | `app_scheduler_stop()` により停止された |

アプリは通常 `app_scheduler_register_handler()` で owner ごとの callback を登録します。複数のアプリが横断的にイベントを監視したい場合は `app_scheduler_receive_event()` でキューから読むこともできます。

イベントキュー長は内部で `16` 件です。キューが満杯でイベントを積めなかった場合、その schedule の `missed_event_count` が増えます。この値は `app_scheduler_get_status()` または `app_scheduler_list()` で確認できます。

English supplement: The callback is dispatched immediately after publishing the event to the queue. Callback code should return quickly and should hand off long work to its own task or app logic.

## Public API

利用するファイルでは、次のヘッダーを include します。

```c
#include "app_scheduler.h"
```

初期化は起動処理で一度だけ行います。この時計製品では `cyd_clock_composition` が `app_scheduler_init()` のあとに clock alarm 用 entry の存在確認を行います。

```c
ESP_ERROR_CHECK(app_scheduler_init());
```

登録または更新は `app_scheduler_upsert()` を使います。同じ `owner/tag` がすでにある場合は更新され、なければ空き slot に追加されます。

```c
app_scheduler_config_t config = {
    .owner = "clock",
    .tag = "alarm1",
    .mode = APP_SCHEDULER_MODE_INSTANT,
    .behavior = APP_SCHEDULER_BEHAVIOR_EVENT,
    .enabled = true,
    .repeat = true,
    .weekday_mask = APP_SCHEDULER_WEEKDAY_ALL,
    .at = { .hour = 8, .minute = 0, .second = 0 },
};

ESP_ERROR_CHECK(app_scheduler_upsert(&config));
```

削除は `app_scheduler_remove()` です。

```c
esp_err_t err = app_scheduler_remove("clock", "alarm1");
if (err != ESP_ERR_NOT_FOUND) {
    ESP_ERROR_CHECK(err);
}
```

有効/無効の切り替えは `app_scheduler_set_enabled()` を使います。無効化しても entry 自体は残ります。

```c
ESP_ERROR_CHECK(app_scheduler_set_enabled("clock", "alarm1", false));
```

window を途中停止する場合は `app_scheduler_stop()` を使います。対象が `ACTIVE` でない場合は `ESP_ERR_INVALID_STATE` を返します。

```c
esp_err_t err = app_scheduler_stop("pump", "pumpUp");
if (err != ESP_ERR_INVALID_STATE && err != ESP_ERR_NOT_FOUND) {
    ESP_ERROR_CHECK(err);
}
```

状態確認は `app_scheduler_get_status()`、一覧取得は `app_scheduler_list()` を使います。

```c
app_scheduler_status_t status = { 0 };
if (app_scheduler_get_status("clock", "alarm1", &status) == ESP_OK) {
    /* status.state, status.fired_count, status.missed_event_count を参照できます。 */
}
```

## Instant Example

時計アプリの Alarm 1 のような繰り返しアラームは `INSTANT` + `EVENT` + `repeat = true` で登録します。

```c
app_scheduler_config_t alarm1 = {
    .owner = "clock",
    .tag = "alarm1",
    .mode = APP_SCHEDULER_MODE_INSTANT,
    .behavior = APP_SCHEDULER_BEHAVIOR_EVENT,
    .enabled = true,
    .repeat = true,
    .weekday_mask = APP_SCHEDULER_WEEKDAY_MONDAY |
                    APP_SCHEDULER_WEEKDAY_TUESDAY |
                    APP_SCHEDULER_WEEKDAY_WEDNESDAY |
                    APP_SCHEDULER_WEEKDAY_THURSDAY |
                    APP_SCHEDULER_WEEKDAY_FRIDAY,
    .at = { .hour = 8, .minute = 0, .second = 0 },
};

ESP_ERROR_CHECK(app_scheduler_upsert(&alarm1));
```

Alarm 2 のような1回だけのアラームは `repeat = false` にします。

```c
app_scheduler_config_t alarm2 = {
    .owner = "clock",
    .tag = "alarm2",
    .mode = APP_SCHEDULER_MODE_INSTANT,
    .behavior = APP_SCHEDULER_BEHAVIOR_EVENT,
    .enabled = true,
    .repeat = false,
    .weekday_mask = APP_SCHEDULER_WEEKDAY_ALL,
    .at = { .hour = 8, .minute = 30, .second = 0 },
};

ESP_ERROR_CHECK(app_scheduler_upsert(&alarm2));
```

`repeat = false` の `INSTANT` は、`FIRED` 後に scheduler 側で自動的に disabled になります。アプリ側に独自の設定保存がある場合は、callback でアプリ側設定も無効へ同期してください。

English supplement: The scheduler persists its own `enabled=false` transition for one-shot entries. Application-specific configuration may still need to mirror that state.

## Latched Instant Example

ユーザーが止めるまで active にしたい目覚まし時計は `INSTANT` + `LATCHED` で登録します。

```c
app_scheduler_config_t alarm = {
    .owner = "clock",
    .tag = "wake",
    .mode = APP_SCHEDULER_MODE_INSTANT,
    .behavior = APP_SCHEDULER_BEHAVIOR_LATCHED,
    .enabled = true,
    .repeat = true,
    .weekday_mask = APP_SCHEDULER_WEEKDAY_ALL,
    .at = { .hour = 7, .minute = 0, .second = 0 },
};

ESP_ERROR_CHECK(app_scheduler_upsert(&alarm));
```

この schedule は 07:00:00 に `STARTED` を出して `ACTIVE` になります。アプリは `STARTED` で鳴動を開始し、ユーザー操作で `app_scheduler_stop("clock", "wake")` を呼びます。

```c
static void alarm_scheduler_handler(const app_scheduler_event_t *event, void *ctx)
{
    (void)ctx;

    if (event == NULL || strcmp(event->tag, "wake") != 0) {
        return;
    }

    switch (event->type) {
    case APP_SCHEDULER_EVENT_STARTED:
        alarm_sound_start();
        break;
    case APP_SCHEDULER_EVENT_STOPPED_BY_USER:
        alarm_sound_stop();
        break;
    default:
        break;
    }
}
```

ミュートやスヌーズは scheduler の state ではなく、アラームアプリ側の状態として扱います。scheduler は「この予定は active か、止められたか」までを担当します。

## Window Example

09:00 から 10:00 までポンプを動かすような継続動作は `WINDOW` で登録します。

```c
app_scheduler_config_t pump = {
    .owner = "pump",
    .tag = "pumpUp",
    .mode = APP_SCHEDULER_MODE_WINDOW,
    .behavior = APP_SCHEDULER_BEHAVIOR_EVENT,
    .enabled = true,
    .repeat = true,
    .weekday_mask = APP_SCHEDULER_WEEKDAY_MONDAY |
                    APP_SCHEDULER_WEEKDAY_TUESDAY |
                    APP_SCHEDULER_WEEKDAY_WEDNESDAY,
    .at = { .hour = 9, .minute = 0, .second = 0 },
    .to = { .hour = 10, .minute = 0, .second = 0 },
};

ESP_ERROR_CHECK(app_scheduler_upsert(&pump));
```

アプリ側は `STARTED` で ON、`ENDED` または `STOPPED_BY_USER` で OFF にします。

```c
static void pump_scheduler_handler(const app_scheduler_event_t *event, void *ctx)
{
    (void)ctx;

    if (event == NULL || strcmp(event->tag, "pumpUp") != 0) {
        return;
    }

    switch (event->type) {
    case APP_SCHEDULER_EVENT_STARTED:
        pump_relay_set(true);
        break;
    case APP_SCHEDULER_EVENT_ENDED:
    case APP_SCHEDULER_EVENT_STOPPED_BY_USER:
        pump_relay_set(false);
        break;
    default:
        break;
    }
}

ESP_ERROR_CHECK(app_scheduler_register_handler("pump", pump_scheduler_handler, NULL));
```

手動停止 UI を作る場合は、リレーを直接 OFF するだけでなく `app_scheduler_stop("pump", "pumpUp")` を呼びます。これにより現在 window の残り時間は `STOPPED` になり、次回 window まで scheduler が再度 `STARTED` を出さなくなります。

## Handler Pattern

owner callback は scheduler task から呼ばれます。callback 内では長時間ブロックしないようにしてください。

推奨パターンは以下です。

1. `event->owner` は owner 登録で絞られている前提にする
2. `event->tag` で用途を分岐する
3. `event->type` で ON/OFF/鳴動などの最小処理を行う
4. 時間のかかる処理は自分の task、queue、app state へ渡す

時計アプリでは `owner = "clock"` の handler を登録し、`alarm1` と `alarm2` の `FIRED` で speaker alarm event を再生します。latched instant を使う場合は `STARTED` で鳴動開始、`STOPPED_BY_USER` で鳴動停止にします。

English supplement: Keep scheduler callbacks short. They are notification hooks, not worker threads.

## Persistence

`app_scheduler_upsert()`、`app_scheduler_remove()`、`app_scheduler_set_enabled()` は scheduler の NVS 設定を保存します。

保存されるのは scheduler の config です。`fired_count`、`missed_event_count`、`last_event_at` のような runtime status は再起動でリセットされます。

起動時は NVS から config を読み戻し、各 entry は初期状態へ戻ります。

English supplement: Runtime counters are diagnostic only and are not part of the persisted contract.

## Time Assumptions

scheduler は `localtime_r()` で得られるローカル時刻を使います。時刻が未同期で 2024 年より前に見える間は発火処理を行いません。

このプロジェクトでは `time_sync` が timezone と SNTP 同期を担当します。scheduler を使うアプリは、NTP 同期前の起動直後にはイベントが出ない可能性を前提にしてください。

English supplement: The scheduler intentionally ignores obviously invalid wall-clock time to avoid firing stale schedules during boot before time synchronization.

## Clock Settings Page

この時計プロジェクトでは、`Clock Settings` アプリに `SCHED` ページがあります。

表示内容は以下です。

- 登録数: `schedules: n/5`
- 各 entry: `slot owner/tag mode+behavior state time`

`mode+behavior` は短縮表示です。`ie` は instant/event、`il` は instant/latched、`we` は window/event を表します。

表示幅の都合で `owner` や `tag` は短縮されます。詳細な状態をアプリ側で確認する場合は `app_scheduler_get_status()` または `app_scheduler_list()` を使ってください。

English supplement: Scheduler diagnostics are product UI in this project. Keep reusable `INFO` independent from `app_scheduler`.

## Dependencies

`app_scheduler` は以下のコンポーネントに依存します。

- `freertos`
- `nvs_flash`

利用するコンポーネントは `CMakeLists.txt` の `REQUIRES` に `app_scheduler` を追加してください。

```cmake
idf_component_register(SRCS "my_app.c"
                    INCLUDE_DIRS "include"
                    REQUIRES app_scheduler)
```
