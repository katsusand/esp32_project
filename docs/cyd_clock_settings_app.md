# CYD Clock Settings App

## Overview

`cyd_clock_settings_app` は、時計製品固有の設定・診断を扱う foreground app です。

共通 `system_settings_app` から `Clock Settings` extension として遷移します。これにより、共通 system UI は brightness / volume / network / NVS などに集中し、時計固有の alarm / scheduler 表示はこの app に閉じ込めます。

English supplement: This component is product-specific UI. It edits the clock's alarms through `cyd_clock_alarm` and shows `app_scheduler` diagnostics, while `cyd_system_apps` should stay reusable.

## Pages

現在は以下の page を持ちます。

- `ALARM1`
  Alarm 1 の hour / minute と weekday mask を変更する
- `ALARM2`
  Alarm 2 の hour / minute を変更する
- `SCHED`
  `app_scheduler` の登録状況を診断表示する

画面下部の `<` / `>` で page を切り替え、左上の `<<` で遷移元 app へ戻ります。

## Alarm Pages

`ALARM1` / `ALARM2` は `cyd_clock_alarm_get()` / `cyd_clock_alarm_set()` で読み書きします。scheduler 上の owner/tag や既定値はこの app には書かれていません。以前はここと `cyd_clock_composition` に既定値が重複していました ([cyd_clock_alarm.md](cyd_clock_alarm.md))。

- hour は `0..23`
- minute は `0..59`
- `ALARM1` は `SUN` から `SAT` の weekday button を持つ
- 曜日を 1 つも選ばない状態も有効。その場合 `ALARM1` は発火しない（「絶対に鳴らさない」という明示的な設定として扱う）

`-` / `+` の stepper は `PRESS` と `REPEAT` で反応します。通常 button は `RELEASE` 時に同じ button 上で離された場合だけ確定します。

English supplement: Stepper actions are intentionally handled separately from confirmed tap actions so long-press repeat works without affecting normal buttons.

## Scheduler Page

`SCHED` は `app_scheduler_list()` の結果を表示する診断 page です。

表示内容は以下です。

- `schedules: n/5`
- 各 entry: `slot owner/tag mode+behavior scope state time`

`mode+behavior` は短縮表示です。

- `ie`: instant / event
- `il`: instant / latched
- `we`: window / event

`scope` は `a` (app scope、`app_sched`) または `f` (feature scope、`ftr_sched`) です。時計のアラームは `a` になります。`f` のまま残っていれば、scope 導入前の保存データがまだ移行されていないことを示します。

`state` は `dis`、`wait`、`active`、`stop` の短縮表示です。表示幅の都合で `owner` と `tag` は短縮されます。

このページは scheduler 全体を表示し、時計以外の owner の entry も出ます。

## Dependencies

`cyd_clock_settings_app` は以下のコンポーネントに依存します。

- `app_registry`
- `app_scheduler` (`SCHED` page)
- `app_shell`
- `cyd_clock_alarm` (`ALARM1` / `ALARM2` page)
- `cyd_display`
- `cyd_input`
- `cyd_ui`
