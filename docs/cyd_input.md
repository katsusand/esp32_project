# CYD Input Driver

## Overview

`cyd_input` は、CYD のタッチ入力と基板上の簡易 GPIO ボタン入力をアプリから扱いやすくするための入力コンポーネントです。

主な対象は XPT2046 タッチコントローラーです。`cyd_display` から取得したタッチ座標を周期的に読み取り、押下、離上、クリック、長押しのイベントへ変換します。

GPIO0 の BOOT ボタンも `CYD_INPUT_EVENT_GPIO_BUTTON` として扱えます。BOOT ボタンは ESP32 の UART download mode に入るための strapping pin でもあるため、リセット中または起動時に押し続けると通常のアプリ起動ではなくフラッシュ用モードへ入ります。アプリ起動後は active-low の通常入力として利用できます。

アプリ側は、現在のタッチ状態を読むか、イベントキューから入力イベントを受け取ります。タッチのポーリング、デバウンス、クリック判定、長押し判定は `cyd_input` の内部タスクが担当します。

English supplement: This component owns the input event queue and input polling task. Application code should consume events through `cyd_input_read_event()` instead of polling the touch controller or BOOT GPIO directly.

## Public API

利用するファイルでは、次のヘッダーを include します。

```c
#include "cyd_input.h"
```

初期化は、通常 `cyd_display_init()` の後に一度だけ呼びます。

```c
ESP_ERROR_CHECK(cyd_input_init());
```

現在のタッチ状態を読む場合は `cyd_input_get_touch_state()` を使います。

```c
cyd_input_touch_state_t state;
ESP_ERROR_CHECK(cyd_input_get_touch_state(&state));

if (state.pressed) {
    printf("touch x=%d y=%d\n", state.x, state.y);
}
```

タッチ IRQ GPIO の現在レベルを読む場合は `cyd_input_get_touch_irq_level()` を使います。`CONFIG_CYD_TOUCH_PIN_INT < 0` の場合は `ESP_ERR_NOT_SUPPORTED` を返します。

```c
int level = 1;
ESP_ERROR_CHECK(cyd_input_get_touch_irq_level(&level));
```

入力イベントを待つ場合は `cyd_input_read_event()` を使います。

```c
cyd_input_event_t event;
esp_err_t err = cyd_input_read_event(&event, pdMS_TO_TICKS(100));

if (err == ESP_OK && event.type == CYD_INPUT_EVENT_TOUCH) {
    printf("touch action=%d x=%d y=%d\n",
           event.data.touch.action,
           event.data.touch.x,
           event.data.touch.y);
}

if (err == ESP_OK &&
    event.type == CYD_INPUT_EVENT_GPIO_BUTTON &&
    event.source_id == CYD_INPUT_SOURCE_BOOT_BUTTON) {
    printf("boot button action=%d pressed=%d hold_seconds=%u\n",
           event.data.button.action,
           event.data.button.pressed,
           event.data.button.hold_seconds);
}
```

画面更新や重い処理のあとに、処理中へ積まれた入力を捨てたい場合は `cyd_input_discard_pending_events()` を使います。

```c
ESP_ERROR_CHECK(cyd_input_discard_pending_events());
```

English supplement: Use this only at intentional interaction boundaries, such as after a blocking scan, where queued touches would represent stale UI state.

モード画面のボタンにタッチしているかを調べる場合は `cyd_input_get_mode_button_touch()` を使います。

```c
size_t button_index = 0;
bool pressed = false;

ESP_ERROR_CHECK(cyd_input_get_mode_button_touch(button_count, &button_index, &pressed));
if (pressed) {
    printf("mode button %u pressed\n", (unsigned)button_index);
}
```

タッチ補正を手動で実行する場合は `cyd_input_run_touch_calibration()` を使います。

```c
ESP_ERROR_CHECK(cyd_input_run_touch_calibration());
```

保存済みのタッチ補正を消す場合は `cyd_input_clear_touch_calibration()` を使います。

```c
ESP_ERROR_CHECK(cyd_input_clear_touch_calibration());
```

English supplement: `cyd_input_init()` depends on display touch access. Initialize `cyd_display` before `cyd_input`.

## Event Model

入力イベントは `cyd_input_event_t` として表現されます。

```c
typedef enum {
    CYD_INPUT_EVENT_TOUCH = 0,
    CYD_INPUT_EVENT_GPIO_BUTTON,
    CYD_INPUT_EVENT_ROTARY_ENCODER,
} cyd_input_event_type_t;
```

現在の実装で生成されるのは、`CYD_INPUT_EVENT_TOUCH` と `CYD_INPUT_EVENT_GPIO_BUTTON` です。ロータリーエンコーダー用の型は、今後の拡張用に用意されています。

イベントの入力元は `source_id` で判別します。

```c
typedef enum {
    CYD_INPUT_SOURCE_TOUCH = 0,
    CYD_INPUT_SOURCE_BOOT_BUTTON = 1,
} cyd_input_source_id_t;
```

BOOT ボタンイベントでは `event.type == CYD_INPUT_EVENT_GPIO_BUTTON`、`event.source_id == CYD_INPUT_SOURCE_BOOT_BUTTON` になります。

GPIO ボタンイベントのアクションは以下です。

```c
typedef enum {
    CYD_INPUT_BUTTON_ACTION_PRESS = 0,
    CYD_INPUT_BUTTON_ACTION_RELEASE,
    CYD_INPUT_BUTTON_ACTION_CLICK,
    CYD_INPUT_BUTTON_ACTION_DOUBLE_CLICK,
    CYD_INPUT_BUTTON_ACTION_LONG_PRESS,
} cyd_input_button_action_t;
```

各アクションの意味は以下です。

- `PRESS`: ボタンが安定して押下状態になった
- `RELEASE`: ボタンが安定して離上状態になった
- `CLICK`: 短押しが確定した
- `DOUBLE_CLICK`: 短時間に 2 回の短押しが確定した
- `LONG_PRESS`: 押下継続中の長押し通知

`event.data.button.pressed` は現在の押下状態を示します。`LONG_PRESS` では `event.data.button.hold_seconds` に押下継続秒数が 1 から `CONFIG_CYD_BOOT_BUTTON_LONG_PRESS_MAX_SECONDS` まで入ります。標準設定の上限は 15 秒です。上限到達後は、押し続けても追加の `LONG_PRESS` は出ません。

`CLICK` は `CONFIG_CYD_BOOT_BUTTON_DOUBLE_CLICK_TIMEOUT_MS` の間だけ遅延して確定します。この待ち時間内に 2 回目の短押しが完了した場合、単発 `CLICK` の代わりに `DOUBLE_CLICK` が即時に発行されます。`LONG_PRESS` が発生した押下シーケンスは `CLICK` / `DOUBLE_CLICK` にはなりません。

English supplement: GPIO button long-press events are emitted once per second while held. `hold_seconds` is capped so application code can use deterministic thresholds such as 3, 5, 10, or 15 seconds. Single click is delayed by the double-click timeout; double click suppresses that pending single click.

タッチイベントのアクションは以下です。

```c
typedef enum {
    CYD_INPUT_TOUCH_ACTION_PRESS = 0,
    CYD_INPUT_TOUCH_ACTION_RELEASE,
    CYD_INPUT_TOUCH_ACTION_LONG_PRESS,
} cyd_input_touch_action_t;
```

各アクションの意味は以下です。

- `PRESS`: タッチが安定して押下状態になった
- `RELEASE`: タッチが安定して離上状態になった
- `LONG_PRESS`: 長押ししきい値を超えた。以後、設定された間隔で繰り返し通知される

`LONG_PRESS` では `hold_ticks` に長押し段階が入ります。最初の長押しで `1`、以後の繰り返し通知で増加します。

`cyd_input` はタッチの `CLICK` イベントを生成しません。ボタンやアクションの確定は、UI 側で `PRESS` 時の hit-test 対象と `RELEASE` 時の hit-test 対象が一致するかを見て判断します。押した後に指やスタイラスを対象外へ動かして離した場合は、UI 側でキャンセルとして扱えます。

English supplement: `cyd_input` reports touch facts, not widget intent. UI code should confirm clicks by matching the pressed widget/action against the release location.

## Queue Model

`cyd_input` は内部に FreeRTOS キューを持ちます。キューの長さは `CONFIG_CYD_INPUT_EVENT_QUEUE_LENGTH` で決まります。

入力タスクはタッチ状態の変化を検出すると、イベントをキューへ送ります。アプリ側は `cyd_input_read_event()` でそのイベントを受け取ります。

イベントキューが満杯の場合、最も古いイベントを1つ捨ててから新しいイベントを積み直します。それでも送れない場合は、最新イベントを破棄してログに警告を出します。入力イベントを取りこぼしたくないアプリでは、十分な頻度で `cyd_input_read_event()` を呼んでください。

`cyd_input_discard_pending_events()` は、呼び出し時点でキューに残っているイベントをすべて破棄します。現在押されているタッチ状態そのものは変更しません。

English supplement: Event enqueue is non-blocking and latest-event oriented. The queue first drops the oldest item to preserve recent user intent; if the second send still fails, the newest event is dropped.

## Touch State

`cyd_input_get_touch_state()` は、最後に安定判定されたタッチ状態を返します。

```c
typedef struct {
    bool pressed;
    int16_t x;
    int16_t y;
    TickType_t tick;
} cyd_input_touch_state_t;
```

各フィールドの意味は以下です。

- `pressed`: 現在タッチされている場合は `true`
- `x`: タッチ中の X 座標。タッチしていない場合は `0`
- `y`: タッチ中の Y 座標。タッチしていない場合は `0`
- `tick`: 状態を更新した FreeRTOS tick

タッチ状態は mutex で保護されます。アプリ側は構造体を受け取った後、そのコピーを自由に使えます。

English supplement: `cyd_input_get_touch_state()` returns a snapshot protected by the component mutex. The returned struct is not a live reference.

## BOOT Button

BOOT ボタンは、標準設定では GPIO0 の active-low 入力として扱います。`CONFIG_CYD_BOOT_BUTTON_ENABLED` が有効な場合、`cyd_input` の内部タスクが GPIO0 を周期的にサンプリングし、安定した押下/離上の変化と長押し進捗をイベントキューへ送ります。

ESP32 の GPIO0 は strapping pin です。通常利用時の注意点は以下です。

- アプリ起動後に押す: GPIO ボタンとして利用可能
- リセット中または電源投入中に押し続ける: UART download mode に入り、通常アプリは起動しない
- 外部回路を追加する場合: 起動時の GPIO0 レベルを強く LOW に固定しない

English supplement: Treat BOOT as a user button only after firmware has started. Do not design application behavior that requires BOOT to be held during reset or power-on.

## Calibration

タッチ補正は `cyd_display` のタッチ補正機能を使って実行されます。`CONFIG_CYD_TOUCH_USE_NVS_CALIBRATION` が有効な場合、補正値は NVS に保存されます。

保存先は以下です。

- NVS namespace: `cyd_display`
- NVS key: `touch_cal`

`CONFIG_CYD_TOUCH_RUN_CALIBRATION_ON_BOOT` が有効で、保存済み補正値がない場合は、起動時に補正フローを実行します。

保存済み補正値は versioned blob として保存します。blob サイズ、version/header、または raw 座標が異常な場合は、その値を信用せず `Initialize NVS` を要求する warning 対象として扱います。

English supplement: Calibration data is stored by `cyd_input`, but the raw calibration operation is performed by `cyd_display_calibrate_touch()` and applied by `cyd_display_apply_touch_calibration()`.

このプロジェクトでは、保存済み補正がない場合でも `CONFIG_CYD_TOUCH_X_MIN/X_MAX/Y_MIN/Y_MAX` と `CONFIG_CYD_TOUCH_OFFSET_ROTATION` を使ったデフォルト変換で最低限の操作を可能にします。ただし、これは「保存済み補正」とは別状態です。起動時にキャリブレーション導線へ入れるかどうか、無操作復帰を許可するかどうかは、保存済み補正の有無で判定します。

注意:

- `CONFIG_CYD_TOUCH_OFFSET_ROTATION` の `4` は「180 度回転」ではない
- LovyanGFX では `0` から `3` が 90 度単位の回転、`4` が追加の反転フラグ、`5` から `7` が反転付き回転として扱われる
- この基板設定では `CONFIG_CYD_DISPLAY_ROTATION=1` と `CONFIG_CYD_TOUCH_OFFSET_ROTATION=4` の組み合わせで、未補正時のデフォルト座標系を正しく合わせている

キャリブレーション保存時の注意:

- `cyd_display_calibrate_touch()` は内部で一時的に回転状態を変えて 4 点の raw 座標を取得する
- その 4 点をこの基板設定のまま `setTouchCalibrate()` 用データとして無加工で保存すると、実行時に X/Y の増減が反転した状態で再現されることがある
- このプロジェクトでは `cyd_input_run_touch_calibration()` が、取得した 4 点をランタイム向け順序へ正規化してから再適用・保存する
- そのため、通常のアプリや system settings からタッチ補正を起動する場合は `cyd_input_run_touch_calibration()` を使い、`cyd_display_calibrate_touch()` と `cyd_display_apply_touch_calibration()` を直接つないで使わない

English supplement: On this board, the default raw touch mapping is correct with `CONFIG_CYD_DISPLAY_ROTATION=1` and `CONFIG_CYD_TOUCH_OFFSET_ROTATION=4`, but the raw corner order returned by LovyanGFX calibration is not directly reusable for persistence. `cyd_input_run_touch_calibration()` normalizes the corner order before saving so reboot behavior matches immediate post-calibration behavior.

## Configuration

主な設定項目は `idf.py menuconfig` の `CYD Input` から変更できます。

- `CONFIG_CYD_TOUCH_ENABLED`: タッチ入力を有効にする
- `CONFIG_CYD_TOUCH_SPI_HOST`: タッチコントローラーの SPI host
- `CONFIG_CYD_TOUCH_PIN_SCLK`: タッチ SCLK GPIO
- `CONFIG_CYD_TOUCH_PIN_MOSI`: タッチ MOSI GPIO
- `CONFIG_CYD_TOUCH_PIN_MISO`: タッチ MISO GPIO
- `CONFIG_CYD_TOUCH_PIN_CS`: タッチ CS GPIO
- `CONFIG_CYD_TOUCH_PIN_INT`: タッチ INT GPIO
- `CONFIG_CYD_TOUCH_OFFSET_ROTATION`: タッチ座標の追加回転/反転補正
- `CONFIG_CYD_TOUCH_X_MIN`: タッチ raw X 最小値
- `CONFIG_CYD_TOUCH_X_MAX`: タッチ raw X 最大値
- `CONFIG_CYD_TOUCH_Y_MIN`: タッチ raw Y 最小値
- `CONFIG_CYD_TOUCH_Y_MAX`: タッチ raw Y 最大値
- `CONFIG_CYD_TOUCH_POLL_PERIOD_MS`: タッチのポーリング周期
- `CONFIG_CYD_TOUCH_USE_IRQ`: T_IRQ によるタッチタスク wake を有効にする
- `CONFIG_CYD_TOUCH_IDLE_POLL_PERIOD_MS`: IRQ 待機中の fallback ポーリング周期
- `CONFIG_CYD_INPUT_BINARY_STABLE_COUNT`: 連続して同じ値が必要な安定判定サンプル数
- `CONFIG_CYD_BOOT_BUTTON_ENABLED`: BOOT ボタンイベントを有効にする
- `CONFIG_CYD_BOOT_BUTTON_GPIO`: BOOT ボタン GPIO
- `CONFIG_CYD_BOOT_BUTTON_ENABLE_INTERNAL_PULLUP`: BOOT ボタン GPIO の内部 pull-up を有効にする
- `CONFIG_CYD_BOOT_BUTTON_LONG_PRESS_MAX_SECONDS`: BOOT ボタン長押し通知の最大秒数
- `CONFIG_CYD_BOOT_BUTTON_DOUBLE_CLICK_TIMEOUT_MS`: BOOT ボタン double click 判定の待ち時間
- `CONFIG_CYD_TOUCH_LOG_EVENTS`: タッチイベントログを有効にする
- `CONFIG_CYD_TOUCH_LOG_IRQ_LEVEL`: タッチログに IRQ レベルを含める
- `CONFIG_CYD_TOUCH_USE_NVS_CALIBRATION`: タッチ補正値を NVS に保存/読込する
- `CONFIG_CYD_TOUCH_RUN_CALIBRATION_ON_BOOT`: 保存済み補正がない場合に起動時補正を実行する
- `CONFIG_CYD_INPUT_EVENT_QUEUE_LENGTH`: 入力イベントキューの長さ
- `CONFIG_CYD_INPUT_LONG_PRESS_MS`: 長押し判定までの時間
- `CONFIG_CYD_INPUT_LONG_PRESS_REPEAT_MS`: 長押し繰り返し通知の間隔

English supplement: Touch polling period and click timing directly affect user interaction latency. Keep them explicit when changing UI behavior.
