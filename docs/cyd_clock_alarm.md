# CYD Clock Alarm

## Overview

`cyd_clock_alarm` は、時計アプリ (`cyd_clock_app`) のアラームです。

独立したシステム機能ではなく時計の一部で、インストールするのは `cyd_clock_app_register()` だけです。時計を載せない製品にはアラームも存在せず、メインアプリを時計から差し替えればアラームも一緒に外れます。

アラームは 2 本 (アラーム1 は繰り返し、アラーム2 は 1 回だけ) で、どちらも `app_scheduler` の schedule として保存し、発火すると speaker の alarm event を鳴らします。

English contract: the alarm is part of the clock app, not a system feature. Only `cyd_clock_app_register()` installs it. A product without the clock has no alarm, and swapping the main app removes it.

## Ownership

アラームを操作する UI は 2 か所あり、どちらも時計に属します。

- 時計画面のアラームのボタン (`cyd_clock_app`): 有効/無効を切り替える
- 時計の設定の「アラーム1」「アラーム2」page (`cyd_clock_settings_app`): 時刻と曜日を変更する

どちらもこの component の API だけを使い、scheduler 上の識別子や既定値を知りません。

```text
cyd_clock_app
  -> cyd_clock_alarm -> app_scheduler
  |                  -> cyd_speaker (private)
  -> cyd_clock_settings_app -> cyd_clock_alarm
```

以前は既定値 (07:00 / 07:30、無効) が `cyd_clock_composition` と `cyd_clock_settings_app` の 2 か所に重複して書かれ、発火時の handler は composition にありました。composition は製品全体の組み立て役なので、アラームが時計の機能なのか製品全体の機能なのかが読み取りにくい構成でした。

現在は既定値、handler、scheduler 上の識別子 (`owner = "clock"`、tag `alarm1` / `alarm2`) はこの component にだけあります。

English supplement: Defaults, the event handler and the scheduler identity live only here. Callers use the API and must not spell `"clock"` / `"alarm1"` themselves.

## Stateless Design

この component は状態を持ちません。設定の保存先は `app_scheduler` の entry だけで、API は呼ばれるたびに scheduler を読み書きします。

過去の `cyd_alarm` component は独自の設定保存を持ち、scheduler と二重管理になっていたため廃止されました。`cyd_clock_alarm` はその再導入ではありません。保存先を自分で持たないので、二重管理の問題は起きません。

English contract: stateless. The scheduler entry is the only storage. Do not add a separate NVS key or an in-memory cache here; a second store kept in sync with the scheduler is what got `cyd_alarm` removed.

## Alarms

| ID | scheduler tag | repeat | weekday | 既定値 |
| --- | --- | --- | --- | --- |
| `CYD_CLOCK_ALARM_1` | `alarm1` | する | ユーザーが選択 (空も可) | 07:00、無効 |
| `CYD_CLOCK_ALARM_2` | `alarm2` | しない (1 回だけ) | 使わない (全曜日で保存) | 07:30、無効 |

どちらも `APP_SCHEDULER_MODE_INSTANT` + `APP_SCHEDULER_BEHAVIOR_EVENT`、`scope = APP_SCHEDULER_SCOPE_APP` です。

アラーム1 の曜日を 1 つも選ばない状態は有効な設定で、その場合は発火しません。アラーム2 は発火後に scheduler が自動で無効にします。

## Public API

```c
#include "cyd_clock_alarm.h"
```

| API | 内容 |
| --- | --- |
| `cyd_clock_alarm_register()` | handler を登録し、2 本の schedule を用意する |
| `cyd_clock_alarm_get(id, &config)` | 時刻・曜日・有効状態を読む。schedule がまだ無ければ既定値を返す |
| `cyd_clock_alarm_set(id, &config)` | 保存する。hour `0..23`、minute `0..59` の範囲外は `ESP_ERR_INVALID_ARG` |
| `cyd_clock_alarm_is_enabled(id)` | 有効かどうか。schedule が無い、または scheduler 未初期化なら `false` |
| `cyd_clock_alarm_set_enabled(id, enabled)` | 有効/無効だけを切り替える |

`cyd_clock_alarm_config_t` が持つのは `hour`、`minute`、`weekday_mask`、`enabled` だけです。mode、behavior、repeat、scope といった scheduler 上の形はこの component が決めるので、呼び出し側からは指定できません。

`weekday_mask` のビットは `APP_SCHEDULER_WEEKDAY_*` をそのまま使います。そのためヘッダーは `app_scheduler.h` を include し、`app_scheduler` は public な依存です。

English supplement: The config struct is the clock's view of an alarm. Schedule shape (mode, repeat, scope) is fixed by this component, so callers cannot create an alarm that the handler or the settings page would not understand.

## Registration

`cyd_clock_alarm_register()` は起動のたびに以下を行います。

1. `owner = "clock"` の handler を登録する
2. 各アラームについて、schedule が無ければ既定値で作る
3. schedule が `FEATURE` scope なら、時刻などはそのままで `APP` scope へ移す

3 は scope 導入前のデータの移行です。それ以前の schedule はすべて `ftr_sched` に保存されていたため、`FEATURE` として読み込まれます。

`app_scheduler_init()` の後に呼ぶ必要があります。scheduler の変更系 API は init 前だと `ESP_ERR_INVALID_STATE` を返すからです。この製品では composition が init を済ませてから `cyd_clock_app_register()` を呼ぶので、composition がこの関数を直接呼ぶ必要はありません。

失敗しても時計自体は登録済みで、時計として動作します。composition は `register clock app failed` として error log に記録し、起動を続けます。

English contract: call after `app_scheduler_init()`. A failure leaves the clock registered and usable without alarms; the composition logs it and keeps booting.

## Storage and Clear App Data

アラームは app scope なので `app_sched` namespace に保存されます。

- 設定の「アプリのデータを消去」で消え、次回起動時に既定値 (無効) で作り直されます
- メインアプリを時計以外に差し替えた場合、`clock/alarm1` と `clock/alarm2` は handler のいない残骸として scheduler の slot を 2 つ占有します。時計と一緒に時計の設定の「スケジュール」page も外れるので、システム情報の「NVS」page に `app_sched` として残っていることを確認し、設定の「アプリのデータを消去」で消します

English supplement: App scope is what makes the alarm's data follow the app. Clear App Data resets it, and after an app swap it counts as leftover app data instead of feature data that survives every reset.

## Limitations

時計はこの基盤の動作確認用サンプルなので、アラームも最小限の実装にとどめています。次の制限は把握したうえで受け入れています（2026-09-26 のレビューの指摘を受け、2026-09-28 に判断）。

- 判定は `app_scheduler` の `INSTANT` で、時刻がちょうどその秒に一致したときだけ鳴る。NTP 補正などで時刻がその秒を飛び越えると鳴らない（`time_tick` の `jumped` / `delta_sec` は使っていない）
- 音は約1秒で1回鳴るだけ
- 停止やスヌーズの UI はない

本格的なアラームが必要な app を作る場合は、飛び越えを考慮した判定と、鳴動・停止の UI をその app 側で用意してください。

English supplement: known and accepted limitations of a sample feature. Do not harden them in the clock unless asked; a product that needs a real alarm should build it in its own app.

## Dependencies

- `app_scheduler` (public)
- `cyd_speaker` (private)

`cyd_speaker` の初期化は `system_boot` が行います。
