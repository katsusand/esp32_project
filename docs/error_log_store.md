# Error Log Store

このコンポーネントは、SD カード上の `/sdcard/ERR_0000.LOG` のような suffix 付き log file へエラー系メッセージを 1 行ずつ追記するための helper です。

English supplement: `error_log_store` is an explicit sink for important runtime failures. It is not a global replacement for `ESP_LOGE`, and it records only the call sites that opt in.

書き込みは [SD Card Writer](sd_card_writer.md) の stream を1つ使います。呼び出し元の task は SD を待たずに戻り、writer task がすぐに書いて sync します（flush 間隔 0）。

English supplement: logging never blocks the caller on the card. The line is queued and the writer task writes and syncs it right away.

## Public API

- `error_log_store_start()`
- `error_log_store_stop()`
- `error_log_store_is_failed()`
- `error_log_store_write_error_log(line)`
- `error_log_store_append_message(tag, message)`
- `error_log_store_append_esp_err(tag, message, err)`

`error_log_store_start()` は SD の mount と `sd_card_writer_start()` の後に呼びます。[SD Card Status](sd_card_status.md) が、カードを mount できるたびに呼びます。composition が直接呼ぶことはありません。それより前の行と、SD が無いときの行は捨てます（呼び出し元の serial log には残ります）。

`error_log_store_stop()` は stream を閉じます。以後の行は、`error_log_store_start()` をもう一度呼ぶまで捨てます。カードを unmount する前と、空き容量が尽きる前に使います。他の task が書いている最中に呼んでも安全で、戻った後は、どの task も stream を持っていません。

`error_log_store_is_failed()` は、カードの書き込みが失敗して stream が止まったかを返します。止まった stream は、書き込みが再開することはありません。`stop` の後に `start` して、開き直したものだけが書けます。

English supplement: `stop` waits for producers that are inside `sd_card_writer_write()`, which never waits for room here, so it returns quickly. A stream that has failed does not recover on its own; closing it and starting a new one is the only way back, and each new stream starts a new `ERR_xxxx.LOG` file.

`error_log_store_start()` は `sd_card_writer` の報告先にもなります。ほかのストリームの取りこぼし（まとめて最大10秒に1行）とストリームの失敗が、tag `sd_card_writer` の行として記録されます（[SD Card Writer](sd_card_writer.md#reports)）。エラーログ自身のストリームは報告の対象外です。

`error_log_store_write_error_log(line)` は raw 1 行を current error log file へ追記します。

`append_*` API は formatted line を組み立てて同じ file へ追記します。起動後の最初の行を書くときに `ERR_0000.LOG` のような未使用 filename を採番して新規作成し、その起動中は同じ file へ追記します。エラーが無かった起動では file を作りません。

## Format

```text
[12345 ms] wifi_connection: Wi-Fi setup connection test failed: ESP_FAIL (auth failed)
```

- 時刻は boot からの経過ミリ秒
- `tag` は呼び出し元 component 名
- `message` は呼び出し側が決める固定文言

## Notes

- 保存先は現状 `/sdcard/ERR_%04u.LOG` 形式です
- filename は 8.3 形式に収めています。このプロジェクトの FATFS は ESP-IDF 既定の `CONFIG_FATFS_LFN_NONE`（長い filename 非対応）で、拡張子の前が 8 文字を超える名前は作成できません。以前の `error_0000.log` はこのため一度も作成できていませんでした
- 行単位の text log です
- 1 file が 256 KB（約 4000 行）を超える前に、次の suffix の file へ切り替えます。行の途中では切り替えないので、file は常に行の終わりで終わります
- 再起動後は card 上にある最大 suffix を調べて、その `+1` の file から再開します
- `ERR_9999.LOG` まで使い切ったら、それ以降の error log 保存は停止します
- 書き込みに失敗したとき（file 作成失敗、SD の書き込みエラー、バッファ満杯）は、serial へ warning を 1 回だけ出して以後は黙って捨てます

English supplement: This helper intentionally uses root-level suffixed filenames rather than hidden directory creation, so the file lifecycle stays explicit and easy to inspect on the card. Names must stay within 8.3 while long filename support is disabled; upper case matches what `readdir()` reports for short names, which the index scan depends on.
