# SD Card Status

このコンポーネントは、SD カードの状態（未挿入・未フォーマット・空き容量なし・異常）を判定し、カードの抜き差しから自動で復帰させます。SD カード上のエラーログ（[Error Log Store](error_log_store.md)）は、このコンポーネントが開始・停止します。

English supplement: `sd_card_status` owns the whole life cycle of the card and of the error log stream on top of it: mount, check, release, mount again. The card is optional. A missing or broken card is a state to show, never a reason to fail the boot or to stop the main app.

## Why It Exists

SD カードは、デバッグのためにカードを抜いて PC でログを読む使い方を前提にしています。この基板にはカード検出ピンが無く、抜き差しを割り込みで知る手段がありません。そのため、次のことをこのコンポーネントが定期的な確認で補います。

- 起動時にカードが無い、または未フォーマットでも、起動を止めずに状態として表示する
- カードを後から挿した、PC で読んで戻した、フォーマットし直した、というときに再起動なしで使い始める
- 空き容量が尽きる前にログを止め、書き込みエラーの連鎖を避ける

English supplement: there is no card detect pin on this board, so presence is learned by polling. Without this component a card that is put back after being read on a PC would stay unused until the next reboot, because the error log stream does not recover once it has failed.

## States

| 状態 | 意味 | 画面のアイコン | ログ |
|---|---|---|---|
| `DISABLED` | 起動直後でまだ判定していない、または SD 対応なしのビルド | なし | 書かない |
| `OK` | マウント済みで、書き込みも確認済み | なし | 書く |
| `NO_CARD` | 未挿入、または応答しない | グレーのカードに赤い斜線 | 書かない |
| `NO_FILESYSTEM` | カードは応答するが、ファイルシステムをマウントできない（未フォーマット、FAT 以外、exFAT など） | 黄色のカードに `?` | 書かない |
| `FULL` | 空きが `CONFIG_SD_CARD_STATUS_MIN_FREE_KB` を下回った | オレンジのカードに満杯のゲージ | 止める |
| `FAULT` | マウントできたが書き込みを受け付けない、ログの書き込みが失敗した、SPI bus を確保できないなど | 赤いカードに `!` | 書かない |

アイコンは `sd_card_status_icon_for_state()` が返します。時計アプリ（`cyd_clock_app`）は、時計画面の右上（列 38・行 0）に出します。`OK` と `DISABLED` では何も描きません。

English supplement: the exact meaning of each state, as the code decides it. `NO_CARD` is any failure to bring the card itself up (usually an empty slot, which looks like a timeout). `NO_FILESYSTEM` is exactly "the card answered and `f_mount()` failed": the card is never formatted automatically. `FAULT` is everything else, including host-side failures, which must not read as "insert a card".

## Life Cycle

1. 起動すると、このコンポーネントのタスクがすぐにマウントを試します
2. 成功したら、テストの書き込みをして、エラーログを開始します
3. マウント中は `CONFIG_SD_CARD_STATUS_POLL_INTERVAL_MS` ごとに、次の 3 つを確認します
    - カードが応答しなくなった → `NO_CARD`
    - エラーログの書き込みが失敗した → `FAULT`
    - 空きが下限を下回った → `FULL`（マウントは維持）
4. `NO_CARD` と `FAULT` では、ログを閉じてカードを解放し、後で再度マウントします。失敗したマウント（未挿入・未フォーマット）も、後で再度試します

- マウント後は、テスト用ファイル `SDCHK.TMP`（512 バイト）を書いて消し、書き込めることを確認してから使い始めます。write-protect、劣化、満杯のカードは、マウントには成功して最初の書き込みで初めて失敗するためです
- 応答の確認は、カードの先頭セクタを直接読んで行います。ファイルシステムの cache は、カードを抜いた後も成功を返し続けるためです
- 解放は `sd_card_storage_deinit()` で行います。SPI bus も返すので、同じカードでも別のカードでも、そのまま再度マウントできます
- 再試行の間隔は、状況で 2 通りです
    - 起動後に一度でもカードが動いた個体で、`NO_CARD` と `NO_FILESYSTEM` のとき: 固定で `CONFIG_SD_CARD_STATUS_RETRY_SEEN_MS`（3 秒）。カードを抜いて PC で読み、戻す、という使い方で、戻してから気づくまでを短くするためです
    - 一度もカードを見ていない個体と、書き込み失敗（`FAULT`）のとき: 5 秒から始まり、10・20・40 秒と伸びて 60 秒で止まります。カードを使わない個体や、再マウントしても書けないカードが、SPI bus とシリアルログを使い続けないためです。正常だった時間が 1 分以上あれば、次の失敗は 5 秒から始めます
- シリアルログは、最初の失敗だけを詳しく出します。ESP-IDF のドライバ（`vfs_fat_sdmmc`・`sdmmc_*`・`sdspi_*`）は失敗のたびにエラーを出し、GPIO ドライバ（`gpio`）は SDSPI の初期化と解放のたびにピンごとの `GPIO[n]|` 行を出します。最初の失敗では理由が分かるように残し、繰り返しの再マウント、カードの解放、抜去を調べる応答確認では、これらのタグを一時的に止めて、終わったら元に戻します。抜去の理由は、このコンポーネントの警告に含めます（例: `SD card OK -> NO_CARD: card stopped answering (ESP_ERR_TIMEOUT)`）

English supplement: every failure path ends the same way: close the error log, release the card, wait, mount again. A card that was working and is now out is retried at a short fixed interval, because somebody is waiting for the icon. A unit that never had a card, or a card that will not take writes, gets a growing wait so it cannot turn this task into a mount loop.

### FULL

`FULL` ではカードをマウントしたままにします。ログだけを止めるので、カードの中身はそのまま PC で読めます。

- 空きが下限を下回った時点で、「FULL になった」という行を最後に書いてから、ログを閉じます
- テスト用ファイルすら書けなかったときは、カードが解放されるまで `FULL` に固定します。FATFS の空き容量は FAT32 の情報セクタに由来する cache で、古いことがあります。書き込みが失敗した直後にそれを信じると、`OK` に戻って、ログを再開して、最初の 1 行で失敗する、という往復になります
- 空きを作る方法は、カードを PC で整理して戻すことです。カードが抜かれると `NO_CARD` を経由して再マウントされ、空き容量は測り直されます

English supplement: a full card stays mounted so it can still be read; only logging stops, before it can run into a write error. A failed test write pins the state to FULL until the card is released, because the cached free-space count may still claim room.

## Error Log Files

エラーログのファイルは、最初の 1 行を書くときに作られます。エラーのない起動はファイルを作りません。そのため、このコンポーネントは起動直後の最初のマウントをログに書きません。書くと、起動のたびに `ERR_xxxx.LOG` が 1 つ増えます。

ログに残るのは、カードが一度確認された後の変化だけです。

```text
[123456 ms] sd_card_status: SD card OK -> FULL: 512 KB free; logging stopped
[789012 ms] sd_card_status: SD card NO_CARD -> OK: mounted, logging
```

復帰のたびに新しいログファイル（次の番号）が使われます。抜いていた間の空白は、その行から読み取れます。

English supplement: the first mount is deliberately silent (serial only). Only a change after the card has been seen is written to the card: it filled up, or it came back.

## Public API

- `sd_card_status_start()`
- `sd_card_status_get_state()`
- `sd_card_status_get_revision()`
- `sd_card_status_get_info(info)`
- `sd_card_status_state_name(state)`
- `sd_card_status_state_is_problem(state)`

`sd_card_status_start()` はすぐに戻ります。最初のマウントはこのコンポーネントのタスクが行うので、カードの無い個体でも起動は待たされません。戻り値のエラーはタスクを作れなかったときだけで、カードの問題は状態として読みます。

`sd_card_status_get_revision()` は状態が変わるたびに増えます。画面は、描いたときの revision を覚えておき、違っていれば描き直します。

English contract: call `sd_card_status_start()` once, after system boot and before anything whose errors should reach the card (Wi-Fi, time sync). Nothing else may call `sd_card_storage_init()` or `error_log_store_start()`: this component is the single owner of the card, and two owners would unmount each other's card.

## Configuration

| Kconfig | 既定値 | 意味 |
|---|---|---|
| `SD_CARD_STATUS_POLL_INTERVAL_MS` | 5000 | マウント中の確認間隔 |
| `SD_CARD_STATUS_RETRY_SEEN_MS` | 3000 | 一度カードが動いた個体の、未挿入・未フォーマット中の再試行間隔（固定） |
| `SD_CARD_STATUS_RETRY_INITIAL_MS` | 5000 | それ以外の失敗で、最初に再マウントを試すまでの時間 |
| `SD_CARD_STATUS_RETRY_MAX_MS` | 60000 | 再試行の間隔の上限 |
| `SD_CARD_STATUS_MIN_FREE_KB` | 1024 | これを下回ったら `FULL`。0 は完全に空きが無いときだけ |
| `SD_CARD_STATUS_TASK_STACK_SIZE` | 6144 | マウントとカード情報の printf がこのタスクで走る |
| `SD_CARD_STATUS_TASK_PRIORITY` | 3 | |
| `SD_CARD_STATUS_SHOW_ICON` | y | `sd_card_status_icon_for_state()` がアイコンを返すか。無効にすると、どの app にも出ない |

SD カードを使わない個体は、`NO_CARD` のアイコンが出続けます。その場合は `SD_CARD_STATUS_SHOW_ICON` を無効にしてください。ログは、このスイッチと関係なく動きます。

English supplement: units that run without a card on purpose will show the NO_CARD icon for good. Turn `SD_CARD_STATUS_SHOW_ICON` off for them; it only hides the icon.

## Debugging With The Card

1. 通常どおり運用します。エラーは `ERR_0000.LOG` のようなファイルに、1 行ずつ追記されます
2. カードを抜くと、数秒以内に画面の右上へ `NO_CARD` のアイコンが出ます。PC でログを読みます
3. カードを戻すと、次の再試行（3 秒ごと）で使い始めます。アイコンが消えて、`SD card NO_CARD -> OK` が新しいログファイルの 1 行目になります
4. アイコンが `?` のとき、カードは FAT（FAT32 が確実）でフォーマットし直してください。32 GB を超えるカードは exFAT になりがちで、このファームウェアは exFAT を扱えません

English supplement: a card pulled and put back is the intended workflow, not an error. exFAT is not supported (`FF_FS_EXFAT` is 0); cards above 32 GB usually need to be formatted as FAT32 by hand.

## Tests

状態の判断と、状態遷移そのものはホストで検証しています。

```bash
bash test/host/run.sh
```

- `test_sd_card_status_logic.c`: マウントエラーの分類、書き込み結果の分類、満杯の判定、再試行間隔
- `test_sd_card_status_machine.c`: 実際の `sd_card_status.c` を、偽のカード・偽のエラーログ・手で進める時計で動かします。起動時にカードなし、未フォーマット、抜き差し、満杯、書き込み不可、ログの失敗、間隔の伸び方、を通します
- `test_sd_card_status_icon_art.c`: アイコンの絵の形（16 行 × 16 文字、未定義の記号が無いこと、種類ごとに違うこと）

English supplement: these cover the decisions and the state machine. What they cannot cover is the card itself: the SDSPI driver, a real card's response when it is pulled, and the display. Check those on a unit.
