# Error Log Store

このコンポーネントは、SD カード上の `/sdcard/error_0000.log` のような suffix 付き log file へエラー系メッセージを 1 行ずつ追記するための helper です。

English supplement: `error_log_store` is an explicit sink for important runtime failures. It is not a global replacement for `ESP_LOGE`, and it records only the call sites that opt in.

## Public API

- `error_log_store_write_error_log(line)`
- `error_log_store_append_message(tag, message)`
- `error_log_store_append_esp_err(tag, message, err)`

`error_log_store_write_error_log(line)` は raw 1 行を current error log file へ追記します。

`append_*` API は formatted line を組み立てて同じ file へ追記します。起動後の最初の書き込み時に `error_0000.log` のような未使用 filename を採番して新規作成し、その起動中は同じ file へ追記します。

## Format

```text
[12345 ms] wifi_connection: Wi-Fi connection connect failed: ESP_FAIL
```

- 時刻は boot からの経過ミリ秒
- `tag` は呼び出し元 component 名
- `message` は呼び出し側が決める固定文言

## Notes

- 保存先は現状 `/sdcard/error_%04u.log` 形式です
- 行単位の text log です
- 1 file あたり 5000 行に達したら次の suffix の file を新規作成して追記先を切り替えます
- 再起動後は card 上にある最大 suffix を調べて、その `+1` の file から再開します
- `error_9999.log` まで使い切ったら、それ以降の error log 保存は停止します
- SD mount 失敗中や file 作成失敗時は、serial へ warning を 1 回だけ出して以後は黙って無視します

English supplement: This helper intentionally uses root-level suffixed filenames rather than hidden directory creation, so the file lifecycle stays explicit and easy to inspect on the card.
