# Wi-Fi RSSI History

## Overview

`wifi_rssi_history` は、接続中の AP の RSSI を一定間隔でサンプリングし、直近 N 件を保持する service です。

`components/services/wifi_rssi_history/` に置いています。

このコンポーネントは、`cyd_display` の sparkline widget を実データで検証するための最初の利用例でもあります。**app が「値を集める service」と「それを描く view」に分かれる形の参照実装**として読んでください。

English supplement: this is the reference implementation of the sampling-service pattern. A service owns the task and the buffer; the foreground app is a thin view over it.

## Public API

```c
#include "wifi_rssi_history.h"

esp_err_t wifi_rssi_history_start(void);
bool wifi_rssi_history_get(const int16_t **samples, uint16_t *count, uint16_t *revision);
bool wifi_rssi_history_get_latest(int16_t *rssi_dbm);
```

`wifi_rssi_history_get()` の出力は、そのまま `cyd_display_sparkline_t` に渡せる形になっています。

```c
const int16_t *samples = NULL;
uint16_t count = 0;
uint16_t revision = 0;

if (wifi_rssi_history_get(&samples, &count, &revision)) {
    cyd_display_sparkline_t graph = {
        .samples = samples,
        .count = count,
        .revision = revision,
        .min_value = -100,
        .max_value = -30,
    };
    cyd_ui_add_sparkline(screen, 2, 9, 36, 13, &graph,
                         CYD_UI_COLOR_GREEN, CYD_UI_COLOR_BLACK, CYD_UI_COLOR_DARKGREY);
}
```

出力ポインタはいずれも NULL を渡せます。再描画が必要かどうかだけ知りたい場合は `revision` だけ取得してください。

## Sampling Model

サンプリングは専用 task で行います。

- 間隔: `CONFIG_WIFI_RSSI_HISTORY_SAMPLE_INTERVAL_MS`
- 保持数: `CONFIG_WIFI_RSSI_HISTORY_MAX_SAMPLES`
- 取得元: `esp_wifi_sta_get_ap_info()` の `rssi`

**専用 task を持つ理由は複数あります。** ブロックの有無はそのうちの 1 つに過ぎません。

- 前面 app が何を表示していても記録を続ける必要がある（止まると履歴に穴が空く）
- 優先度を前面 app と独立させたい
- `esp_wifi_sta_get_ap_info()` が短時間で返る保証を、こちらでは取れない

3 番目が実際的な決め手です。Wi-Fi 実装はバイナリブロブなので、**この API が非ブロッキングだとソースで確認する手段がありません**。ESP-IDF の Wi-Fi API は内部で API ロックを取る作りが一般的で、Wi-Fi task がそれを保持している間は待たされ得ます。

esp_timer のコールバックは**システム全体で共有される単一 task**（`CONFIG_ESP_TIMER_TASK_STACK_SIZE` 既定 3584）で動くため、ここで少しでも待つと Wi-Fi や lwIP を含む他の全タイマーが遅れます。task stack 3072 B を節約するために負う risk としては見合いません。

なお当初この task を作った直接の理由は別で、グラフ表示中だけ radio lease を取っていた頃に `radio_manager_acquire()` のブロックを `app_shell` から隔離するためでした（`step()` から呼んで実測 約2.8秒 UI が固まりました）。lease を撤去してその理由は消えましたが、上の 3 点で task は維持しています。

English contract: "no blocking call" is not sufficient reason to drop a task. Independent lifetime, priority isolation, and unverifiable call duration all matter. esp_timer callbacks run on a shared system task, so anything that might wait does not belong there.

## This Service Never Powers The Radio Up

**この service は Wi-Fi を起動しません。** 既に上がっている Wi-Fi を観測するだけです。

一時期は「グラフ表示中だけ radio lease を保持する」実装にしていましたが、グラフを見るためだけに Wi-Fi を起動するのは過剰なので取りやめました。`radio_manager` の lease も client enum も参照しません。

その結果、グラフは「他の機能が Wi-Fi を使っていた区間」を映します。Wi-Fi が落ちている間は `system_info_app` の RSSI page が「Wi-Fi はオフです」と明示します。

Wi-Fi を長く保ちたい場合は、設定の「ネットワーク2」にある「Wi-Fi切断」で `radio_manager` の idle timeout を延ばしてください（「しない」を含む）。

English contract: this service observes, it never acquires. Keeping Wi-Fi alive is a user-facing policy exposed as a setting, not something a diagnostic view decides on its own.

## Gap Markers

未接続時は `WIFI_RSSI_HISTORY_GAP_DBM` を **1 回だけ**記録します。

```c
#define WIFI_RSSI_HISTORY_GAP_DBM INT16_MIN
```

毎周期記録しないのは、`radio_manager` が Wi-Fi を数時間落としたままにできるためです。1 秒ごとに欠測を積むと、**見せたい実測履歴がバッファから押し出されて消えます**。区切りは「ここで途切れた」という 1 個の印で足り、実データを保存する方が有用です。

実測値がまだ 1 件も無い状態での先頭の欠測も、ノイズなので記録しません。

`wifi_rssi_history_get_latest()` は末尾の欠測を**読み飛ばしません**。末尾が欠測であること自体が「今つながっていない」の表現なので、過去の実測値を現在値のように返さないためです。

サンプルは ring buffer ではなく**線形配列で、新しいものが末尾**です。1 サンプルごとに数百バイトの `memmove` が発生しますが、これは「読み手全員に ring の順序を教える」よりも安いという判断です。sparkline widget にそのまま渡せる形を優先しています。

## Concurrency

sampler は `wifi_rssi` task から書き、読み手は `app_shell` task（および描画時は `cyd_display` task）です。

`count` と `revision` の整合性だけ critical section で保護し、**サンプル配列そのものは描画中にロックしません**。書き込みと描画が競合した場合の結果は「一部だけ新しい値のフレームが 1 回出る」だけで、トレンドグラフでは実害が無いためです。

English contract: this is the same trade-off documented for `cyd_display_sparkline_t`. A torn frame is acceptable; a lock held across rendering is not.

## Revision

`revision` はサンプル内容が変わるたびに加算されます。

これは必須の仕組みです。`cyd_display` の dirty-rect 差分は widget を値で比較するため、ポインタの先までは見ません。`revision` を渡さないと、グラフは submit されても**永久に再描画されません**。

同じ値は「再描画が必要か」の判定にも使えます。`system_info_app` の RSSI ページは、前回描画した revision と比較して変化があったときだけ再描画しています。

## Build Feature Switch

`APP_WIFI_STA=0` ビルドでは stub 実装に切り替わります。

- `wifi_rssi_history_start()` は `ESP_ERR_NOT_SUPPORTED` を返す
- `wifi_rssi_history_get()` / `_get_latest()` は `false` を返す

app 側に `#if` を書かずに済むよう、API は維持されます。RSSI ページは「Wi-Fi はオフです」表示になります。

## Configuration

`idf.py menuconfig` の `Wi-Fi RSSI History` から変更できます。

- `CONFIG_WIFI_RSSI_HISTORY_TASK_STACK_SIZE` (既定 3072、実測ピーク使用 1944 B)
- `CONFIG_WIFI_RSSI_HISTORY_TASK_PRIORITY` (既定 4)
- `CONFIG_WIFI_RSSI_HISTORY_SAMPLE_INTERVAL_MS` (既定 1000)
- `CONFIG_WIFI_RSSI_HISTORY_MAX_SAMPLES` (既定 120)

### History Length

**保持できる時間は `MAX_SAMPLES x SAMPLE_INTERVAL_MS` です。** 既定値の組み合わせだとこうなります。

| MAX_SAMPLES | バッファ | 履歴 (1 秒間隔) | 1 サンプルの幅 |
|---|---|---|---|
| 60 | 120 B | 1 分 | 5.3 px |
| **120 (既定)** | **240 B** | **2 分** | **2.7 px** |
| 300 | 600 B | 5 分 | 1.1 px |
| 320 (上限) | 640 B | 5 分 20 秒 | 1.0 px |

上限を 320 にしているのは、**ディスプレイの横幅が 320 px だから**です。それ以上サンプルを持っても 1 px 未満に潰れるだけで、表示の情報量は増えません。逆に 320 は 1 サンプル = 1 px で対応する値です。

間隔を延ばせば同じバッファでより長い履歴になりますが、そのぶん短時間の変動は見えなくなります。

### Memory Balance

**バッファより task stack の方がはるかに大きい**点に注意してください。

| | サイズ |
|---|---|
| サンプルバッファ | 240 B (既定) |
| task stack | 3072 B (実測ピーク 1944 B) |

RAM を削りたい場合、`MAX_SAMPLES` を半分にしても 120 B しか浮きません。task stack の方が効きますが、**3072 は実測に基づく値**で、2048 まで下げると余裕が 104 B しか残らず危険です。詳細は Sampling Model を参照してください。

English supplement: shrinking the sample buffer is not where the memory is. The task stack dominates, and it is already sized from a measured high-water mark.

## Notes

`cyd_clock_composition` は、他の Wi-Fi service と同じく optional service として起動します。起動に失敗しても degraded mode で継続し、ブートループにはなりません。
