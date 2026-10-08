# CYD Wi-Fi Setup

## Overview

`cyd_wifi_setup` は、CYD の画面とタッチ入力を使って Wi-Fi credential を設定する foreground app / UI helper コンポーネントです。

AP スキャン一覧、パスワード入力キーボード、接続失敗ダイアログを表示します。credential の保存と STA 接続テストは `wifi_connection` に委譲します。現在は `app_shell` 上の `wifi setup app` として動作し、`clock app` などから切り替えて起動します。

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

設定済み credential での通常接続は `wifi_connection` の manager task が行います。`cyd_wifi_setup` は通常接続helperを公開せず、foreground UI に限定します。

この UI は STA を直接操作しません。setup の開始と終了、scan、接続テストはすべて `wifi_connection` への要求で、実際の処理は manager task 上で行われます。UI はその完了を待ちます。

English supplement: Saved-profile connection selection belongs to `wifi_connection`, not to the setup UI.

scan/password 入力 UI は session helper としても利用できます。

```c
cyd_wifi_setup_begin_scan_session();
ESP_ERROR_CHECK(cyd_wifi_setup_poll_scan_session(&event, &selected, &cancelled, &record));
```

English supplement: Session helpers are intended for shell-managed apps. They update the screen on the current display owner task and keep their own small transient UI state.

## Scan Flow

scan mode の画面遷移は以下です。

1. `wifi setup app` が scan session を開始する
2. 「探しています…」を表示する
3. `wifi_connection_setup_scan()` で manager task にスキャンを依頼し、完了を待つ
4. 見つかったネットワークを 1 ページ 5 件 (各 32px) ずつ表示する
5. 「再検索」が押されたら手動で再スキャンする
6. 「前へ」「次へ」が押されたらページを切り替える
7. 左上の「戻る」が押されたら遷移元 app へ戻る
8. ネットワークが選択されたら password session へ進む

スキャン中は「再検索」「前へ」「次へ」を無効表示にします。「前へ」「次へ」は移動先ページが存在しない場合も無効表示になります。見つからなかったときは「見つかりません」、スキャン自体が失敗したときは「検索できませんでした」とエラー名 (英語) を出します。

一覧に表示する情報は SSID と電波の強さです。強さは RSSI から「強い」(-60 dBm 以上)、「ふつう」(-72 dBm 以上)、「弱い」の 3 段階で表し、channel は出しません。SSID は 22 文字程度まで表示し、それより長いものは「…」で切ります。名前の無い SSID は「（名前なし）」と表示します。`wifi_connection_setup_scan()` が `esp32_wifi_sta_scan_record_t` の配列に結果をコピーします。

画面の組み立ては `cyd_wifi_setup_view.c` にあり、model (`cyd_wifi_setup_view_model_t`) だけから組みます。シミュレーターの `wifi_*` シーンと `test/host/test_wifi_setup_view.c` が全状態を確認します。

English supplement: The scan list is transient RAM state. Credentials are not persisted by selecting an SSID. Results stay stable until the user presses 再検索 (rescan). The top-left 戻る (back) action returns to the app captured from `enter(from_app)`. `cyd_wifi_setup_view_build()` calls no service.

## Password UI

password 入力画面は汎用frameworkコンポーネント `cyd_text_input` の `PASSWORD` modeを利用します。

見出しは「Wi-Fi のパスワード」、文脈の行に SSID を出します。キーボード画面の構成とボタンは [CYD Text Input](cyd_text_input.md#screen) を参照してください。「文字を表示」「文字を隠す」で伏せ字を切り替え、「戻る」で一覧へ戻り、「保存」で接続テストを行います。

「保存」は `wifi_connection_connect_and_save()` を通じて、選択 SSID と入力 password の接続テストを実行します。成功した場合だけ profile を保存します。

テスト中は「接続しています…」、成功すると「接続しました」を表示します。失敗すると「接続できませんでした」と、失敗理由 (`esp32_wifi_sta_failure_reason_t`) に応じた一言を出し、「OK」で一覧へ戻ります。

| 失敗理由 | 表示 |
|---|---|
| `AUTH` | パスワードが違うかもしれません |
| `NO_AP_IN_RANGE` | ネットワークが見つかりません |
| `TIMEOUT` | 時間内に接続できませんでした |
| それ以外 | 接続に失敗しました |

エラー名 (`esp_err_to_name()`、英語) も小さく添えます。

接続失敗時は同じSTA状態で `esp_wifi_connect()` を即時反復せず、STA停止、設定再適用、待機、STA開始を1回のfresh retryとして実行します。最大再試行回数は `CONFIG_ESP32_WIFI_STA_MAX_RETRY`、試行間隔は `CONFIG_ESP32_WIFI_STA_RETRY_DELAY_MS` です。この経路はpassword保存時と保存済みSSIDへの通常接続で共通です。

ただし password 保存時の接続テストは、認証失敗（たいていパスワード違い）が `CONFIG_WIFI_CONNECTION_SETUP_AUTH_FAILURE_LIMIT`（既定 2）回に達した時点で打ち切ります。1回の試行は handshake のタイムアウトで約4秒かかるため、以前はパスワードを間違えると6回分、約25秒待ってから失敗画面が出ていました。保存済み profile での自動接続は、弱い電波での handshake タイムアウトも同じ理由に分類されるため、打ち切らずに全回数試します。

English supplement: the setup connection test gives up after a few authentication failures because a wrong password never recovers. Automatic connection with saved profiles keeps every attempt, since a weak signal can produce the same failure reason.

English supplement: A retry is a fresh stop/reconfigure/start cycle run by the wifi_connection manager task. The disconnect event handler never reconnects on its own.

English supplement: 保存 (save) is gated by a live connection test. Failed credentials are not persisted.

Wi-Fi setupがactiveな間は、アプリ自身の状態を `app_shell` のidle-return抑止callbackで返します。特定の遷移元アプリIDには依存しません。

## Touch Confirmation

このコンポーネントは touch の `PRESS` と `RELEASE` を照合してクリックを確定します。`PRESS` 時と `RELEASE` 時の action id が一致し、途中で `LONG_PRESS` が出ていない場合だけ操作を実行します。

English supplement: Touch intent is confirmed on release. A long press suppresses click confirmation.

## Configuration

`cyd_wifi_setup` 専用の Kconfig はありません。主に以下の設定を参照します。

- `CONFIG_ESP32_WIFI_STA_SCAN_LIST_SIZE`: scan list の保持件数。標準は `30`
- `CONFIG_ESP32_WIFI_STA_MAX_RETRY`: 接続テストの最大リトライ回数
- `CONFIG_ESP32_WIFI_STA_CONNECT_TIMEOUT_MS`: 接続テストの待ち時間
- `CONFIG_ESP32_WIFI_STA_RETRY_DELAY_MS`: fresh retry間の待ち時間
- `CONFIG_ESP32_WIFI_STA_SAE_H2E_IDENTIFIER`: 接続テスト時の SAE H2E identifier

内部の password 最大長は `64` 文字です。画面上のテキストは `CYD_DISPLAY_TEXT_MAX_LEN` に収まるよう短く表示されます。

## Dependencies

`cyd_wifi_setup` は以下のコンポーネントに依存します。

- `app_shell`
- `cyd_display`
- `cyd_input`
- `cyd_text_input`
- `cyd_ui`
- `esp32_wifi_sta`
- `time_sync`
- `wifi_connection`
- `wifi_connection`
