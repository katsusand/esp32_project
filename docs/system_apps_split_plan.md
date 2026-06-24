# System Apps Split Plan

## Status

この分離計画は完了済みです。alarm 設定と scheduler 診断表示は `cyd_clock_settings_app` 側へ移動済みで、`cyd_system_apps` は共通 `INFO` / `SETTINGS` / touch calibration に集中しています。

開始日:

- 2026-05-01

目的:

- `INFO` と `SETTINGS` を時計アプリ非依存の共通 foreground app として再利用しやすくする
- alarm 設定を時計アプリ固有責務として切り出す
- 将来の別アプリでも `INFO` / `SETTINGS` ボタン導線を共通化できるようにする

## Current State

現在の `components/apps/cyd_system_apps` には、以下が残っています。

- 共通化しやすい `INFO`
- 共通化しやすい `SETTINGS` の general / network / nvs
- touch calibration app

時計依存の alarm 設定ページと scheduler 診断ページは `components/apps/cyd_clock_settings_app` へ移動済みです。

English supplement: Reusable system apps should not depend on clock-domain services such as alarm or scheduler diagnostics.

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

## Completed Work

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

### Step D

clock-specific diagnostics を common system UI から外す。

- [x] scheduler 診断表示を `INFO` から除去
- [x] scheduler 診断表示を `cyd_clock_settings_app` の `SCHED` page へ移動
- [x] `cyd_system_apps` から `app_scheduler` 依存を除去

## Remaining Candidates

分離自体の未完了 TODO はありません。

次に構造整理を進めるなら、以下が候補です。

- clock alarm は `app_scheduler` へ統合済み。`cyd_alarm` の再導入はしない
- `cyd_system_apps` の network 設定が Wi-Fi 前提でよいか、派生プロジェクト向けに optional 化するかを検討する
- `products/` の composition を増やす場合に、共通 composition helper が本当に必要になるかを見極める

English supplement: Do not add a generic plugin system until multiple products need the same extension mechanism.

## Notes

- 最初から汎用 plugin 機構にはしない
- extension は「ラベル + 遷移先 app」程度の小さい API に留める
- `INFO` は基本的にそのまま共通 app として使う
- `SETTINGS` の戻り先管理は今の `from_app` ベースを継続する

## Verification

各段階で以下を確認する。

- [x] build 成功
- [x] clock app から `INFO` へ遷移できる
- [x] clock app から共通 `SETTINGS` へ遷移できる
- [x] 共通 `SETTINGS` から `Clock Settings` へ遷移できる
- [x] `Clock Settings` から戻れる
- [x] brightness / volume / timezone / Wi-Fi setup / touch calibration の既存導線が維持される
