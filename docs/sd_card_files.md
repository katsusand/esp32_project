# SD Card Files

このコンポーネントは、`sd_card_storage` が mount した `/sdcard` 上へ、
アプリ側から相対パス指定で明示的な file write policy を使うための薄い helper です。

English supplement: `sd_card_files` is intentionally small. It hides mount-point handling and explicit create/append/overwrite checks, but it does not own buffering, file rotation, or record schemas.

## Public API

- `sd_card_files_write_new_text(relative_path, text)`
- `sd_card_files_append_existing_text(relative_path, text)`
- `sd_card_files_overwrite_text(relative_path, text)`
- `sd_card_files_write_new_binary(relative_path, data, size)`
- `sd_card_files_append_existing_binary(relative_path, data, size)`
- `sd_card_files_overwrite_binary(relative_path, data, size)`

どちらも `relative_path` は `/sdcard` からの相対パスです。
先頭の `/` は付いていても剥がして扱います。

```c
ESP_ERROR_CHECK(sd_card_files_write_new_text("logs/boot.txt", "boot ok\n"));
```

```c
uint8_t packet[] = {0x01, 0x02, 0xA5, 0xFF};
ESP_ERROR_CHECK(sd_card_files_append_existing_binary("logs/raw.bin", packet, sizeof(packet)));
```

## Behavior

- `write_new_*`: ファイルが未存在のときだけ成功。既存ならエラー
- `append_existing_*`: 既存ファイルにだけ追記。未存在ならエラー
- `overwrite_*`: 既存なら truncate、未存在なら新規作成
- 書き込み後に `fflush()` と `fclose()` を実行
- mount point は `sd_card_storage_get_mount_point()` から取得

English supplement: This API avoids implicit file creation during append and avoids accidental overwrite during create-new.

## Notes

- 親ディレクトリの自動作成はまだ行いません
- binary の `size == 0` は空ファイル作成や truncate を含む明示操作として許可します
- seek や read-modify-write をしたい場合は従来どおり `fopen()` 系を直接使ってください
