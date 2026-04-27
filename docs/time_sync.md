# Time Sync

## Overview

`time_sync` は、Wi-Fi 接続後に SNTP/NTP でシステム時刻を同期するコンポーネントです。

内部 task は必要時に `wifi_manager` へ Wi-Fi 接続を要求し、`esp_netif_sntp` を使って同期を行います。同期後は `wifi_manager_disable()` で Wi-Fi を OFF に戻し、基準間隔に jitter を加えた周期で再同期します。

一度も同期に成功していない起動直後は、接続失敗時に Wi-Fi setup UI へ遷移できる通常要求を使います。一度でも同期に成功した後の再同期では、時計表示を奪わないように setup UI への遷移を抑止した接続要求を使います。

English supplement: After the first successful sync, background retries use a no-setup Wi-Fi request so failed resync attempts do not take over the display.

## Public API

利用するファイルでは、次のヘッダーを include します。

```c
#include "time_sync.h"
```

起動は `time_sync_start()` で行います。`CONFIG_TIME_SYNC_ENABLED` が無効な場合も `ESP_OK` を返します。有効で、すでに起動済みの場合も `ESP_OK` です。

```c
ESP_ERROR_CHECK(time_sync_start());
```

最後の同期状態を参照する場合は、以下の API を使います。

```c
time_sync_state_t state = time_sync_get_state();
if (time_sync_is_busy()) {
    /* Wi-Fi 接続待ち、または SNTP 同期待ちです。 */
}

time_t sync_time = 0;
if (time_sync_get_last_success_at(&sync_time)) {
    /* sync_time は最後に成功した同期時刻です。 */
}

esp_err_t status = ESP_OK;
if (time_sync_get_last_attempt_status(&status)) {
    /* status == ESP_OK なら最後の同期試行は成功です。 */
}
```

`time_sync_get_state()` は `STOPPED`、`IDLE`、`WAITING_WIFI`、`SYNCING`、`RETRY_WAIT` のいずれかを返します。`time_sync_is_busy()` は `WAITING_WIFI`、`SYNCING`、`RETRY_WAIT` の間だけ `true` を返します。

`time_sync_get_last_success_at()` は、まだ同期に成功していない場合 `false` を返します。`time_sync_get_last_attempt_status()` は、まだ同期を試行していない場合 `false` を返します。同期完了を直接待つ API や停止 API はありません。

English supplement: `time_sync_is_busy()` represents active work that should usually disable duplicate UI actions. `last_success_at` means the last successful synchronization time, while `last_attempt_status` means the result of the most recent synchronization attempt.

## Runtime Behavior

起動時に `CONFIG_TIME_SYNC_TIMEZONE` が空でなければ、`setenv("TZ", ...)` と `tzset()` を一度だけ実行します。

task の同期ループは以下です。

1. 最大 60 秒 Wi-Fi 接続を要求する。初回成功前は setup 許可、初回成功後は setup 抑止で要求する
2. Wi-Fi が利用可能なら `esp_netif_sntp_init()` で SNTP を開始する
3. `CONFIG_TIME_SYNC_WAIT_TIMEOUT_MS` だけ同期完了を待つ
4. 成功/失敗にかかわらず `esp_netif_sntp_deinit()` する
5. Wi-Fi 接続要求に成功していた場合は `wifi_manager_disable()` で Wi-Fi を OFF に戻す
6. 失敗時は `CONFIG_TIME_SYNC_RETRY_ATTEMPTS` 回まで短い間隔で再試行する
7. 通常周期に戻り、`CONFIG_TIME_SYNC_INTERVAL_MINUTES +/- CONFIG_TIME_SYNC_JITTER_MINUTES` の秒単位ランダム delay を待つ

English supplement: SNTP is initialized for each sync attempt and deinitialized immediately after the wait completes. The current design assumes no other concurrent Wi-Fi client is active when `time_sync` disables Wi-Fi.

## Retry and Jitter

通常の次回同期 delay は、基準間隔を秒へ変換し、jitter 範囲を秒単位でランダムに加減して決めます。jitter が 0 の場合は固定周期です。

失敗時 retry は通常周期とは別に、`CONFIG_TIME_SYNC_RETRY_DELAY_SECONDS` ごとに `CONFIG_TIME_SYNC_RETRY_ATTEMPTS` 回実行します。retry 上限に達した場合は警告ログを出し、通常周期へ戻ります。

English supplement: The jitter is symmetric around the base interval. If subtracting jitter would underflow, the delay is clamped to at least 1 second.

## Configuration

主な設定項目は `idf.py menuconfig` の `Time Sync` から変更できます。

- `CONFIG_TIME_SYNC_ENABLED`: NTP 時刻同期を有効にする
- `CONFIG_TIME_SYNC_TASK_STACK_SIZE`: time sync task の stack size
- `CONFIG_TIME_SYNC_TASK_PRIORITY`: time sync task の priority
- `CONFIG_TIME_SYNC_SERVER`: NTP server
- `CONFIG_TIME_SYNC_INTERVAL_MINUTES`: 通常同期の基準間隔
- `CONFIG_TIME_SYNC_JITTER_MINUTES`: 通常同期間隔へ加減する jitter 幅
- `CONFIG_TIME_SYNC_WAIT_TIMEOUT_MS`: 1回の SNTP 同期待ち timeout
- `CONFIG_TIME_SYNC_RETRY_DELAY_SECONDS`: 失敗時 retry の間隔
- `CONFIG_TIME_SYNC_RETRY_ATTEMPTS`: 失敗時 retry 回数
- `CONFIG_TIME_SYNC_TIMEZONE`: POSIX timezone 文字列

標準 timezone は `JST-9` です。UTC のまま扱う場合は空文字にします。

## Dependencies

`time_sync` は以下のコンポーネントに依存します。

- `esp_netif`
- `esp_hw_support`
- `wifi_manager`

`main/main.c` では `CONFIG_ESP32_WIFI_STA_AUTO_START` が有効な場合に `wifi_manager_start()` の後で `time_sync_start()` を呼びます。
