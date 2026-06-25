# Error Log Store

このコンポーネントは、SD カード上の `/sdcard/error.log` へエラー系メッセージを 1 行ずつ追記するための helper です。

English supplement: `error_log_store` is an explicit sink for important runtime failures. It is not a global replacement for `ESP_LOGE`, and it records only the call sites that opt in.

## Public API

- `error_log_store_append_message(tag, message)`
- `error_log_store_append_esp_err(tag, message, err)`

どちらも 1 行追記します。ファイルが無ければ初回だけ新規作成し、それ以後は既存ファイルへ追記します。

## Format

```text
[12345 ms] wifi_connection: Wi-Fi connection connect failed: ESP_FAIL
```

- 時刻は boot からの経過ミリ秒
- `tag` は呼び出し元 component 名
- `message` は呼び出し側が決める固定文言

## Notes

- 保存先は現状 `/sdcard/error.log` 固定
- 行単位の text log です
- SD mount 失敗中や file 作成失敗時は、serial へ warning を 1 回だけ出して以後は黙って無視します

English supplement: This helper intentionally uses a root-level filename to avoid hidden directory-creation behavior.
