# SD Card Storage

このコンポーネントは、SPI 接続の SD カードを FAT filesystem として `/sdcard` に mount します。
mount 後の read/write は、通常の POSIX / C stdio API で扱う前提です。

English supplement: `sd_card_storage` owns SPI bus setup and FAT mount. Application code should treat `/sdcard` as the stable mount point instead of reinitializing SDSPI directly.

## Ownership

- owner: `sd_card_storage`
- transport: ESP-IDF `SDSPI_HOST_DEFAULT()`
- mount point: `/sdcard`
- who mounts it: [SD Card Status](sd_card_status.md), started by the product composition. It also unmounts, retries and watches the card, so a product does not call `sd_card_storage_init()` itself

## Default Wiring

デフォルトでは SD カードを `SPI3_HOST` に割り当てます。
このリポジトリの現行 `sdkconfig` では TFT が `SPI2_HOST` を使っているため、touch を software SPI に逃がした構成なら VSPI 側を SD に専有させる意図です。

- host: `CONFIG_SD_CARD_STORAGE_SPI_HOST=3`
- `SCLK`: GPIO18
- `MOSI`: GPIO23
- `MISO`: GPIO19
- `CS`: GPIO5

English supplement: If your board wiring differs, change the `CONFIG_SD_CARD_STORAGE_*` values instead of hardcoding pins in application code.

## Usage

mount は [SD Card Status](sd_card_status.md) が行います。product の composition もアプリも、直接は呼びません。
mount 後は `/sdcard` 配下を通常ファイルとして扱えます。

```c
FILE *fp = fopen("/sdcard/example.txt", "w");
if (fp != NULL) {
    fputs("hello sd\n", fp);
    fclose(fp);
}
```

`sd_card_storage_is_mounted()` で mount 状態を確認できます。

## Public API

- `sd_card_storage_init()`: mount します。失敗しても何も確保したまま残らないので、カードを後から挿した場合などに、もう一度呼べます
- `sd_card_storage_deinit()`: unmount して SPI bus を返します。開いているファイルは先に閉じてください
- `sd_card_storage_probe()`: カードの先頭セクタを読んで、応答するかを確かめます。ファイルシステムを通さないのは、FATFS の cache が、カードを抜いた後も成功を返し続けるためです
- `sd_card_storage_get_space()`: 容量と空きを返します
- `sd_card_storage_is_mounted()` / `sd_card_storage_get_mount_point()`

`sd_card_storage_init()` の失敗コードは、どこまで進んだかを表します。

- `ESP_FAIL`: カードは応答したが、ファイルシステムを mount できなかった（未フォーマット、FAT 以外、読めない）
- それ以外の `ESP_ERR_TIMEOUT` など: カード自体を立ち上げられなかった（たいてい未挿入）。`ESP_ERR_NO_MEM`・`ESP_ERR_INVALID_STATE` は、ホスト側の失敗です

English contract: `init` / `deinit` / `probe` / `get_space` have no locking and must be called from one task. `get_space()` can take seconds on the first call for a FAT32 card whose info sector is unset, because it has to scan the FAT.

ログのように継続して書くデータは、直接 `fopen()` せず [SD Card Writer](sd_card_writer.md) の stream を使ってください。書き込みを1つの task にまとめ、呼び出し元が SD を待たないようにしています。

## Memory

`sdkconfig.defaults` は `CONFIG_FATFS_USE_DYN_BUFFERS=y` にしています。

- オフ (ESP-IDF の既定): FatFs は、一番大きいセクタ (wear levelling の 4096 バイト) に合わせたバッファを、ボリュームごとに 1 つ、開けるファイルの枠 (`CONFIG_SD_CARD_STORAGE_MOUNT_MAX_FILES`、今は 5) ごとに 1 つ、mount のときから持ち続ける。SD カードのセクタは 512 バイトなのに、4KB を約 6 つ持つことになる
- オン: バッファはボリュームのセクタの大きさで取られ、ファイルの分は開いているあいだだけ取られる

派生の cyd_time_puncher の監視端末では、オフのとき `sd_card_status` のタスクがヒープを 43.8KB 持ち続け、最初の TLS 接続が証明書の確認の途中でヒープ不足になり失敗しました (接続前の空きヒープ: オフ 13.7KB、オン 38.7KB。2026-10-24)。

**既にある `sdkconfig` には自動では入りません。** ESP-IDF は、`sdkconfig` が無いときにだけ `sdkconfig.defaults` を読みます。`idf.py fullclean` も `build/` を消すだけです。手元の `sdkconfig` に `# CONFIG_FATFS_USE_DYN_BUFFERS is not set` とあれば、次のどちらかで有効にしてください。

- `./scripts/idf-menuconfig.sh` で Component config → FAT Filesystem support → Use dynamic buffers をオンにする
- 必要なローカルの設定を控えてから、プロジェクト直下の `sdkconfig` を消して、作り直す

確かめるには: `grep FATFS_USE_DYN_BUFFERS sdkconfig` が `CONFIG_FATFS_USE_DYN_BUFFERS=y` を返すこと。

English supplement: this is a heap setting, so `esp_idf_size` (static DRAM) does not show it; compare free heap on the device. Each open file costs one 512-byte buffer while it is open.

## Notes

- FAT format を前提とし、mount 失敗時の自動 format は行いません。カードの中身を、黙って消さないためです。exFAT（32 GB 超のカードに多い）も扱えません
- bus 共有の抽象化はまだ入れていないため、現状は SD が専用 SPI host を持つ前提です
- TFT を `SPI3_HOST` に切り替える場合は、SD 側 host を `SPI2_HOST` へ振り替えるか、共有設計を別途追加してください

English supplement: The current design intentionally avoids shared-bus arbitration. Keep SD and TFT on separate SPI hosts unless you add an explicit shared-bus policy.
