# SD Card Files

このコンポーネントは、`sd_card_storage` が mount した `/sdcard` 上へ、
アプリ側から相対パス指定で明示的な file write policy を使うための薄い helper です。

English supplement: `sd_card_files` is intentionally small. It hides mount-point handling and explicit create/append/overwrite checks, but it does not own buffering, file rotation, or record schemas.

呼び出し元の task で、1回ごとに open から close まで同期実行します。設定の保存のような、まれな単発の書き込み向けです。ログのように継続して書くデータは [SD Card Writer](sd_card_writer.md) を使ってください。

English supplement: every call opens, writes, flushes and closes on the caller's task. Use it for rare one-off writes; continuous data belongs in sd_card_writer.

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
- FATFS は ESP-IDF 既定の `CONFIG_FATFS_LFN_NONE` なので、path の各要素は 8.3 形式（拡張子の前 8 文字以内、拡張子 3 文字以内）に収めてください。長い名前は `fopen()` が失敗します。長い filename が必要なら `CONFIG_FATFS_LFN_HEAP` などを有効にします
- binary の `size == 0` は空ファイル作成や truncate を含む明示操作として許可します
- seek や read-modify-write をしたい場合は従来どおり `fopen()` 系を直接使ってください
