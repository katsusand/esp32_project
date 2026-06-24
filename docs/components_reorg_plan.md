# Components Architecture

## Status

このドキュメントは、`components/` 再編が完了したあとの現状構成を説明するためのものです。

再編完了日:

- 2026-05-01

English supplement: This document is no longer a migration TODO. It describes the current stable component layout and the intended ownership boundaries.

## Overview

このプロジェクトでは、`components/` を責務ごとに 6 つの層へ分けています。

```text
components/
  apps/
  framework/
  products/
  platform/
  services/
  support/
```

最も重要な前提は、`components/products/` が製品ごとの接続層、`components/apps/` がメインアプリ層であり、その他の層は土台として再利用する前提だということです。

時計プロジェクトでは `components/products/cyd_clock_composition/` が clock 系 app と service を接続しています。派生プロジェクトでは `products/` と `apps/` を差し替え、`platform/`、`framework/`、`services/`、`support/` はできるだけそのまま活かす想定です。

English supplement: Treat `products/` as the replaceable composition layer and `apps/` as the replaceable application layer. The other groups form reusable infrastructure for derived projects.

## Current Layout

現在の主な構成は以下です。

```text
components/
  apps/
    cyd_clock_app/
    cyd_clock_settings_app/
    cyd_system_apps/

  framework/
    app_shell/
    cyd_ui/
    cyd_text_input/
    system_boot/

  products/
    cyd_clock_composition/

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
    wifi_connection/
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
新しい派生プロジェクトでは、まずこの層を整理することを前提にします。

English supplement: `apps/` is where project-specific behavior belongs. Replacing or shrinking this layer is the normal path when deriving a new product from this repository.

### `framework/`

app を載せるための実行基盤です。

- `app_shell/`
  foreground UI app の切り替え実行基盤
- `cyd_ui/`
  画面組み立て helper
- `system_boot/`
  NVS、CYD platform、入力補正などの共通boot処理
- `cyd_text_input/`
  foreground app から使う共通テキスト入力UI

`main/main.c` は薄く保ち、製品固有の起動責務は`products/`のcompositionへ寄せます。

English supplement: `framework/` should stay thin and generic. It provides execution structure rather than domain-specific application behavior.

### `products/`

製品ごとのcompositionを置く層です。

- `cyd_clock_composition/`
  時計製品で使うservice、app、起動順、app_shellへの製品固有hookを接続する

`products/` は派生プロジェクトで差し替えやすい境界です。`main/main.c` はここから起動するcompositionを選ぶだけに留めます。

English supplement: Product composition owns product-specific wiring. It may connect generic framework hooks to domain services, while framework components should not depend on those services directly.

## Build Feature Boundaries

派生プロジェクト向けに機能をバイナリから外す場合、component の `CMakeLists.txt` では Kconfig の `CONFIG_*` を source/dependency selection に使いません。

Wi-Fi STA のような大きい機能は、`APP_WIFI_STA=0` のように CMake 再構成時に見える build-time switch で実装と依存を切り替えます。詳細は [Build Feature Switches](build_feature_switches.md) を参照してください。

English supplement: Kconfig configures behavior after a feature is included. Build-time feature switches decide whether the feature and its heavy dependencies are included at all.

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

- `wifi_connection/`
  Wi-Fi 接続状態と接続ライフサイクル
- `radio_manager/`
  通信利用権の調停
- `time_sync/`
  NTP/SNTP による時刻同期
- `time_tick/`
  秒変化配信
- `app_scheduler/`
  時刻ベースの共通スケジューラー
- `status_indicator/`
  service 状態を status LED 表示へ変換する
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
- 製品固有の起動順や cross-component wiring は `products/` に置く

English supplement: Choose the layer based on ownership and reuse boundaries, not just implementation convenience.

## Startup Boundary

起動経路は現在、次のように分かれています。

1. `main/app_main()`
2. product composition
3. `system_boot_start(...)`による共通基盤初期化
4. compositionによる製品固有service初期化
5. `app_shell_start(home_app)`

これにより`main/main.c`は薄く保たれ、共通boot判定は`system_boot`、製品固有の起動順はcompositionで管理されます。

English supplement: `main` selects a product composition. Generic boot belongs to `system_boot`; product wiring belongs to the composition; foreground execution belongs to `app_shell`.

## Guidance For Derived Projects

このリポジトリを土台に別用途へ派生するときは、次の順で考えると整理しやすくなります。

1. `apps/` のうち不要な app を削る
2. 新しい main app を `apps/` に追加する
3. 既存 app から切り出せる共通機能があれば `services/` へ上げる
4. board 依存の処理は `platform/` を再利用する
5. 製品固有の起動順はproduct compositionで調整する

時計機能をベースにしない派生では、まず `apps/` を入れ替え対象として見るのが基本です。

English supplement: Most derived projects should change `apps/` first and keep the infrastructure layers as stable as possible.

## Notes

- clock alarm は独立 component を持たず、現在は `app_scheduler` を clock app から直接利用する構成です
- `cyd_system_apps/` は system-level app ですが、foreground app であるため `apps/` に置いています
- `cyd_wifi_setup/` は UI を持ちますが、Wi-Fi service の一部として `services/` に置いています
- `app_shell` のような framework component は domain service へ直接依存させず、必要な製品固有判定は composition が hook として登録します

English supplement: Layering is based on primary ownership. A component may expose UI and still belong to `services/` if it is fundamentally part of a reusable service workflow.
