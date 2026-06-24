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
       -> time_tick / scheduler / alarm
       -> Wi-Fi / radio / time sync
       -> app_shell(cyd_clock_app)
```

`system_boot` が検出した起動ショートカットは `system_boot_result_t` で受け取り、時計製品ではWi-Fi setup要求として解釈します。

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
- 汎用platform初期化の詳細

English supplement: Keep policy and wiring here; keep behavior inside the selected app and service components.
