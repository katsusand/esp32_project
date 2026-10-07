# CYD Clock Composition

## Overview

`cyd_clock_composition` は、CYD時計製品で使用するapp・service・platform機能を選択し、起動順と接続関係を定義するcomposition componentです。

English supplement: This component is the product composition root. It selects and wires features but does not implement UI behavior or service internals.

## Startup Flow

`system_boot_start()` で共通基盤を初期化した後、時計製品が必要とするserviceを順番に起動し、最後にclock appをhome appとして`app_shell`へ渡します。

```text
main
  -> cyd_clock_composition
       -> system_boot (NVS / CYD platform / input calibration)
       -> sd_card_status (SD card: its own task mounts it and starts the error log)
       -> time_tick / app_scheduler (service only)
       -> Wi-Fi / radio / time sync
       -> register apps (clock + its settings screen + its alarms)
       -> app_shell(cyd_clock_app)
```

## App Registration

composition が登録するのは app 単位です。時計の場合は `cyd_clock_app_register()` を 1 回呼ぶだけで、設定画面 (`cyd_clock_settings_app`) とアラーム (`cyd_clock_alarm`) もそこから一緒にインストールされます。

composition 自身はアラームのコードを持ちません。以前はアラームの既定値、存在確認、発火時の handler がここにありましたが、アラームは時計の機能なので時計側へ移しました。composition が起動するのは共有 service の `app_scheduler` だけで、その上に何を載せるかは各 app が決めます。

時計を別のメインアプリに差し替えるときは、`cyd_clock_app_register()` の呼び出しを外せばアラームと設定画面も一緒に外れます。前の時計が保存したアラームは app scope なので、`Clear App Data` で消せる残骸として扱えます ([cyd_clock_alarm.md](cyd_clock_alarm.md))。

登録は `app_scheduler_init()` の後に行います。アラームの登録に scheduler が必要なためです。登録の失敗は optional service と同じく error log に記録して起動を続けます。時計はアラームなしでも動作します。

English contract: the composition registers apps, not app features. The clock's settings screen and alarms come with `cyd_clock_app_register()`; the composition only starts the shared `app_scheduler` service and must not seed or handle any app's schedules.

`system_boot` が検出した起動ショートカット (`system_boot_result_t.setup_shortcut_requested`) は、時計製品では **settings app を初期 app にする** 要求として解釈します。画面を押したまま電源を入れると時計ではなく settings が開きます。

以前はここで Wi-Fi setup ウィザードを予約していました。settings に変えたのは、settings が Wi-Fi setup・タッチ補正・Initialize NVS のいずれにも届く単一の行き先であり、「Wi-Fi profile が存在するかどうか」で意味が変わる shortcut より予測しやすいためです。またウィザードの予約は Wi-Fi build feature の `#if` の内側にあったため、Wi-Fi を無効にしたビルドでは shortcut が何もしていませんでした。

NVS が不正な場合は、この判定より前に settings と Initialize NVS 確認画面が選ばれており、そちらが優先されます。

## Feature Switches

`APP_WIFI_STA=0` で再構成したビルドでは、composition は Wi-Fi startup 群を起動しません。対象は `wifi_connection`、`radio_manager`、`time_sync`、`status_indicator` の起動と、Wi-Fi setup 中の home return guard です。

Wi-Fi 無効ビルドでも `cyd_clock_app` は同じ API を呼べますが、下位 service は stub 実装として `ESP_ERR_NOT_SUPPORTED` または安全な空状態を返します。

English supplement: Product composition owns feature startup. Service stubs keep app-level dependencies stable when a build removes the Wi-Fi feature.

## Responsibility

- 時計製品で使用する機能の選択
- serviceとappの初期化順の定義
- home appの選択
- 製品固有の依存関係の接続

次の責務は持ちません。

- 時計画面の描画や入力処理
- Wi-Fi、時刻同期、アラームなどの内部実装
- app が scheduler に載せる schedule の既定値や handler (時計のアラームは `cyd_clock_alarm` が持つ)
- 汎用platform初期化の詳細

English supplement: Keep policy and wiring here; keep behavior inside the selected app and service components.
