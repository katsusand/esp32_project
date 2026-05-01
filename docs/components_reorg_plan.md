# Components Reorg Plan

## Status

この再編計画は完了済みです。

完了日:

- 2026-05-01

完了項目:

- Step 1: `framework/` と `platform/` の再配置
- Step 2: `services/` の再配置
- Step 3: `apps/` と `support/` の再配置
- `system_boot` の追加と `main/main.c` の簡素化

## Overview

`components/` がフラットなまま育ってきたため、board 制御、共通 service、UI app、実行基盤が同じ階層に並ぶ状態になっています。

このドキュメントは、今後の再編を安全に進めるための TODO と見通しを整理するためのものです。

English supplement: The goal is to improve navigability and reuse first, without mixing directory cleanup with behavior changes.

## Current Problem

現状では以下が混ざりやすくなっています。

- board / peripheral control
- Wi-Fi / NTP / NVS を伴う共通 service
- foreground UI app
- app 実行基盤や UI helper
- 補助 library / diagnostics

そのため、新しいアプリを追加するときに「どこへ置くべきか」が直感的でなくなりやすく、再利用の境界も読み取りにくくなります。

## Target Layout

再編の第一目標は、責務の島を作ることです。

```text
components/
  platform/
    cyd_display/
    cyd_input/
    cyd_speaker/
    cyd_status_led/

  services/
    cyd_wifi_setup/
    esp32_wifi_sta/
    time_sync/
    wifi_manager/
    wifi_profile_store/

  apps/
    cyd_clock_app/
    cyd_system_apps/
    cyd_alarm/            # temporary placement; revisit later

  framework/
    app_shell/
    cyd_ui/

  support/
    app_diagnostics/
    lovyangfx_wrapper/
```

## Design Intent

各グループの意味は以下です。

- `platform/`
  CYD board の表示、入力、LED、speaker など、ハード寄り component を置く
- `services/`
  Wi-Fi、time sync、credential store など、複数 app から再利用される機能 service を置く
- `apps/`
  `app_shell` 上で動く foreground app を置く
- `framework/`
  app 実行基盤や UI 組み立て helper を置く
- `support/`
  diagnostics や wrapper など、直接の app/service ではない補助 component を置く

English supplement: This layout is meant to express ownership boundaries, not just naming preference.

## Migration Policy

今回の再編では、以下を原則とします。

- まずは directory layout の整理を優先する
- 1 step の中で挙動変更を混ぜない
- component 名、public header 名、NVS namespace は当面維持する
- include 修正と CMake 追従は行うが、API の大規模 rename は後回しにする
- 各 step ごとにビルド確認する

English supplement: Keep each step reviewable by separating relocation from refactoring.

## TODO

### Step 1

`framework/` と `platform/` を分ける。

対象:

- `components/framework/app_shell`
- `components/framework/cyd_ui`
- `components/platform/cyd_display`
- `components/platform/cyd_input`
- `components/platform/cyd_speaker`
- `components/platform/cyd_status_led`

TODO:

- 新しい親 directory を作る
- 対象 component を移動する
- `CMakeLists.txt` と component discovery が壊れないことを確認する
- `#include "..."` の解決が維持されることを確認する
- build が通ることを確認する

期待効果:

- 実行基盤と board 制御の境界が読みやすくなる
- 次の app が platform API をどこから使うか明確になる

### Step 2

`services/` をまとめる。

対象:

- `components/services/cyd_wifi_setup`
- `components/services/esp32_wifi_sta`
- `components/services/time_sync`
- `components/services/wifi_manager`
- `components/services/wifi_profile_store`

TODO:

- Wi-Fi / NTP / credential 保存の component を service island として集約する
- 相互依存の include / `REQUIRES` / `PRIV_REQUIRES` を点検する
- build が通ることを確認する

期待効果:

- 新規 app から見た再利用対象が明確になる
- Wi-Fi 系の責務を一か所で追いやすくなる

### Step 3

`apps/` と `support/` を整理する。

対象:

- `components/apps/cyd_clock_app`
- `components/apps/cyd_system_apps`
- `components/apps/cyd_alarm`
- `components/support/app_diagnostics`
- `components/support/lovyangfx_wrapper`

TODO:

- foreground app 群を `apps/` に寄せる
- support component を `support/` に寄せる
- `cyd_alarm` の責務を再確認する

期待効果:

- app と app 以外の違いがはっきりする
- 次の app 追加時の置き場所が迷いにくくなる

## Decision Notes

`components/` 再編の完了後に `system_boot` を新設し、`main/main.c` の起動 orchestration を切り出しました。

理由:

- `main/main.c` の include と初期化責務を減らせた
- directory 再編後に実施したため、構造整理と起動責務整理を分離できた
- 起動順や boot-time touch 判定を `system_boot` 側へ集約できた

English supplement: `system_boot` was intentionally deferred until after the component boundaries became clearer.

## Future Outlook

再編後は、以下の次段階を検討しやすくなります。

- 新規 foreground app 用の template component を用意する
- `wifi_manager_user_t` を拡張し、time sync 以外の Wi-Fi consumer を追加しやすくする
- 端末共通設定と app 固有設定の境界をより明確にする
- `cyd_alarm` を app 機能として残すか、共通 service に上げるかを判断する

## Non Goals

この再編 TODO では、以下は対象外とします。

- UI 仕様変更
- Wi-Fi 接続ポリシー変更
- time sync の挙動変更
- NVS key / namespace の変更
- release / dev build 方針の変更

## Progress Tracking

進捗管理用の簡易 checklist として使う場合は、以下を更新します。

- [x] Step 1: `framework/` と `platform/` の再配置
- [x] Step 2: `services/` の再配置
- [x] Step 3: `apps/` と `support/` の再配置
- [x] 再編後に `system_boot` を追加して `main/main.c` を薄くする
