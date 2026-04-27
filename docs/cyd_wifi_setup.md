# CYD Wi-Fi Setup

## Overview

`cyd_wifi_setup` は、CYD の画面とタッチ入力を使って Wi-Fi credential を設定する foreground app / UI helper コンポーネントです。

AP スキャン一覧、パスワード入力キーボード、接続失敗ダイアログを表示します。credential の保存と STA 接続テストは `esp32_wifi_sta` に委譲します。現在は `app_shell` 上の `wifi setup app` として動作し、`clock app` などから切り替えて起動します。

English supplement: This component no longer exposes a blocking full-screen provisioning loop. It is a shell-managed foreground app plus reusable scan/password session helpers.

## Public API

利用するファイルでは、次のヘッダーを include します。

```c
#include "cyd_wifi_setup.h"
```

foreground app として使う場合は `cyd_wifi_setup_get_app()` を使います。

```c
ESP_ERROR_CHECK(app_shell_switch_to(cyd_wifi_setup_get_app()));
```

戻り先アプリは通常、`app_shell` が `enter()` に渡す遷移元 app を使います。

互換用に `cyd_wifi_setup_set_return_app()` も残しています。特殊な戻り先を明示したい場合のみ使います。

設定済み credential で接続する場合は `cyd_wifi_setup_connect_configured()` を使います。

```c
esp_err_t err = cyd_wifi_setup_connect_configured(pdMS_TO_TICKS(15000), NULL);
```

`wait_ticks` が 0 より大きい場合は `esp32_wifi_sta_wait_connected()` まで実行します。第 2 引数には失敗理由の出力先を渡せます。

この関数は通常接続用 helper であり、接続中画面は表示しません。接続状態の表示は `cyd_clock_app` などの foreground app が担当します。

scan/password 入力 UI は session helper としても利用できます。

```c
cyd_wifi_setup_begin_scan_session();
ESP_ERROR_CHECK(cyd_wifi_setup_poll_scan_session(&event, &selected, &cancelled, &record));
```

English supplement: Session helpers are intended for shell-managed apps. They update the screen on the current display owner task and keep their own small transient UI state.

## Scan Flow

scan mode の画面遷移は以下です。

1. `wifi setup app` が scan session を開始する
2. `Scanning...` を表示する
3. `esp32_wifi_sta_enter_scan_mode()` で同期スキャンする
4. `Wi-Fi SSID list` に AP を最大 `10` 件ずつページ表示する
5. `SCAN` が押されたら手動で再スキャンする
6. `<` / `>` が押されたらページを切り替える
7. 左上の `<<` が押されたら遷移元 app へ戻る
8. SSID が選択されたら password session へ進む

スキャン中は `SCAN` / `<` / `>` を無効表示にします。`<` / `>` は移動先ページが存在しない場合も無効表示になります。

一覧に表示する情報は SSID、RSSI、channel です。内部では `esp32_wifi_sta_get_scan_records()` から `esp32_wifi_sta_scan_record_t` を読み出します。

English supplement: The scan list is transient RAM state. Credentials are not persisted by selecting an SSID. Results stay stable until the user presses SCAN. The scan screen top-left `<<` action returns to the app captured from `enter(from_app)`.

## Password UI

password 入力画面は `cyd_display_screen_t` のボタンウィジェットで構成されます。

キーボードページは以下です。

- lower: `qwertyuiop` などの小文字ページ
- upper: `QWERTYUIOP` などの大文字ページ
- symbol: 数字と記号ページ
- symbol extra: 追加記号ページ

操作ボタンは以下です。

- `SHOW PASSWORD`: password 表示/マスクを切り替える
- `ABC` / `abc`: 大文字/小文字を切り替える
- `123` / `()[]`: 記号ページを切り替える
- `DEL`: 末尾1文字を削除する
- `SPACE`: 空白を追加する
- `CANCEL`: scan list へ戻る
- `SAVE`: 接続テストを行い、成功した場合だけ NVS に保存する

`SAVE` は `wifi_test_connect_and_save()` を通じて、選択 SSID と入力 password で `esp32_wifi_sta_init_with_config()`、`esp32_wifi_sta_start()`、`esp32_wifi_sta_wait_connected()` を実行します。成功後に `wifi_profile_store_record_success()` 系の保存へつながる credential 更新を行います。

English supplement: SAVE is gated by a live connection test. Failed credentials are not persisted.

## Touch Confirmation

このコンポーネントは touch の `PRESS` と `RELEASE` を照合してクリックを確定します。`PRESS` 時と `RELEASE` 時の action id が一致し、途中で `LONG_PRESS` が出ていない場合だけ操作を実行します。

English supplement: Touch intent is confirmed on release. A long press suppresses click confirmation.

## Configuration

`cyd_wifi_setup` 専用の Kconfig はありません。主に以下の設定を参照します。

- `CONFIG_ESP32_WIFI_STA_SCAN_LIST_SIZE`: scan list の保持件数。標準は `30`
- `CONFIG_ESP32_WIFI_STA_MAX_RETRY`: 接続テストの最大リトライ回数
- `CONFIG_ESP32_WIFI_STA_CONNECT_TIMEOUT_MS`: 接続テストの待ち時間
- `CONFIG_ESP32_WIFI_STA_SAE_H2E_IDENTIFIER`: 接続テスト時の SAE H2E identifier

内部の password 最大長は `64` 文字です。画面上のテキストは `CYD_DISPLAY_TEXT_MAX_LEN` に収まるよう短く表示されます。

## Dependencies

`cyd_wifi_setup` は以下のコンポーネントに依存します。

- `app_shell`
- `cyd_display`
- `cyd_input`
- `cyd_ui`
- `esp32_wifi_sta`
- `time_sync`
- `wifi_manager`
