# Error Log Store

このコンポーネントは、どの component も同じ書き方でエラーを記録できる、共通のエラーログです。呼び出し側は、何が失敗したか（エラーコードとメッセージ）を渡すだけで、SD カード上のログファイルに 1 行追記されます。アプリ名・関数名・行番号・時刻は自動で付きます。

English supplement: `error_log_store` is the shared error log. A component says what failed and with which code; the line, its stamp, its file and its place on the card are decided here. It is an explicit sink for important failures, not a replacement for `ESP_LOGE`.

## Quick Start

```c
#define TAG "wifi_connection"   /* the component's own name, as everywhere else */
#include "error_log_store.h"

ERROR_LOG(err, "connect failed (%s)", reason);   /* with an esp_err_t */
ERROR_LOG_MSG("card %s was refused", uid);       /* without a code */
```

書かれる行はこうなります。

```text
[2026-10-07 12:34:56 +12345ms] wifi_connection connect_task:312 ESP_ERR_TIMEOUT(0x107): connect failed (AP not found)
```

- `TAG` がアプリ（component）名になります。ファイルごとに `TAG` を定義する、既存のやり方のままです
- 同じ行は serial にもエラーレベルで出るので、別に `ESP_LOGE` を書く必要はありません
- 呼び出し元はカードを待ちません。書けないときは RAM に溜めます。失敗を呼び出し元へ返すこともありません
- 割り込みからは呼べません

English supplement: the macros take the file's `TAG` as the app name and add `__func__` and `__LINE__`. They never fail and never wait for the card. `ERROR_LOG(ESP_OK, ...)` and `ERROR_LOG_MSG(...)` leave out the code field.

## Line Format

```text
[<日付> <時刻> +<起動からの ms>] <tag> <関数>:<行> <エラー名>(<コード>): <メッセージ>
```

| 部分 | 内容 |
|---|---|
| 日付 時刻 | ローカルの日時。時計が合っていないときは `1970-01-01 00:00:xx` |
| `+NNNNms` | 起動からの経過ミリ秒。時計が合っていなくても、行の順序を示す |
| `<tag> <関数>:<行>` | 記録した場所。旧 API から来た行には関数と行がない |
| `ESP_ERR_TIMEOUT(0x107)` | エラー名とコード。負の値（`ESP_FAIL`）は `ESP_FAIL(-1)` と 10 進で書く |
| メッセージ | 改行などの制御文字は空白になる（1 行 = 1 件の記録を守る） |

1 行は改行を含めて最大 255 バイトです。長いメッセージは捨てずに切り詰めて、末尾を `...` にします。

English supplement: a line is never silently missing. Over-long lines are cut and end in `...`; control characters become spaces so a message cannot split a record.

## Files

```text
ERR_2026-10-07_01.LOG
    \________/ \/
     local date number
```

- 同じ日付の行は、同じファイルに追記します。ファイルが 256 KB に近づくと、次の番号のファイルに切り替わります
- 日付が変わると（0 時、または時計が合ったとき）、次の行から別のファイルになります
- 時計が合っていない間は `ERR_1970-01-01_nn.LOG` になります。この名前が、時計が合っていなかった記録の目印です
- 番号は 2 桁以上で、99 を超えても増え続けます（時計が合わない個体は、起動のたびに同じ `1970-01-01` に積み上がるため）。`9999` まで使うと、その日の記録は止まります
- 旧形式の `ERR_0001.LOG` などは、触りません

ファイルを決める手順は次のとおりです。

1. 今日の日付を決める（時計が合っていなければ `1970-01-01`）
2. カード上の、今日の日付のファイルのうち、最大の番号を探す
3. 無ければ番号 1 で新規作成。有って、サイズに余裕があれば（256 KB の少し手前まで）それに追記。余裕が無ければ次の番号で新規作成
4. 追記するとき、最後の行が改行で終わっていなければ（電源が切れた起動の続き）、先に改行を書く
5. 開いたら、先頭に `boot:` の行を書く（下記）

`余裕` は、ファイルの大きさのことです。カードの空き容量は [SD Card Status](sd_card_status.md) の `FULL` が見ます。

English supplement: the file is a function of today's date. The newest file of that date with room is appended to, so several boots on one day share a file; a full one is followed by the next number. "Room" is judged a little under the limit, because the writer ends a file just under it and that file would otherwise look usable again.

### Long File Names

ファイル名は 8.3 に収まりません。FATFS の長いファイル名（`CONFIG_FATFS_LFN_HEAP`）が必要で、`sdkconfig.defaults` が有効にします。既存の `sdkconfig` は `sdkconfig.defaults` の変更を引き継がないので、無効のままだとビルドが `#error` で止まります。その場合は `idf.py menuconfig` で FAT Filesystem support の Long filename support を有効にするか、`sdkconfig` を消して再構成してください。

English supplement: long names are required. The build fails on purpose rather than letting every file open fail with FR_INVALID_NAME at run time, which would leave a unit that never writes its log and says nothing.

## Boot Line

ファイルを開くたびに（新規でも追記でも）、先頭に 1 行書きます。

```text
[2026-10-07 12:34:56 +12345ms] boot: id=3f2b8c1e-... part=2 fw=1.4.0 reset=SW dev=1 clock=set ntp=ok
```

| 項目 | 内容 |
|---|---|
| `id` | この起動の UUID（[Boot ID](boot_id.md)）。同じ起動のファイルを結びつける |
| `part` | この起動で何番目に開いたファイルか。順番と、欠けたファイルが分かる |
| `fw` | ファームウェアのバージョン |
| `reset` | リセットの原因（`POWERON`、`SW`、`PANIC`、`TASK_WDT`、`BROWNOUT` など） |
| `dev` | DEV ビルドなら 1 |
| `clock` | 開いた時点で時計が合っていたか（`set` / `unset`） |
| `ntp` | 時刻の問いにどう答えが出たか（`ok` / `failed` / `none` / `timeout` / `preset`） |

遅れて時計が合うと、`1970-01-01` のファイルの後に日付つきのファイルが続きます。2 つのファイルの `id` が同じなら、同じ起動の記録です。UUID は「同じ起動か」を示すもので、起動の前後は日付と番号で分かります。

English supplement: the boot line is what ties files together when a late clock sync, midnight or the size limit splits one boot over several files. The UUID says "same run", not "which came first".

## Waiting Before Writing

行は、次の 2 つがそろうまで RAM に溜めます。

1. **カードの準備ができたこと**（`error_log_store_start()`）
2. **時刻の問いに答えが出たこと**（時計が合っているかどうかが分かった）

ファイルを作ってから日付が違った、ということを避けるためです。答えが出たら、溜めた行を順に書きます。

時刻の問いの答えは、次のどれかです。

| 答え | いつ |
|---|---|
| `ok` | `time_sync` が同期に成功した |
| `failed` | 同期の要求が失敗で終わった（接続できない、NTP が諦めた）。時計が合っていなければ、`1970-01-01` のファイルに書く |
| `none` | 時刻同期が無いビルド、または起動しなかった |
| `preset` | 時計がすでに合っていた（ソフトリセット後。RTC の時刻は電源を切るまで残る） |
| `timeout` | 起動から `CONFIG_ERROR_LOG_STORE_TIME_WAIT_SECONDS`（既定 180 秒）が過ぎても答えが無い（Wi-Fi 設定画面を開いたまま、など） |

- 溜めた行は、書き出すときに、記録した時点の時刻へ戻します。時計が合う前に溜めた行も、実際の日時になります（`現在時刻 − (現在の起動後 ms − その行の起動後 ms)`）。時計が合わなかったときは `1970-01-01` のまま
- 溜める大きさは `CONFIG_ERROR_LOG_STORE_HOLD_BYTES`（既定 4096、約 35 行）です。あふれたときは**新しい行を捨て**、古い行を残します。最初の失敗が、その後を説明するからです。捨てた件数は、最初の行に書きます
- 同じ仕組みは、カードを抜いている間にも働きます。`error_log_store_stop()` の後に記録した行は溜まり、カードを戻して `error_log_store_start()` を呼ぶと書き出されます

English supplement: lines wait until the card is ready AND the clock question is answered, so a file is never started before it is known whether its date is real. Held lines carry only their uptime and get their wall-clock stamp when released. When the buffer is full the NEWEST lines are refused, the first ones are kept, and the count of refused lines is written as the first line.

## Repeated Errors

同じ行（タグ・関数・行・コード・メッセージがすべて同じ）が `CONFIG_ERROR_LOG_STORE_REPEAT_WINDOW_SECONDS`（既定 10 秒）の間に繰り返すと、最初の 1 行だけ書き、窓の最後に 1 行にまとめます。

```text
[... +12345ms] wifi_connection connect_task:312 ESP_ERR_TIMEOUT(0x107): connect failed
[... +22400ms] wifi_connection connect_task:312: previous line repeated 41 more times
```

- メッセージが違えば別の記録です（カード番号が違うなど）。畳むのは完全に同じものだけなので、失われる情報はありません
- ほかの行が来なくても、窓が終わるとタイマで書きます。`error_log_store_stop()` でも、溜まった回数を書いてから閉じます
- `0` にすると、畳みません

## Public API

| 関数 | 内容 |
|---|---|
| `ERROR_LOG(err, fmt, ...)` / `ERROR_LOG_MSG(fmt, ...)` | エラーを記録する（普通はこちら） |
| `error_log_store_report(tag, func, line, err, fmt, ...)` | 上のマクロの実体 |
| `error_log_store_notify_time_decided(outcome)` | 時刻の問いに答える。`time_sync` が、成功（`SYNCED`）と失敗（`FAILED`）で呼ぶ。時刻源の無いビルドは起動時に `UNAVAILABLE` を呼ぶ |
| `error_log_store_start()` | カードの準備ができた（マウント済み、`sd_card_writer_start()` 済み） |
| `error_log_store_stop()` | カードが無くなる、または満杯。stream を閉じ、以後の行は溜める |
| `error_log_store_is_failed()` | writer が stream を諦めたか |
| `error_log_store_append_message()` / `_append_esp_err()` / `_write_error_log()` | 旧 API。同じ経路を通るが serial には出さない |

[SD Card Status](sd_card_status.md) が、カードのマウントに合わせて `start` と `stop` を呼びます。composition は直接呼びません。

`error_log_store_start()` は、溜めた行を書けるときは、その場で書き出します。書く準備（stream を開く）に失敗したときだけエラーを返し、行は溜めたまま残ります。

`error_log_store_stop()` は、他の task が書いている最中でも安全です。戻った後は、どの task も stream を持っていません。start と stop は 1 つの task から呼んでください。

`error_log_store_start()` は `sd_card_writer` の報告先にもなります。ほかのストリームの取りこぼし（まとめて最大 10 秒に 1 行）とストリームの失敗が、tag `sd_card_writer` の行として記録されます（[SD Card Writer](sd_card_writer.md#reports)）。エラーログ自身の stream は報告の対象外です。

English supplement: start and stop are called by one task (sd_card_status). `stop` waits for producers that are inside the writer, which never waits for room here, so it returns quickly. A stream that has failed does not recover on its own; stop followed by start is the only way back.

## Configuration

| Kconfig | 既定値 | 内容 |
|---|---|---|
| `ERROR_LOG_STORE_HOLD_BYTES` | 4096 | カードまたは時刻を待つ行を溜める RAM |
| `ERROR_LOG_STORE_TIME_WAIT_SECONDS` | 180 | 時刻の問いに答えが無いとき、失敗として書き始めるまで（起動から） |
| `ERROR_LOG_STORE_REPEAT_WINDOW_SECONDS` | 10 | 同じ行を畳む窓。0 で無効 |

## Tests

```bash
bash test/host/run.sh
```

- `test_error_log_format.c` / `test_error_log_naming.c` / `test_error_log_hold.c` / `test_error_log_dedupe.c`: 行の整形、ファイル名と追記先の決定、溜めるバッファ、繰り返しの抑制
- `test_error_log_store_machine.c`: 実際の `error_log_store.c` を、本物のファイルを一時ディレクトリへ書く偽の writer、合っている／いない時計、手で進めるタイマで動かします。保留とその解放、時刻の問い、追記先と改行の補正、ファイルの切り替え（番号・日付・時計が合ったとき）、カードの抜き差し、あふれ、繰り返し

English supplement: the end-to-end test checks what ends up in the files. What it cannot cover is the card itself and the FreeRTOS writer task, which must be checked on a unit.
