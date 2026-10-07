# Boot ID

`boot_id` は、この起動を表す UUID を返します。起動ごとに 1 回だけ作られ、次にリセットされるまで、どの task から呼んでも同じ値です。

English supplement: a random UUID (version 4 form) made once per boot. It says which log files and lines belong to one run of the firmware; it is unique, not secret, and it does not order boots.

## Usage

```c
#include "boot_id.h"

const char *id = boot_id_get();   /* "3f2b8c1e-5d4a-4e7b-9a1c-0b2d3e4f5a6b" */
```

- 36 文字の小文字（`xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx`）で、NUL で終わります
- 返るポインタは起動中ずっと有効です。どの task からも呼べます
- ISR からは呼べません

主な使い手は [Error Log Store](error_log_store.md) で、ファイルを開くたびに書く `boot:` 行に入れます。遅れて時計が合ったり、サイズ上限や日付の変更でファイルが分かれても、同じ `id` のファイルは同じ起動の記録だと分かります。

## Design Notes

- **UUID は順序を表しません。** 「同じ起動か」が分かるだけで、どちらが先かは分かりません。起動の前後は、日付とファイルの番号で決まります
- **作るのは、最初に使うときです。** ハードウェア乱数は、Wi-Fi か Bluetooth が動いているときだけ真の乱数になります。起動直後に作ると、品質が落ちます
- **MAC アドレスと起動後の時間を混ぜます。** 乱数が弱くても、端末どうし、起動どうしで値が重ならないようにするためです。混ぜるのは乱数との XOR なので、乱数が良ければ、その良さは失われません
- **秘密ではありません。** ログに平文で書き、認証には使いません

English supplement: the UUID is made on first use because the hardware RNG is only truly random with Wi-Fi or Bluetooth running. The MAC address and the uptime are XORed in, so two units, or two boots of one unit, differ even when the RNG is weak, and a good RNG loses nothing.

## Tests

```bash
bash test/host/run.sh
```

`test_boot_id_format.c` が、UUID の形（バージョンと variant のビット、ハイフンの位置）と、乱数が同じでも MAC や時間が違えば値が違うことを確かめます。
