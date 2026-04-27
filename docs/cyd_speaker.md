# CYD Speaker Driver

## Overview

`cyd_speaker` は、CYD 用の音声出力コンポーネントです。

現在の実装は、ESP32 の LEDC PWM 出力でパッシブブザーまたは小型スピーカーを鳴らす、短い効果音向けのドライバーです。音声合成や PCM オーディオ再生はまだ行いません。

アプリ側は公開関数を呼んで再生要求を送るだけでよく、内部では `cyd_speaker` が FreeRTOS キューと専用タスクを使って非同期に再生します。

English supplement: This component owns its queue and playback task. Application code should call the public API instead of creating or sending speaker queue items directly.

デフォルトのハードウェア設定は以下です。

- `CONFIG_CYD_SPEAKER_GPIO=26`
- `CONFIG_CYD_SPEAKER_LEDC_TIMER=0`
- `CONFIG_CYD_SPEAKER_LEDC_CHANNEL=0`
- `CONFIG_CYD_SPEAKER_VOLUME_PERCENT=50`

これらは `idf.py menuconfig` の `CYD Speaker` から変更できます。

## Public API

利用するファイルでは、次のヘッダーを include します。

```c
#include "cyd_speaker.h"
```

初期化は通常 `app_main()` の起動処理で一度だけ呼びます。

```c
ESP_ERROR_CHECK(cyd_speaker_init());
```

よく使う効果音は `cyd_speaker_play_event()` で再生できます。

```c
cyd_speaker_play_event(CYD_SPEAKER_EVENT_CLICK);
cyd_speaker_play_event(CYD_SPEAKER_EVENT_BEEP);
cyd_speaker_play_event(CYD_SPEAKER_EVENT_ERROR);
cyd_speaker_play_event(CYD_SPEAKER_EVENT_WARNING);
cyd_speaker_play_event(CYD_SPEAKER_EVENT_ALERT);
cyd_speaker_play_event(CYD_SPEAKER_EVENT_ALARM);
cyd_speaker_play_event(CYD_SPEAKER_EVENT_SUCCESS);
```

単音を鳴らす場合は `cyd_speaker_play_tone()` を使います。

```c
cyd_speaker_play_tone(1200, 80);
```

複数の音を順番に鳴らす場合は `cyd_speaker_play_sequence()` を使います。

```c
const cyd_speaker_note_t notes[] = {
    { .frequency_hz = 880, .duration_ms = 100, .gap_ms = 30 },
    { .frequency_hz = 1320, .duration_ms = 120, .gap_ms = 0 },
};

cyd_speaker_play_sequence(notes, sizeof(notes) / sizeof(notes[0]));
```

現在の再生とキューに残っている再生要求を止めたい場合は `cyd_speaker_stop()` を使います。

```c
cyd_speaker_stop();
```

English supplement: Playback APIs enqueue commands and return quickly. They do not wait until the sound has finished playing.

## Queue Model

このコンポーネントは、内部に FreeRTOS キューを持ちます。アプリ側が `QueueHandle_t` を作ったり、キューへ直接送信したりする必要はありません。

内部では、現在以下のようなコマンドをキューに積んでいます。

```c
#define CYD_SPEAKER_QUEUE_LEN 8

typedef enum {
    CYD_SPEAKER_CMD_PLAY = 0,
    CYD_SPEAKER_CMD_STOP,
} cyd_speaker_cmd_id_t;

typedef struct {
    cyd_speaker_cmd_id_t id;
    cyd_speaker_note_t notes[CYD_SPEAKER_MAX_NOTES];
    size_t note_count;
} cyd_speaker_cmd_t;
```

キューアイテムの意味は以下です。

- `id`: コマンド種別。`PLAY` は音列の再生、`STOP` は PWM 出力の停止
- `notes`: キューアイテム内にコピーされる固定長の音符配列
- `note_count`: `notes` のうち有効な要素数

音符は次の形式です。

```c
typedef struct {
    uint32_t frequency_hz;
    uint32_t duration_ms;
    uint32_t gap_ms;
} cyd_speaker_note_t;
```

各フィールドの意味は以下です。

- `frequency_hz`: 周波数。`0` の場合は `duration_ms` の間、無音
- `duration_ms`: 音または無音を継続する時間
- `gap_ms`: その音の後、次の音までに入れる無音時間

現在の制限は以下です。

- 1回の再生要求で指定できる最大音符数: `CYD_SPEAKER_MAX_NOTES`、現在は `8`
- 有効な非ゼロ周波数: `20` から `20000` Hz
- `duration_ms` の最大値: `5000`
- `gap_ms` の最大値: `5000`
- キュー送信の待ち時間: `100` ms

English supplement: The note array is copied into the queue item before the API returns. The caller does not need to keep the original array alive after a successful `cyd_speaker_play_sequence()` call.

## Playback Behavior

`cyd_speaker_play_tone()`、`cyd_speaker_play_sequence()`、`cyd_speaker_play_event()` は `PLAY` コマンドをキューに積み、すぐに戻ります。

実際の再生は `cyd_speaker_task()` が担当します。タスクはキューからコマンドを受け取り、FIFO 順に処理します。

再生中は、20 ms ごとにキューを確認します。これにより、長めの音を再生している途中でも、後続の再生要求や停止要求をある程度すばやく反映できます。

再生中に別の `PLAY` が届いた場合、現在の音列は次の 20 ms チェックで中断され、新しいコマンドがキューの先頭へ戻されてから処理されます。

`cyd_speaker_stop()` はキューをリセットしてから `STOP` コマンドを積みます。そのため、現在の音と未処理の音をまとめて止める用途に使います。

English supplement: Playback interruption latency is bounded by the 20 ms polling chunk used inside `cyd_speaker_delay_or_stop()`.

## PWM Output

`CONFIG_CYD_SPEAKER_GPIO` で指定した GPIO は PWM 出力として使われます。

このドライバーは ESP-IDF の LEDC を low-speed mode で使用します。

- 音の周波数は `ledc_set_freq()` で変更する
- 音量は `CONFIG_CYD_SPEAKER_VOLUME_PERCENT` をもとに PWM duty で近似する
- 無音は duty を `0` にして作る

パッシブブザーでは PWM 駆動が想定されます。アクティブブザーではブザー内部に発振回路があるため、設定した周波数が音程に強く反映されない場合があります。

実スピーカーを使う場合は、ESP32 の GPIO から低インピーダンススピーカーを直接駆動せず、適切なトランジスタやアンプ回路を使ってください。

English supplement: Do not treat the ESP32 GPIO as an audio power output. The GPIO only provides a PWM control signal suitable for a buzzer input or an external driver circuit.

## Sound Table

現在の組み込み効果音は以下です。

| Event | Pattern |
| --- | --- |
| `CLICK` | 1800 Hz, 35 ms |
| `BEEP` | 1200 Hz, 80 ms |
| `ERROR` | 330 Hz 160 ms, 40 ms gap, 220 Hz 220 ms |
| `WARNING` | 660 Hz 120 ms, 80 ms gap, 660 Hz 120 ms |
| `ALERT` | 880 Hz x3, 120 ms each, 60 ms gaps |
| `ALARM` | 880 Hz and 440 Hz alternating, 180 ms each |
| `SUCCESS` | 1200 Hz 60 ms, 20 ms gap, 1800 Hz 90 ms |

`ALERT` を正式なイベント名として使います。`aleart` は typo として扱い、ドキュメントやコードには追加しないでください。

English supplement: Use `CYD_SPEAKER_EVENT_ALERT` only. Do not add compatibility aliases for the misspelling `aleart` unless a real external API already depends on it.
