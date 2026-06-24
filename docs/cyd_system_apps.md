# CYD System Apps

## Overview

`cyd_system_apps` は、`app_shell` 上で動く小さなシステム系 foreground app をまとめるコンポーネントです。

現在は以下の app を提供します。

- `info`: firmware / IDF / chip / heap / Wi-Fi 状態を表示する
- `settings`: 設定系画面への入口を表示する

English supplement: These apps are intentionally lightweight shell apps. They should not own background services; they only present UI and switch to domain apps such as `wifi_setup`.

## Public API

利用するファイルでは、次のヘッダーを include します。

```c
#include "cyd_system_apps.h"
```

`info app` へは `app_shell_switch_to()` で遷移します。

```c
ESP_ERROR_CHECK(app_shell_switch_to(system_info_app_get_app()));
```

`settings app` も同様です。

```c
ESP_ERROR_CHECK(app_shell_switch_to(system_settings_app_get_app()));
```

English supplement: Return apps come from the `from_app` pointer passed to `enter()`, avoiding compile-time dependency from system apps back to the clock app.

保存済みSSID一覧、touch calibration消去確認、NVS消去確認は、次のAPIで次回のsettings遷移先として直接指定できます。

```c
system_settings_open_stored_ssids();
ESP_ERROR_CHECK(app_shell_switch_to(system_settings_app_get_app()));
```

確認画面を開く場合は、1行目を `system_settings_open_clear_touch_calib_confirm()` または
`system_settings_open_clear_nvs_confirm()` に置き換えます。

指定は次回の `settings app` の `enter()` で一度だけ消費されます。直接開いた画面の戻る操作は、通常のsettings pageではなく遷移元アプリへ戻ります。

English supplement: Direct-view selection is one-shot and thread-safe; callers still request the shell transition explicitly.

## Info App

`info app` は参照用の情報画面です。

- app 名 / version
- ESP-IDF version
- chip revision / core count
- free heap
- Wi-Fi manager state / active users / last user
- Wi-Fi connected duration / max duration / warning / last failure

左上の `<<` ボタンで、`enter()` の `from_app` として受け取った return app へ戻ります。

## Settings App

`settings app` は設定入口です。

- `GENERAL` page
  `LcdBrightness`: LCD バックライトの明るさを変更する
  `Volume`: スピーカー音量を変更する
  optional extension button: app 固有設定 app への導線を表示できる
- `TIME` page
  `TimeSyncInterval`: NTP 同期間隔を分単位で変更する
  `Timezone`: POSIX timezone 設定をプリセットから切り替える
  `SYNC NOW`: その場で同期を要求する
  `Touch Calib`: タッチ補正画面を任意に実行する
- `NETWORK` page
  現在の Wi-Fi 状態表示
  `Stored SSIDs`: 保存済みSSID一覧、優先化、削除
  `Wi-Fi Setup`: `wifi_setup app` へ切り替える
- `NVS` page
  `Clear Touch Calib`: 保存済みタッチ補正だけ消す
  `Initialize NVS`: 保存済み NVS データを全消去して再起動する
- `<<`: `enter()` の `from_app` として受け取った return app へ戻る

ページ切り替えは画面下部の `<` / `>` ボタンで行います。これにより settings 画面は、system-level setting を page 単位で増やしやすい構成になっています。

`LcdBrightness` は `100 / 75 / 50 / 40 / 30 / 25 / 20 / 15 / 10 / 5` の 10 段階です。`Volume` は `100 / 70 / 50 / 35 / 25 / 18 / 12 / 8 / 5` の 9 段階です。`TimeSyncInterval` は 1 から 1440 分の範囲で、現在値に応じて `1 / 5 / 30 / 60 / 180` 分ステップで増減します。`Timezone` は内蔵プリセットから切り替えます。これらは `-` / `+` ボタンで変更すると、その場で反映されます。`SYNC NOW` は `time_sync` に即時同期要求を送り、進行状況は settings 画面上に反映されます。`Touch Calib` は manual なタッチ補正画面を起動し、完了後に settings へ戻ります。保存は `settings app` を離れるタイミングで行われます。

`Stored SSIDs` は `NETWORK` page から入るサブ画面です。保存済みSSIDを優先順で表示し、選択したSSIDを最優先にしたり、削除確認を経て削除したりできます。

`NVS` page の `Clear Touch Calib` は、`cyd_input` が保存しているタッチ補正だけを削除します。Wi-Fi profile や他の設定値には触れません。`Initialize NVS` は確認画面を経て `nvs_flash_erase()` を実行し、保存済み Wi-Fi profile や各種設定値も含めて初期化したうえで再起動します。

NVS blob の version / size / 文字列終端などが現在 firmware の想定フォーマットと一致しない場合は、起動時に warning 付きの `Initialize NVS` 画面へ強制遷移します。この場合、通常の clock home には入らず、`Initialize` 実行後の再起動が必要です。

English supplement: Structurally incompatible persistent data now routes the product into a forced initialize flow instead of silently trusting or rewriting the broken payload.

`Wi-Fi Setup` へ入ると、`wifi_setup app` は `from_app` として `settings app` を受け取ります。これにより、Wi-Fi 設定完了後は settings 画面へ戻ります。

app 固有設定がある場合は、`system_settings_set_extension()` で `label + app_shell_app_t` を差し込めます。現在の時計アプリでは `Clock Settings` への導線がこれで追加されます。

時計固有の alarm 設定と scheduler 診断表示は `Clock Settings` 側にあります。`cyd_system_apps` は `app_scheduler` に依存しません。

settings 画面が `wifi_setup app` から戻ってきた場合は、元の return app を保持します。これにより `clock -> settings -> wifi_setup -> settings -> <<` は `clock` へ戻ります。

English supplement: Settings is a menu app, not persistent configuration storage. Add storage-backed settings in dedicated components when values need to survive reboot.

## Input Handling

`settings app` の touch handler には、意図的に 2 系統あります。

1. 通常ボタン経路
   `cyd_system_apps_touch_confirmed_action()` が使われます。
   これは `PRESS` 時に候補 action を記録し、`RELEASE` 時に同じボタン上で離された場合だけ確定します。
   `<<`、`Wi-Fi Setup`、`Stored SSIDs`、ページ移動 `<` / `>` のような普通の button はこの経路です。

2. ステッパー経路
   `cyd_settings_touch_stepper_action()` が使われます。
   これは `PRESS` と `REPEAT` をそのまま action として返します。
   `-` / `+` の長押し連続変更を成立させるため、`RELEASE` を待ちません。

実装上の入口は `cyd_settings_app_step()` です。
最初にステッパー経路を評価し、該当しなければ通常ボタン経路へ進みます。

English supplement: Stepper buttons are handled on `PRESS`/`REPEAT`, while normal buttons are handled on confirmed `RELEASE`. They are not interchangeable.

### Maintenance Rule

`settings` に新しい `-` / `+` ステッパー項目を追加するときは、次の 3 箇所を必ずセットで更新します。

1. `cyd_settings_is_stepper_action()`
   新しい action id をステッパー扱いとして登録する
2. `cyd_settings_touch_stepper_action()`
   そのページでステッパー経路を有効にする
3. `cyd_settings_app_step()`
   ステッパー経路で呼ぶ page-specific handler へ配線する

今回の不具合は 3 が漏れていたため発生しました。
見た目上は `-` / `+` が描画されていても、handler 配線が抜けると反応しません。

English supplement: If a control should auto-repeat while held, route it through the stepper path and explicitly wire its handler in `cyd_settings_app_step()`.
