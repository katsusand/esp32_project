# CYD Clock Settings App

## Overview

`cyd_clock_settings_app` は、時計製品固有の設定・診断を扱う foreground app です。

共通 `system_settings_app` から `Clock Settings` extension として遷移します。これにより、共通 system UI は brightness / volume / network / NVS などに集中し、時計固有の alarm / scheduler 表示はこの app に閉じ込めます。

English supplement: This component is product-specific UI. It manipulates clock-owned `app_scheduler` entries directly, while `cyd_system_apps` should stay reusable.

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

`ALARM1` / `ALARM2` は `app_scheduler` の clock-owned entry を直接読み書きします。

- hour は `0..23`
- minute は `0..59`
- `ALARM1` は `SUN` から `SAT` の weekday button を持つ

`-` / `+` の stepper は `PRESS` と `REPEAT` で反応します。通常 button は `RELEASE` 時に同じ button 上で離された場合だけ確定します。

English supplement: Stepper actions are intentionally handled separately from confirmed tap actions so long-press repeat works without affecting normal buttons.

## Scheduler Page

`SCHED` は `app_scheduler_list()` の結果を表示する診断 page です。

表示内容は以下です。

- `schedules: n/5`
- 各 entry: `slot owner/tag mode+behavior state time`

`mode+behavior` は短縮表示です。

- `ie`: instant / event
- `il`: instant / latched
- `we`: window / event

`state` は `dis`、`wait`、`active`、`stop` の短縮表示です。表示幅の都合で `owner` と `tag` は短縮されます。

## Dependencies

`cyd_clock_settings_app` は以下のコンポーネントに依存します。

- `app_scheduler`
- `app_shell`
- `app_scheduler`
- `cyd_display`
- `cyd_input`
- `cyd_ui`
