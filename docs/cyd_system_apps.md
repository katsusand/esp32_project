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
ESP_ERROR_CHECK(app_shell_switch_to(cyd_info_app_get_app()));
```

`settings app` も同様です。

```c
ESP_ERROR_CHECK(app_shell_switch_to(cyd_settings_app_get_app()));
```

English supplement: Return apps come from the `from_app` pointer passed to `enter()`, avoiding compile-time dependency from system apps back to the clock app.

## Info App

`info app` は参照用の情報画面です。

- app 名 / version
- ESP-IDF version
- chip revision / core count
- free heap
- Wi-Fi manager state

左上の `<<` ボタンで、`enter()` の `from_app` として受け取った return app へ戻ります。

## Settings App

`settings app` は設定入口です。

- `GENERAL` page
  `LcdBrightness`: LCD バックライトの明るさを変更する
  `Volume`: スピーカー音量を変更する
  `TimeSyncInterval`: NTP 同期間隔を分単位で変更する
- `NETWORK` page
  現在の Wi-Fi 状態表示
  `Stored SSIDs`: 保存済みSSID一覧、優先化、削除
  `Wi-Fi Setup`: `wifi_setup app` へ切り替える
- `<<`: `enter()` の `from_app` として受け取った return app へ戻る

ページ切り替えは画面下部の `<` / `>` ボタンで行います。これにより settings 画面は、今後 `Volume` や `TimeSyncInterval` などを追加しやすい構成になっています。

`LcdBrightness` は `100 / 75 / 50 / 40 / 30 / 25 / 20 / 15 / 10 / 5` の 10 段階です。`Volume` は `100 / 70 / 50 / 35 / 25 / 18 / 12 / 8 / 5` の 9 段階です。`TimeSyncInterval` は 1 から 1440 分の範囲で、現在値に応じて `1 / 5 / 30 / 60 / 180` 分ステップで増減します。これらは `-` / `+` ボタンで変更すると、その場で反映されます。保存は `settings app` を離れるタイミングで行われます。

`Stored SSIDs` は `NETWORK` page から入るサブ画面です。保存済みSSIDを優先順で表示し、選択したSSIDを最優先にしたり、削除確認を経て削除したりできます。

`Wi-Fi Setup` へ入ると、`wifi_setup app` は `from_app` として `settings app` を受け取ります。これにより、Wi-Fi 設定完了後は settings 画面へ戻ります。

settings 画面が `wifi_setup app` から戻ってきた場合は、元の return app を保持します。これにより `clock -> settings -> wifi_setup -> settings -> <<` は `clock` へ戻ります。

English supplement: Settings is a menu app, not persistent configuration storage. Add storage-backed settings in dedicated components when values need to survive reboot.
