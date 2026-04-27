# CYD Status LED Driver

## Overview

`cyd_status_led` は、CYD オンボード RGB LED を扱うステータス表示用コンポーネントです。

アプリ側は、RGB 値を直接設定するか、色と点滅効果を組み合わせたパターンを送ります。パターン再生は内部の FreeRTOS キューと専用タスクで処理されます。

English supplement: This component owns the LED pattern queue and update task. Application code should use the public API for pattern changes instead of managing LED timing itself.

## Public API

利用するファイルでは、次のヘッダーを include します。

```c
#include "cyd_status_led.h"
```

初期化は、通常 `app_main()` の起動処理で一度だけ呼びます。

```c
ESP_ERROR_CHECK(cyd_status_led_init());
```

RGB を直接指定して点灯する場合は `cyd_status_led_set_rgb()` を使います。

```c
ESP_ERROR_CHECK(cyd_status_led_set_rgb(255, 0, 0));
```

継続的な基本パターンを設定する場合は `cyd_status_led_set_base_pattern()` を使います。

```c
const cyd_led_pattern_t pattern = {
    .color = CYD_LED_COLOR_GREEN,
    .effect = CYD_LED_EFFECT_BLINK_SLOW,
    .play = CYD_LED_PLAY_CONTINUOUS,
    .duration_ms = 0,
};

ESP_ERROR_CHECK(cyd_status_led_set_base_pattern(&pattern));
```

一時的な通知パターンを重ねる場合は `cyd_status_led_trigger_pattern()` を使います。

```c
const cyd_led_pattern_t flash = {
    .color = CYD_LED_COLOR_YELLOW,
    .effect = CYD_LED_EFFECT_FLASH_FAST,
    .play = CYD_LED_PLAY_ONESHOT,
    .duration_ms = 500,
};

ESP_ERROR_CHECK(cyd_status_led_trigger_pattern(&flash));
```

English supplement: `cyd_status_led_set_rgb()` writes GPIO levels immediately. Pattern APIs enqueue commands and are processed by the LED task.

## Color Model

色は `cyd_led_color_t` で指定します。

```c
typedef enum {
    CYD_LED_COLOR_OFF = 0,
    CYD_LED_COLOR_RED,
    CYD_LED_COLOR_GREEN,
    CYD_LED_COLOR_BLUE,
    CYD_LED_COLOR_YELLOW,
    CYD_LED_COLOR_MAGENTA,
    CYD_LED_COLOR_CYAN,
    CYD_LED_COLOR_WHITE,
} cyd_led_color_t;
```

各色は RGB LED の各チャンネルの ON/OFF の組み合わせです。現在の実装では、明るさの段階制御は行っていません。

CYD オンボード RGB LED は active-low です。そのため、内部では `red > 0` のように点灯したいチャンネルを GPIO Low にして点灯します。

English supplement: The public RGB values are logical brightness values. Hardware output is inverted because the onboard LED is active-low.

## Effect Model

効果は `cyd_led_effect_t` で指定します。

```c
typedef enum {
    CYD_LED_EFFECT_OFF = 0,
    CYD_LED_EFFECT_ON,
    CYD_LED_EFFECT_BLINK_SLOW,
    CYD_LED_EFFECT_BLINK_FAST,
    CYD_LED_EFFECT_FLASH_SLOW,
    CYD_LED_EFFECT_FLASH_FAST,
} cyd_led_effect_t;
```

各効果の意味は以下です。

- `OFF`: 消灯
- `ON`: 点灯
- `BLINK_SLOW`: 1000 ms 周期で点滅
- `BLINK_FAST`: 250 ms 周期で点滅
- `FLASH_SLOW`: overlay 用の点滅効果
- `FLASH_FAST`: overlay 用の高速点滅効果

点滅の duty はおおよそ 50% です。

English supplement: `FLASH_*` effects are currently meaningful for overlay rendering. Base pattern rendering treats only `OFF`, `ON`, and `BLINK_*` as visible effects.

## Pattern Model

LED パターンは `cyd_led_pattern_t` で指定します。

```c
typedef struct {
    cyd_led_color_t color;
    cyd_led_effect_t effect;
    cyd_led_play_t play;
    uint32_t duration_ms;
} cyd_led_pattern_t;
```

`play` は以下のどちらかです。

```c
typedef enum {
    CYD_LED_PLAY_CONTINUOUS = 0,
    CYD_LED_PLAY_ONESHOT,
} cyd_led_play_t;
```

各フィールドの意味は以下です。

- `color`: 表示する色
- `effect`: 点灯または点滅効果
- `play`: 継続再生か一回だけの再生か
- `duration_ms`: `CYD_LED_PLAY_ONESHOT` の overlay を表示する時間

`cyd_status_led_set_base_pattern()` に渡したパターンは、内部で `CYD_LED_PLAY_CONTINUOUS` として扱われます。

`cyd_status_led_trigger_pattern()` に `CYD_LED_PLAY_ONESHOT` かつ `duration_ms > 0` のパターンを渡すと、その時間だけ overlay として表示されます。overlay が終わると base pattern に戻ります。`CYD_LED_PLAY_CONTINUOUS` または `duration_ms == 0` の trigger pattern は、現在の実装では overlay として有効化されません。

English supplement: The LED task maintains one base pattern and one temporary overlay pattern. The overlay has priority only while an one-shot trigger is active.

## Queue Model

`cyd_status_led` は内部に FreeRTOS キューを持ちます。現在のキュー長は `8` です。

パターンAPIは、以下のような内部コマンドをキューへ送ります。

```c
typedef enum {
    CYD_STATUS_LED_CMD_SET_BASE_PATTERN = 0,
    CYD_STATUS_LED_CMD_TRIGGER_PATTERN,
} cyd_status_led_cmd_id_t;
```

キュー送信の待ち時間は 100 ms です。キューへ送れない場合は `ESP_ERR_TIMEOUT` を返します。

LED タスクは 25 ms ごとに状態を更新し、必要がある場合だけ GPIO 出力を変更します。

English supplement: Pattern updates are asynchronous. The LED task samples commands and renders output on a 25 ms update cadence.

## Configuration

主な設定項目は `idf.py menuconfig` の `CYD Status LED` から変更できます。

- `CONFIG_CYD_STATUS_LED_RED_GPIO`: 赤チャンネルの GPIO
- `CONFIG_CYD_STATUS_LED_GREEN_GPIO`: 緑チャンネルの GPIO
- `CONFIG_CYD_STATUS_LED_BLUE_GPIO`: 青チャンネルの GPIO

デフォルト設定は以下です。

- `CONFIG_CYD_STATUS_LED_RED_GPIO=4`
- `CONFIG_CYD_STATUS_LED_GREEN_GPIO=16`
- `CONFIG_CYD_STATUS_LED_BLUE_GPIO=17`

English supplement: These GPIOs are board-specific. Verify the pins before reusing the component on a non-CYD board.
