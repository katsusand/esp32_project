# NVS Storage

## Overview

このドキュメントは、このリポジトリにおける NVS 保存方針、現在の保存項目一覧、今後の整理方針をまとめたものです。

目的は以下です。

- system 設定と app / feature 設定を混同しない
- 派生プロジェクトで「残す設定」と「消す設定」を分けやすくする
- NVS の ownership を明確にし、保存責務のぶれを減らす

English supplement: Keep NVS ownership explicit. Do not treat NVS as one flat bag of unrelated values.

## Design Rule

このプロジェクトでは、NVS の読み書きを巨大な 1 コンポーネントへ集約するのではなく、次の 2 層に分けて扱う方針とします。

- 保存値の ownership は各 component / feature に残す
- NVS の作法、命名規約、reset 方針は共通ルールとして管理する

つまり、`time_sync` の設定は `time_sync` が所有し、Wi-Fi profile は `wifi_profile_store` が所有します。一方で namespace 命名規約や「どこまでを system reset で消すか」といった横断ルールはドキュメントと共通 helper で揃えます。

English supplement: Ownership stays local; conventions become shared.

## Storage Classes

NVS 項目は、次の 3 クラスで整理します。

### `system`

board や共通 UI 基盤に紐づき、製品をまたいでも意味が変わりにくい設定です。

例:

- display brightness
- touch calibration
- app shell idle timeout
- sound / volume

### `feature`

特定 feature が有効なときだけ意味を持つ、再利用可能な設定です。

例:

- time sync interval
- timezone
- Wi-Fi profiles
- radio idle timeout
- 将来の ESPNOW channel / peer policy

feature をビルドから外した場合、対応する NVS 値は「残っていても未使用の dormant data」として扱って構いません。

English supplement: Feature-scoped data may legitimately exist even when a build no longer uses that feature.

### `app`

特定 app / product の振る舞いに閉じた設定です。

例:

- clock-specific alarm state
- logger filter
- app-local last screen

## Current Inventory

現時点でコード上に存在する NVS 保存先は以下です。

### System-Oriented

- namespace: `cyd_display`
  owner: `cyd_display`
  keys:
  - `config_v1`
  file: [components/platform/cyd_display/cyd_display.cpp](/Users/katsuandkoseto/dev/github/katsusand/esp32_project/components/platform/cyd_display/cyd_display.cpp:20)
  note: LCD 輝度。versioned blob

- namespace: `cyd_display`
  owner: `cyd_input`
  keys:
  - `touch_cal`
  file: [components/platform/cyd_input/cyd_input.c](/Users/katsuandkoseto/dev/github/katsusand/esp32_project/components/platform/cyd_input/cyd_input.c:100)
  note: タッチ補正 blob。transport は `xpt2046_softspi`、補正ロジックと保存 owner は `cyd_input`。現状は `cyd_display` namespace に同居している。versioned blob のみ受け付ける

- namespace: `app_shell`
  owner: `app_shell`
  keys:
  - `config_v1`
  file: [components/framework/app_shell/app_shell.c](/Users/katsuandkoseto/dev/github/katsusand/esp32_project/components/framework/app_shell/app_shell.c:14)
  note: home app への自動復帰 timeout 秒。versioned blob

### Feature-Oriented

- namespace: `time_sync`
  owner: `time_sync`
  keys:
  - `config_v1`
  file: [components/services/time_sync/time_sync.c](/Users/katsuandkoseto/dev/github/katsusand/esp32_project/components/services/time_sync/time_sync.c:60)
  note: NTP 更新間隔、POSIX timezone。versioned blob

- namespace: `radio_manager`
  owner: `radio_manager`
  keys:
  - `config_v1`
  file: [components/services/radio_manager/radio_manager.c](/Users/katsuandkoseto/dev/github/katsusand/esp32_project/components/services/radio_manager/radio_manager.c:32)
  note: radio idle release 秒。versioned blob

- namespace: `esp32_wifi_sta`
  owner: `wifi_profile_store`
  keys:
  - `profiles_v1`
  file: [components/services/wifi_profile_store/wifi_profile_store.c](/Users/katsuandkoseto/dev/github/katsusand/esp32_project/components/services/wifi_profile_store/wifi_profile_store.c:8)
  note: 保存済み Wi-Fi profile 一覧。versioned blob

- namespace: `app_scheduler`
  owner: `app_scheduler`
  keys:
  - `config_v1`
  file: [components/services/app_scheduler/app_scheduler.c](/Users/katsuandkoseto/dev/github/katsusand/esp32_project/components/services/app_scheduler/app_scheduler.c:26)
  note: scheduler entry 一式。現在は clock alarm の source of truth もここへ寄せる

## Current Gaps

現時点で system settings UI に見えていても、まだ NVS 保存されていない項目があります。

- sound volume

現在の `cyd_speaker` は runtime 変更 API を持ちますが、NVS 永続化は未実装です。

English supplement: The absence of persistence is intentional for now, but the namespace reservation should still be documented early.

## Reserved Future Namespaces

将来の衝突を避けるため、以下の namespace を予約方針として扱います。

### System

- `sys_display`
  brightness など display 共通設定
- `sys_input`
  touch calibration など input 共通設定
- `sys_shell`
  shell idle timeout など共通 UI 実行基盤設定
- `sys_sound`
  volume や mute など sound 共通設定

`sys_volume` ではなく `sys_sound` を優先します。将来 volume 以外の sound-related setting を追加しやすいためです。

English supplement: Prefer `sys_sound` over `sys_volume` so the namespace can grow beyond a single integer value.

### Feature

- `feat_time_sync`
- `feat_wifi_sta`
- `feat_radio`
- `feat_espnow`

### App

- `app_clock`
- `app_logger`

## Naming Direction

既存 namespace をすぐ一括 rename する必要はありませんが、新規追加や大きな整理の際は次の方向へ寄せます。

- `cyd_display` の `brightness` は将来的に `sys_display` へ寄せる
- `cyd_input` の `touch_cal` は `cyd_display` から分離し、`sys_input` へ寄せる
- `app_shell` の `idle_timeout` は `sys_shell` へ寄せる
- `time_sync` / `radio_manager` / `esp32_wifi_sta` は feature owner が明確なので、当面は現状維持でもよい

English supplement: Migration should happen when a component is already being edited for a meaningful change. Do not churn stable storage names without a migration reason.

## Alarm Direction

現在の clock alarm は独立 component を持たず、`app_scheduler` の `owner/tag` entry を source of truth として扱います。

今後の推奨方針は次の通りです。

- alarm 専用 component は増やさず、source of truth は `app_scheduler` の `owner/tag` entry へ置く
- clock-specific UI は `cyd_clock_settings_app` や main clock app から `app_scheduler` を直接操作する
- alarm sound、snooze、label のような将来追加が必要なら、薄い clock-app helper として app 側に置く

重要なのは、「alarm 機能を scheduler service の内部仕様へ埋め込む」のではなく、「alarm 専用 wrapper component を減らし、汎用 scheduler API を app 層から使う」ことです。

English supplement: The project now uses `app_scheduler` as the alarm source of truth without introducing a new alarm-specific service.

## Reset Policy

将来的に reset を段階化する場合、次の粒度を基本とします。

- system reset
  `sys_display`, `sys_input`, `sys_shell`, `sys_sound`
- feature reset
  `feat_time_sync`, `feat_wifi_sta`, `feat_radio`, `feat_espnow`
- app reset
  `app_clock`, `app_logger`
- full reset
  NVS 全消去

現状の `Initialize NVS` は full reset です。将来、system 設定を残して app/feature 設定だけ消す導線を追加したい場合は、この分類を基準にします。

現行 firmware が想定する構造体 version / blob size / 文字列終端と一致しない保存値を検出した場合は、既定値で暫定起動しつつ `Initialize NVS` を要求します。互換 migration を明示的に実装していない payload を、黙って読み続けたり自動上書きしたりしない方針です。

English supplement: Unsupported on-flash layouts trigger a forced initialize flow. When old data should survive, add explicit migration code instead of silently accepting unknown payloads.

## Implementation Guidance

新しい NVS 項目を追加するときは、以下を守ります。

1. まず ownership を `system` / `feature` / `app` のどれかで決める
2. namespace owner を決めてから key を追加する
3. app から直接ばらばらに `nvs_open()` するより、必要に応じて薄い helper を使う
4. blob を保存する場合は version を含める
5. migration を入れない方針なら、旧 payload は `nvs_health` で検出して `Initialize NVS` を要求する

English supplement: During active development, it is acceptable to drop obsolete layouts and force a clean initialize instead of carrying long-lived migration code.

## Notes

- sound volume persistence は未実装だが、将来 namespace は `sys_sound` を使う
- `app_scheduler` は reusable service として維持し、alarm-specific policy を内部へ持ち込まない
