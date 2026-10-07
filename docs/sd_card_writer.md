# SD Card Writer

## Overview

`sd_card_writer` は、SD カードへの書き込みを1つの task にまとめる service です。

書き込みたい側（producer）は stream にデータを渡してすぐ戻ります。stream のバッファにたまったデータは writer task がまとめて SD へ書きます。producer が SD の待ちで止まることはありません。

以前は `error_log_store` が1行ごとに open → write → flush → close を呼び出し元の task で実行していました。ログの種類や頻度が増えると、この方式では次の問題が出ます。

- 呼び出し元の task が SD の書き込みが終わるまで止まる
- 書き手が複数いると、FATFS のボリューム単位のロックで互いに待たされる
- 同じファイルを複数の task が書くと、追記同士が混ざったり上書きし合ったりする（`CONFIG_FATFS_FS_LOCK=0`）
- 小さな追記のたびに、ディレクトリ項目と FAT を書き直す

English contract: producers hand bytes to a stream and return without touching the card. The writer task owns every stream's file and moves buffered bytes to the card in sector-aligned chunks, syncing at each stream's flush interval. One `sd_card_writer_write()` call is one record: it is never split across files, and a record that does not fit in time is dropped whole and counted.

## Public API

```c
#include "sd_card_writer.h"

static sd_card_writer_stream_t *s_sensor_stream;

/* writer task 上で呼ばれ、新しい file の名前を返す。再起動後も既存の file と
   重ならないよう、カード上の最大番号 + 1 を使う（error_log_store と同じ考え方）。 */
static esp_err_t sensor_log_pick_path(char *path, size_t path_size, void *ctx)
{
    unsigned next = sensor_log_find_next_index();
    snprintf(path, path_size, "SENS/S%07u.BIN", next);
    return ESP_OK;
}

const sd_card_writer_stream_config_t config = {
    .name = "sensor",
    .path_fn = sensor_log_pick_path,
    .buffer_size = 32 * 1024,
    .flush_interval_ms = 1000,
    .max_file_size = 16 * 1024 * 1024,
};
ESP_ERROR_CHECK(sd_card_writer_open_stream(&config, &s_sensor_stream));

/* 元でまとめたブロックを1回で渡す。0 は待たない。 */
if (sd_card_writer_write(s_sensor_stream, block, block_size, 0) != ESP_OK) {
    /* 取りこぼし。stats に数えられている */
}
```

| 関数 | 内容 |
| --- | --- |
| `sd_card_writer_start()` | writer task を起動する。SD が mount 済みであること |
| `sd_card_writer_open_stream()` | stream を登録し、バッファを確保する。file は最初のデータが来たときに writer が開く |
| `sd_card_writer_write()` | 1 record を渡す。バッファに空きがなければ `wait_ticks` まで待ち、それでも入らなければ丸ごと捨てる |
| `sd_card_writer_rotate()` | 次の record から新しい file にする。file が大きくなったのではなく、「どの file か」の答えが変わったとき（日付が変わった、など）に使う |
| `sd_card_writer_flush()` | そこまでのデータを書いて sync し、SD に載るまで待つ |
| `sd_card_writer_close_stream()` | flush して file を閉じ、stream を解放する |
| `sd_card_writer_get_stats()` | 受け付けた量、書けた量、捨てた量、バッファの最大使用量など |

stream の file は、次の 3 つのどれか 1 つで決めます。親ディレクトリが無ければ作ります。

| 指定 | file |
| --- | --- |
| `path` | 固定の file。既存なら追記、`truncate` なら空から |
| `path_fn` | 毎回、**新しい** file を選ぶ（既存の名前だと open が失敗する） |
| `target_fn` | 毎回、既存の file（追記）か新しい file を選び、file の先頭に書く内容（ヘッダ）も返す |

FATFS の長いファイル名（`CONFIG_FATFS_LFN_HEAP`）が有効でないと、名前の各要素は 8.3 形式でなければなりません。有効かどうかは、使う側が確かめてください。

`sd_card_writer_write()` は task から呼びます（ISR からは呼べません）。同じ stream に複数の task から書いても、record 同士が混ざることはありません。

## Throughput

1つの stream で毎秒 100 KB 程度を書き続けられることを目標にしています。そのために次のようにしています。

- file は開いたままにし、open と close は最初と rotation のときだけ
- 1回の `write()` は最大 `CONFIG_SD_CARD_WRITER_CHUNK_SIZE`（既定 8 KB）で、sector 境界にそろえる。まるごとの sector は、DMA 対応のバッファから直接 SD へ送られる
- ディレクトリ項目と FAT を更新する sync は、stream ごとの flush 間隔で1回だけ
- producer は、バッファがある程度たまったときだけ writer を起こす

SD は SPI3 を専有しているので、ディスプレイ（SPI2）の描画とはバスを取り合いません。SPI クロックは ESP-IDF 既定の 20 MHz です。

### Measuring

`idf.py menuconfig` の `SD Card Writer` で `Run a write throughput benchmark at start` を有効にすると、起動時に `BENCH.BIN` へ一定レート（既定 100 KB/s、30 秒）で書き込み、毎秒の状況と最後の結果をログに出します。

最後の行は次の形式です（`<>` は実測値）。

```text
sd_card_bench: done: <書けた量> KB written in <経過> ms (<速度> KB/s), dropped <捨てた量> KB in <件数> records, waiting max <最大使用量> KB
```

`BENCH.BIN` は連番の uint32（little endian）なので、PC で読めば取りこぼした位置も分かります。確認が終わったら無効に戻してください。

English supplement: the benchmark is for measurement on real hardware only. Throughput depends on the card; check `dropped` and `waiting max` rather than assuming the target is met.

## Buffer Sizing

`buffer_size` は「書き込みレート × SD が一時的に止まる時間」より大きくしてください。SD カードは内部の処理で、数百 ms 書き込みが止まることがあります。毎秒 100 KB なら、32 KB で約 0.3 秒分です。

バッファが足りないと `sd_card_writer_write()` は `ESP_ERR_TIMEOUT` を返し、その record を丸ごと捨てて `records_dropped` に数えます。`buffer_high_water`（バッファの最大使用量）を見て調整してください。

stream ごとのバッファのほかに、writer task の stack（既定 4 KB）と、DMA 用の chunk バッファ（既定 8 KB）を使います。

## Durability

sync する前は FATFS のディレクトリ項目のファイルサイズが更新されないので、flush 間隔のあいだに電源が切れると、その間に書いた分は失われます。

`flush_interval_ms = 0` にすると、書いたらすぐ sync します。エラーログのような、まれだが残したいデータ向けです。ほかのタイミングで確実に残したいときは `sd_card_writer_flush()` を呼びます。

English supplement: data written since the last sync can be lost on power failure. Choose flush_interval_ms per stream: 0 for rare important lines, about a second for high-rate data.

## Rotation

`path_fn` と `max_file_size` を指定すると、file がその大きさを超える前に、次の file へ切り替えます。切り替えは record の境界で行うので、1回の `sd_card_writer_write()` で渡したデータが2つの file に分かれることはありません（そのため file はその record 1つ分だけ上限を超えることがあります）。

`path_fn` は writer task 上で呼ばれ、まだ存在しない file の名前を返します。既存の file を返すと、上書きを避けるために open が失敗します。

## Target Function

`target_fn` は、既存の file に続けて書く用途のための、`path_fn` の拡張です。writer task 上で、最初のデータが来たときと、切り替えのたびに呼ばれます。

```c
static esp_err_t pick_target(sd_card_writer_target_t *target, void *ctx)
{
    /* 今日の file を探し、余裕があればそれを、無ければ次の番号を選ぶ */
    snprintf(target->path, sizeof(target->path), "ERR_2026-10-07_01.LOG");
    target->append = true;                         /* 既存の file に追記する */
    target->header_len = snprintf(target->header, sizeof(target->header), "boot: ...\n");
    return ESP_OK;
}
```

- `append` が真で file が無ければ、新規に作ります。偽のときは `path_fn` と同じく、既存の file を上書きしません
- `header` は、file を開いた直後（追記のときは末尾）に、データより先に書きます。record ではなく、そのまま書きます
- 追記した file の元のサイズと、ヘッダの長さは、`max_file_size` に数えます。数えないと、250 KB の file に追記して 500 KB を超えてしまいます
- `target_fn` は writer task を止めますが、producer は止めません。ディレクトリを走査してもかまいません

English supplement: use `target_fn` when a file is reused across runs. The size of an appended file and the header are counted toward max_file_size, so the limit means the size of the FILE. If a rotation is already queued when the file opens, the count is left alone and that file may run over the limit by what was queued meanwhile.

## Rotating On Demand

`sd_card_writer_rotate()` は、サイズ上限に達したときと同じ扱いで、次の record から新しい file にします。writer は今の file を閉じ、`path_fn` か `target_fn` に次の file を尋ねます。record が来るまで file は作らないので、その後何も書かない stream を切り替えても、空の file は残りません。

使い所は、「どの file か」の答えが、大きさ以外の理由で変わったときです。例えばエラーログは、日付が変わったとき（0 時、または時計が合ったとき）に使います。

- stream が failed なら `ESP_ERR_INVALID_STATE` です
- 切り替えがすでに積まれていて、これ以上積めないときは `ESP_ERR_NO_MEM` です。その切り替えは、積まれているものがすでに同じ所へ届きます
- 直前の切り替えから何も書いていなければ、何もしません（空の file を重ねないため）

## Reports

取りこぼしとストリームの失敗は、`sd_card_writer_set_report_fn()` で登録した関数へ1行ずつ報告します。この製品では `error_log_store_start()` が自分を登録するので、SD のエラーログに残ります。

```text
[45120 ms] sd_card_writer: bench: dropped 20 records (20480 bytes); buffer peaked at 32768 of 32768 bytes
```

- **取りこぼし:** 取りこぼしは SD が止まっている間にまとめて起きるので、1件ずつではなく、ストリームごとに `CONFIG_SD_CARD_WRITER_DROP_REPORT_INTERVAL_MS`（既定10秒）に最大1行でまとめます。ストリームを閉じるときは、残りをすぐ報告します。
- **失敗:** ストリームが止まったときに1行報告します。
- **対象外にする:** `silent` を指定したストリームは報告しません。報告を運ぶストリーム（エラーログ）が、自分について報告し続けないようにするためです。

`sd_card_writer` は報告先の関数を受け取るだけで、`error_log_store` には依存しません。

English supplement: trouble is reported as lines through a callback, summed per stream per interval so reporting never adds card writes while the card is behind. The stream that carries the reports is marked silent.

## Failures

open、write、sync のどれかが失敗すると、その stream は failed になり、以後のデータは捨てます（`sd_card_writer_write()` は `ESP_ERR_INVALID_STATE`）。カードの抜き差しを検出する手段がないので、再試行はしません。失敗はログに1回だけ出します。

## Limits

- stream の数は `CONFIG_SD_CARD_WRITER_MAX_STREAMS`（既定 4）まで。各 stream は file を1つ開いたままにするので、`CONFIG_SD_CARD_STORAGE_MOUNT_MAX_FILES`（既定 5）より少なくしてください
- writer task から `sd_card_writer_flush()` などの要求関数を呼ぶと、自分を待つことになるため `ESP_ERR_INVALID_STATE` を返します（`path_fn` の中から呼ばないでください）。writer task 上の `sd_card_writer_write()` は、同じ理由で待たずに扱います

## Relationship With Other SD Components

| component | 役割 |
| --- | --- |
| `sd_card_storage` | SPI バスの初期化と FAT の mount |
| `sd_card_writer` | ログのような継続的な書き込み。1つの task がすべての stream の file を持つ |
| `sd_card_files` | 設定の保存のような、まれな単発の書き込み。呼び出し元の task で同期実行する |
| `error_log_store` | `sd_card_writer` の stream を1つ使うエラーログ。`target_fn` で、日付ごとの file に追記する |

継続的に書くデータは `sd_card_writer` を使ってください。`sd_card_files` で頻繁に書くと、この文書の最初に挙げた問題がそのまま出ます。
