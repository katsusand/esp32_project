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

- 現在の Wi-Fi 状態表示
- `Wi-Fi Setup`: `wifi_setup app` へ切り替える
- `<<`: `enter()` の `from_app` として受け取った return app へ戻る

`Wi-Fi Setup` へ入ると、`wifi_setup app` は `from_app` として `settings app` を受け取ります。これにより、Wi-Fi 設定完了後は settings 画面へ戻ります。

settings 画面が `wifi_setup app` から戻ってきた場合は、元の return app を保持します。これにより `clock -> settings -> wifi_setup -> settings -> <<` は `clock` へ戻ります。

English supplement: Settings is a menu app, not persistent configuration storage. Add storage-backed settings in dedicated components when values need to survive reboot.
