# SD Card Storage

このコンポーネントは、SPI 接続の SD カードを FAT filesystem として `/sdcard` に mount します。
現状は product composition から初期化され、以後の read/write は通常の POSIX / C stdio API で扱う前提です。

English supplement: `sd_card_storage` owns SPI bus setup and FAT mount. Application code should treat `/sdcard` as the stable mount point instead of reinitializing SDSPI directly.

## Ownership

- owner: `sd_card_storage`
- transport: ESP-IDF `SDSPI_HOST_DEFAULT()`
- mount point: `/sdcard`
- current init path: `cyd_clock_composition_start()`

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

初期化は composition 側で一度だけ行います。
mount 後は `/sdcard` 配下を通常ファイルとして扱えます。

```c
FILE *fp = fopen("/sdcard/example.txt", "w");
if (fp != NULL) {
    fputs("hello sd\n", fp);
    fclose(fp);
}
```

`sd_card_storage_is_mounted()` で mount 状態を確認できます。

## Notes

- FAT format を前提とし、mount 失敗時の自動 format は行いません
- bus 共有の抽象化はまだ入れていないため、現状は SD が専用 SPI host を持つ前提です
- TFT を `SPI3_HOST` に切り替える場合は、SD 側 host を `SPI2_HOST` へ振り替えるか、共有設計を別途追加してください

English supplement: The current design intentionally avoids shared-bus arbitration. Keep SD and TFT on separate SPI hosts unless you add an explicit shared-bus policy.
