# CYD Clock Settings App

## Overview

`cyd_clock_settings_app` は、時計製品固有の設定・診断を扱う foreground app です。

共通 `system_settings_app` から `Clock Settings` extension として遷移します。これにより、共通 system UI は brightness / volume / network / NVS などに集中し、時計固有の alarm / scheduler 表示はこの app に閉じ込めます。

English supplement: This component is product-specific UI. It edits the clock's alarms through `cyd_clock_alarm` and shows `app_scheduler` diagnostics, while `cyd_system_apps` should stay reusable.

## Pages

現在は以下の page を持ちます。見出しは「時計の設定」で、文字はすべて 16px です。画面の組み立ては `cyd_clock_settings_view.c` にあります。

| 表示名 | 内容 |
|---|---|
| アラーム1 | 時・分と曜日 (日〜土のボタン) を変更する |
| アラーム2 | 時・分を変更する |
| 情報画面 | 「情報をPINで保護」のスイッチ。オンにすると、時計の「情報」ボタンで開くシステム情報に設定の PIN を求める |
| スケジュール | `app_scheduler` の登録状況を診断表示する |

画面下部の「前へ」「次へ」で page を切り替え、左上の「戻る」で遷移元 app へ戻ります。

## Alarm Pages

「アラーム1」「アラーム2」は `cyd_clock_alarm_get()` / `cyd_clock_alarm_set()` で読み書きします。scheduler 上の owner/tag や既定値はこの app には書かれていません。以前はここと `cyd_clock_composition` に既定値が重複していました ([cyd_clock_alarm.md](cyd_clock_alarm.md))。

- hour は `0..23`
- minute は `0..59`
- アラーム1 は日〜土の曜日ボタンを持つ。選んだ曜日は塗りで表示する
- 曜日を 1 つも選ばない状態も有効。その場合アラーム1 は発火しない（「絶対に鳴らさない」という明示的な設定として扱う）

「−」「+」の stepper は `PRESS` と `REPEAT` で反応します。時・分は端で反対側へ回り込みます。通常 button は `RELEASE` 時に同じ button 上で離された場合だけ確定します。

English supplement: Stepper actions are intentionally handled separately from confirmed tap actions so long-press repeat works without affecting normal buttons.

## Scheduler Page

「スケジュール」は `app_scheduler_list()` の結果を表示する診断 page です。

表示内容は以下です。

- 「登録 n / 5 件」
- 各 entry を 2 行で: 1 行目に `slot owner/tag` と状態 (待機中 / 動作中 / 無効 / 停止)、2 行目に時刻と `edge` / `latched`、`app` / `feature`。owner・tag と behavior・scope は `app_scheduler` の識別子・用語なので英語のまま

`mode+behavior` は短縮表示です。

- `ie`: instant / event
- `il`: instant / latched
- `we`: window / event

`scope` は `a` (app scope、`app_sched`) または `f` (feature scope、`ftr_sched`) です。時計のアラームは `a` になります。`f` のまま残っていれば、scope 導入前の保存データがまだ移行されていないことを示します。

`state` は `dis`、`wait`、`active`、`stop` の短縮表示です。表示幅の都合で `owner` と `tag` は短縮されます。

このページは scheduler 全体を表示し、時計以外の owner の entry も出ます。

## Info Page

「情報画面」は、時計の「情報」ボタンで開くシステム情報を設定の PIN で守るかを選ぶ page です ([Settings PIN](settings_pin.md#guarding-other-apps))。

- システム情報は親の部品ですが、それを時計の画面から開けるようにしているのは時計です。守るかどうかも時計の設定として持ちます。設定の「設定のPIN」ページには置きません
- 選んだ値は `app_clock` / `info_pin` (u8) に保存する。app scope なので、時計を外すと一緒に消え、「アプリのデータを消去」でもオフに戻る
- 起動時は `cyd_clock_app_register()` が `cyd_clock_settings_app_init()` を呼び、保存した値で `settings_pin_guard_app(system_info_app_get_app(), "システム情報", on)` を呼ぶ
- 設定の PIN機能がオフのあいだは、スイッチは行ごとグレーで押せない。保存した値は残り、PIN機能をオンにすると効く

English contract: the clock owns the choice and its storage; the PIN module only keeps the list of guarded apps in RAM. A product that opens system info from its own app makes the same call with its own setting.

## Dependencies

`cyd_clock_settings_app` は以下のコンポーネントに依存します。

- `app_registry`
- `app_scheduler` (「スケジュール」page)
- `app_shell`
- `cyd_clock_alarm` (「アラーム1」「アラーム2」page)
- `cyd_display`
- `cyd_input`
- `cyd_system_apps` (「情報画面」page: `settings_pin.h` と `system_info_app_get_app()`)
- `cyd_ui`
- `nvs_flash`、`nvs_schema` (「情報画面」page の保存)
