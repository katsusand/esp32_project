# System Apps Split Plan

## Status

この分離計画は進行中です。

開始日:

- 2026-05-01

目的:

- `INFO` と `SETTINGS` を時計アプリ非依存の共通 foreground app として再利用しやすくする
- alarm 設定を時計アプリ固有責務として切り出す
- 将来の別アプリでも `INFO` / `SETTINGS` ボタン導線を共通化できるようにする

## Current State

現状の `components/apps/cyd_system_apps` には、以下が同居しています。

- 共通化しやすい `INFO`
- 共通化しやすい `SETTINGS` の general / network / nvs
- 時計依存の alarm 設定ページ
- touch calibration app

そのため、`SETTINGS` は名前のわりに clock-specific な責務を含んでいます。

English supplement: The current settings app mixes reusable system settings with alarm configuration that belongs to the clock domain.

## Target Shape

最終的には次の責務分離を目指します。

- `system_info_app`
  共通情報表示
- `system_settings_app`
  共通設定
- `system_touch_calibration_app`
  共通 touch calibration
- `cyd_clock_settings_app`
  時計固有設定

共通 `SETTINGS` からは、必要に応じて app 固有設定 app へ遷移できる導線を持たせます。

## TODO

### Step A

公開 API を責務名に寄せる。

- [x] `cyd_info_app_get_app()` を `system_info_app_get_app()` へ整理
- [x] `cyd_settings_app_get_app()` を `system_settings_app_get_app()` へ整理
- [x] `cyd_touch_calibration_app_get_app()` を `system_touch_calibration_app_get_app()` へ整理
- [x] `cyd_clock_app` 側の呼び出しを追従

### Step B

alarm 設定を新しい clock-specific app へ切り出す。

- [x] `components/apps/cyd_clock_settings_app` を追加
- [x] alarm render / action handling を新 component へ移す
- [x] `system_settings_app` から alarm page enum と action 群を除去
- [x] 共通 `SETTINGS` を general / network / nvs に整理

### Step C

共通 `SETTINGS` に app-specific extension 導線を追加する。

- [x] `system_settings_set_extension(...)` API を追加
- [x] extension button のラベルと遷移先 app を差し込めるようにする
- [x] 時計アプリから `Clock Settings` を extension として設定する

## Notes

- 最初から汎用 plugin 機構にはしない
- extension は「ラベル + 遷移先 app」程度の小さい API に留める
- `INFO` は基本的にそのまま共通 app として使う
- `SETTINGS` の戻り先管理は今の `from_app` ベースを継続する

## Verification

各段階で以下を確認する。

- [ ] build 成功
- [ ] clock app から `INFO` へ遷移できる
- [ ] clock app から共通 `SETTINGS` へ遷移できる
- [ ] 共通 `SETTINGS` から `Clock Settings` へ遷移できる
- [ ] `Clock Settings` から戻れる
- [ ] brightness / volume / timezone / Wi-Fi setup / touch calibration の既存導線が維持される
