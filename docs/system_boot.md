# System Boot

## Overview

`components/framework/system_boot` は、どの製品でも共通の起動処理です。NVS、ステータス LED、スピーカー、画面、配色テーマ、入力を初期化し、起動時のタッチ長押し（設定を開くショートカット）を判定して、結果を `system_boot_result_t` で返します。

ショートカットの意味（どの app を開くか）は製品の composition が決めます。時計製品では settings app を開きます（[CYD Clock Composition](cyd_clock_composition.md)）。

English supplement: `system_boot` only detects the held panel and reports `setup_shortcut_requested`. What the shortcut opens is a product decision made by the composition.

## Boot Sequence

```text
system_boot_start()
  1. 早期判定 (100ms)        xpt2046_softspi_init() → パネルを SPI で読む
  2. NVS / LED / speaker / display 初期化、配色テーマの読み込み
  3. 早期判定で押下あり → 案内画面を出す
  4. cyd_input_init()
  5. 早期判定で押下なし → 入力後の判定 (1200ms)。押下あり → 案内画面を出す
  6. 押下あり → 指が離れるのを待つ (最大 10 秒)
  7. 保存済みのタッチ補正が無ければ補正を実行
  8. 押下ありなら、溜まった入力イベントを捨てる
```

押していない起動で待つ時間は、早期判定の 100ms と入力後の判定の 1200ms だけで、以前と同じです。

## Touch Shortcut

### Detection

判定は 2 回あります。

- 早期判定: 画面の初期化より前。最初に `xpt2046_softspi_init()` を呼び、PENIRQ を有効にするコマンドを送ってから（[PENIRQ After A Warm Reset](#penirq-after-a-warm-reset)）、`xpt2046_softspi_get_raw()` で 10ms ごとに読む。PENIRQ が High の間は SPI を使わずに「押されていない」と返るので、押していない起動では軽い。PENIRQ が Low のときは SPI で座標と圧力を読んで確かめるので、PENIRQ の線が Low に張り付いていても押下とは数えない
- 入力後の判定: `cyd_input_init()` の後。電源を入れてから少し遅れて指を置いた場合を拾う。`cyd_input_get_touch_state()` を読む

どちらも「押されているか」を見るだけで、指が離れるのは待ちません。

English contract: before `cyd_input_init()` system_boot may read the XPT2046 directly; after it, only through `cyd_input`. The input task owns the software SPI from then on, and two tasks driving it would corrupt each other's frames.

### Prompt Screen

長押しを認識したら、画面の初期化が済みしだい、次の案内画面を出します。

| 行 | 文言 | 書体・色 |
|---|---|---|
| 1 行目 | 設定を開きます | 16px 標準、`CYD_UI_THEME_SUBTEXT` |
| 2 行目 | 指を離してください | 24px 太字、`CYD_UI_THEME_INFO` |

- ユーザーがすべきこと（指を離す）を大きく、理由（設定が開く）を小さく書く。ユーザーは画面が変わるのを待って押し続けているので、まず手を離してもらうことを優先する
- ボタンは置かない。指はまだ画面に触れているので、ボタンがあると離した瞬間に押されてしまう
- この画面は、最初の app（settings）が自分の画面を描くまで残る。Wi-Fi などの起動処理の間も「設定を開きます」のまま
- 画面の組み立ては `system_boot_view.c` に分けてあり、シミュレーター（`boot_shortcut`）とホストテスト（`test/host/test_system_boot_view.c`）で確認できる

タイミング:

- 早期判定で見つけた場合: `cyd_display_init()` と `cyd_ui_theme_load()` の直後、`cyd_input_init()` より前に出す。テーマを読んでから出すので、ユーザーが選んだ配色になる
- 入力後の判定で見つけた場合: 見つけた直後に出す（画面はもう初期化済み）

保存済みのタッチ補正が無い端末では、指を離した後にタッチ補正の画面が続き、その後に設定が開きます。

English supplement: the prompt is shown before the release wait, never after it. Waiting first is what left the screen dark for up to 10 seconds on 2026-10-12 (detected at 1.15s, display initialised at 11.2s).

### Release Wait

案内画面を出した後、`cyd_input_init()` の後で、指が離れるのを最大 10 秒（`SYSTEM_BOOT_TOUCH_SETUP_RELEASE_WAIT_MS`）待ちます。10 秒たっても押されたままなら、警告を出して起動を続けます。

- 「押されている」とは、`cyd_input` が押下を報告しているか、PENIRQ が Low であること。`cyd_input_init()` の直後は、入力タスクがまだ押下と判定していないことがあるので、PENIRQ も見る
- 5 回続けて（50ms）押されていなければ、離れたとみなす。変換中の XPT2046 は PENIRQ を駆動しないので、1 回の High だけでは決めない
- 待っている間、入力イベントキューを毎回空にする。押し続けている間も入力タスクは押下・長押しのイベントを積むが、まだ誰も読まないので、以前は `input event queue full` の警告が出ていた

### Logs

| ログ | 意味 |
|---|---|
| `early shortcut detected: panel pressed at raw (x,y) after N samples` | 早期判定で押下を見つけた |
| `early shortcut not detected: ... (touch IRQ gpio=36 low in N)` | 早期判定で見つからなかった。`low in` が 0 より大きいのに見つからないなら、PENIRQ は Low だが SPI では押されていない |
| `post-input shortcut detected: ...` / `not detected: ...` | 入力後の判定の結果 |
| `shortcut prompt shown; waiting for release` | 案内画面を出した |
| `shortcut touch released after Nms` | 指が離れた |
| `shortcut touch still held after 10000ms; continuing` | 10 秒待っても離れなかった |

## PENIRQ After A Warm Reset

XPT2046 は ESP32 と一緒にはリセットされません。書き込み、リセットボタン、モニタからのリセットは ESP32 だけを再起動し、XPT2046 は最後に受け取ったコマンドの power-down ビット（PD1、PD0）を持ち続けます。そのコマンドが PD0=1（PENIRQ 無効）だと、PENIRQ は High のままになります。

`xpt2046_softspi_get_raw()` は PENIRQ が High の間は SPI で何も送らないので、PENIRQ を戻すコマンドを送る機会がなく、電源を切るまでタッチが効かなくなっていました（2026-10-12、CYD で確認）。ESP32 のリセットは、例えば読み取りの途中（PD0=1 のコマンドを送った直後）でも起こりえます。

そのため `xpt2046_softspi_init()` は、直前の状態に関係なく、PENIRQ を有効にするコマンドを 1 回送ります。

- コマンドは `0x80`（S=1、A2-A0=000、12 bit、差動、PD1=PD0=0）
- これは `xpt2046_softspi_get_raw()` が毎回の読み取りの最後に送っているバイトと同じ。通常の動作で 1 秒に何度も受け取っているので、初期化で 1 回送っても、普段と違う状態にはならない。PD1=PD0=0 は XPT2046 の電源投入時の状態でもある
- 変換結果は意味がないので捨てる。後ろに 2 バイト（0x00）を続けて 24 クロックのフレームを最後まで送り、変換を終えさせてから CS を High にする
- 送る前に CS を High にしておくので、ESP32 がフレームの途中でリセットされていても、新しい CS の立ち下がりからコマンドが始まる
- 初期化のログに、送る前後の PENIRQ のレベルを出す（`PENIRQ enabled (INT level before=1 after=0)`）。押したまま再起動して `before=1 after=0` なら、まさにこの状態から戻したことを示す

English contract: the PENIRQ-enable command in `xpt2046_softspi_init()` is unconditional. It must not be gated on the PENIRQ level, because a high PENIRQ is exactly the symptom it repairs.

## Device Verification

実機（CYD）での確認項目:

- 書き込みの直後、電源を入れ直さずにタップが効くこと
- リセットボタン・モニタからのリセットの後にタップが効くこと
- 画面を押したまま起動（電源投入・リセットボタンの両方）すると、すぐに案内画面が出て、指を離すと設定が開くこと
- 電源を入れてから少し遅れて押した場合も、案内画面が出て設定が開くこと
- 押していない起動の時間が変わらないこと
- 押し続けている間に `input event queue full` が出ないこと
