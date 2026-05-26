# Components Architecture

## Status

このドキュメントは、`components/` 再編が完了したあとの現状構成を説明するためのものです。

再編完了日:

- 2026-05-01

English supplement: This document is no longer a migration TODO. It describes the current stable component layout and the intended ownership boundaries.

## Overview

このプロジェクトでは、`components/` を責務ごとに 5 つの層へ分けています。

```text
components/
  apps/
  framework/
  platform/
  services/
  support/
```

最も重要な前提は、`components/apps/` がメインアプリ層であり、その他の層は土台として再利用する前提だということです。

時計プロジェクトでは `components/apps/` に clock 系 app を載せていますが、派生プロジェクトではここを差し替え、`platform/`、`framework/`、`services/`、`support/` はできるだけそのまま活かす想定です。

English supplement: Treat `apps/` as the replaceable application layer. The other groups form reusable infrastructure for derived projects.

## Current Layout

現在の主な構成は以下です。

```text
components/
  apps/
    cyd_alarm/
    cyd_clock_app/
    cyd_clock_settings_app/
    cyd_system_apps/

  framework/
    app_shell/
    cyd_ui/
    system_boot/

  platform/
    cyd_display/
    cyd_input/
    cyd_speaker/
    cyd_status_led/

  services/
    app_scheduler/
    cyd_wifi_setup/
    esp32_wifi_sta/
    radio_manager/
    time_sync/
    time_tick/
    wifi_manager/
    wifi_profile_store/

  support/
    app_diagnostics/
    lovyangfx_wrapper/
```

## Layer Roles

### `apps/`

foreground app を置く層です。ここがプロジェクト固有のメインアプリ領域です。

現在の例:

- `cyd_clock_app/`
  通常時の時計表示
- `cyd_clock_settings_app/`
  時計固有設定
- `cyd_system_apps/`
  `INFO` / `SETTINGS` のような system-level app
- `cyd_alarm/`
  現在は app 層に置いている alarm 機能

新しい派生プロジェクトでは、まずこの層を整理することを前提にします。

English supplement: `apps/` is where project-specific behavior belongs. Replacing or shrinking this layer is the normal path when deriving a new product from this repository.

### `framework/`

app を載せるための実行基盤です。

- `app_shell/`
  foreground UI app の切り替え実行基盤
- `cyd_ui/`
  画面組み立て helper
- `system_boot/`
  起動 orchestration と subsystem の初期化順

`main/main.c` は薄く保ち、起動責務は `system_boot` へ寄せます。

English supplement: `framework/` should stay thin and generic. It provides execution structure rather than domain-specific application behavior.

### `platform/`

CYD board のハード寄り制御を置く層です。

- `cyd_display/`
- `cyd_input/`
- `cyd_speaker/`
- `cyd_status_led/`

表示、入力、LED、speaker のような board / peripheral 制御はここへ置きます。

English supplement: `platform/` owns device-facing behavior tied to the CYD board rather than application use cases.

### `services/`

複数 app から再利用される共通 service を置く層です。

- `wifi_manager/`
  Wi-Fi 接続状態と接続ライフサイクル
- `radio_manager/`
  通信利用権の調停
- `time_sync/`
  NTP/SNTP による時刻同期
- `time_tick/`
  秒変化配信
- `app_scheduler/`
  時刻ベースの共通スケジューラー
- `wifi_profile_store/`
  Wi-Fi credential 保存
- `esp32_wifi_sta/`
  ESP-IDF Wi-Fi STA wrapper
- `cyd_wifi_setup/`
  Wi-Fi setup app と helper

新しい機能が「clock 専用ではなく、別 app からも使えそうか」で迷った場合は、まず `services/` に置くべきか検討します。

English supplement: `services/` contains reusable domain logic with state, policy, or background task ownership. If a feature is not specific to one foreground app, it probably belongs here.

### `support/`

直接の app / service ではない補助 component を置く層です。

- `app_diagnostics/`
- `lovyangfx_wrapper/`

diagnostics、wrapper、補助ライブラリ連携のようなものはここへ置きます。

## Design Rules

この構成では、以下を基本ルールとします。

- メインアプリは `apps/` に置く
- board / peripheral 制御は `platform/` に置く
- app 実行基盤や UI helper は `framework/` に置く
- 複数 app から使う共通機能は `services/` に置く
- 補助 component や wrapper は `support/` に置く

English supplement: Choose the layer based on ownership and reuse boundaries, not just implementation convenience.

## Startup Boundary

起動経路は現在、次のように分かれています。

1. `main/app_main()`
2. `system_boot_start(...)`
3. platform / service 初期化
4. `app_shell_start(home_app)`

これにより `main/main.c` は薄く保たれ、起動順や boot-time 判定は `system_boot` 側で一元管理されます。

English supplement: `main` should stay minimal. Boot sequencing belongs to `system_boot`, and foreground app execution belongs to `app_shell`.

## Guidance For Derived Projects

このリポジトリを土台に別用途へ派生するときは、次の順で考えると整理しやすくなります。

1. `apps/` のうち不要な app を削る
2. 新しい main app を `apps/` に追加する
3. 既存 app から切り出せる共通機能があれば `services/` へ上げる
4. board 依存の処理は `platform/` を再利用する
5. 起動順変更が必要なら `system_boot` を調整する

時計機能をベースにしない派生では、まず `apps/` を入れ替え対象として見るのが基本です。

English supplement: Most derived projects should change `apps/` first and keep the infrastructure layers as stable as possible.

## Notes

- `cyd_alarm/` は現在 `apps/` にありますが、今後 service 化を検討する余地があります
- `cyd_system_apps/` は system-level app ですが、foreground app であるため `apps/` に置いています
- `cyd_wifi_setup/` は UI を持ちますが、Wi-Fi service の一部として `services/` に置いています

English supplement: Layering is based on primary ownership. A component may expose UI and still belong to `services/` if it is fundamentally part of a reusable service workflow.
