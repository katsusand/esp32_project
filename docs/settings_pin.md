# Settings PIN

## Overview

設定に PIN の鍵をかける仕組みです。実装は `components/apps/cyd_system_apps` の `settings_pin_*.c`、公開 API は `include/settings_pin.h` です。

社員の目に触れる場所に置く端末（派生プロジェクトの打刻端末・監視端末）で、通りがかりの人が設定に入り、Wi-Fi やサーバーの接続先を書き換えるのを防ぎます。監視端末を止めずに設定できるよう、電源の入れ直しではなく PIN で入口を守ります。

- 既定はオフ。オフのあいだは何も変わらない（画面も遷移も今までどおり。cyd_sim の既存シーンの書き出しはバイト単位で同じ）
- オンにすると、設定の画面に入るすべての入口で PIN の画面が先に出る
- PIN は 6〜8 桁の数字。NVS には端末ごとの塩をつけたハッシュだけを置く
- 間違いが続くと待ち時間が延び、電源を入れ直しても続く
- 忘れたときは画面の操作だけでは戻せない。PC から USB で NVS を消す（[Forgotten PIN](#forgotten-pin)）

English contract: while the PIN is off, the enter guard lets every switch through unchanged. While it is on, no guarded app is entered without the PIN, whoever requests the switch and however the device booted. The PIN never appears in clear in flash or in the log.

## Guarded Apps

鍵は `app_shell` の enter guard (`app_shell_set_enter_guard()`) で掛けます。アプリの切り替えはすべて、起動直後の最初のアプリも含めて guard を通るので、どこから `app_shell_switch_to()` されても同じ扱いになります。設定アプリの中に鍵の判定を書くと、時計のように Wi-Fi 設定へ直接切り替えるアプリが抜け道になるためです。

| 守る画面 | 入り方の例 |
|---|---|
| 設定 (`system_settings_app_get_app()`) | 時計の設定ボタン、起動時の長押し、`system_settings_open_*()` からの直接表示 |
| Wi-Fi の設定 (`cyd_wifi_setup_get_app()`) | 設定の「Wi-Fi を設定する」、時計の「Wi-Fi に接続できません」画面のボタン |
| 各アプリの設定画面 (`app_registry` の `settings_app`) | 設定の「アプリ」ページ |
| システム情報 (`system_info_app_get_app()`) | 「情報も保護」をオンにしたときだけ |

起動時の長押しは、composition が最初のアプリを settings にするだけです。最初のアプリも guard を通るので、長押しで PIN を飛ばすことはできません。そのとき PIN の画面の「戻る」には戻る先が無い（settings が home app でもある）ので、端末を再起動して通常の起動に戻ります。

タッチ補正 (`touch_calibration`) は守りません。設定から開いたときは「設定の内側」として扱い、戻ってきても PIN を聞き直しません。

English contract: "inside" is the guarded apps plus the screens they open that guard nothing themselves (touch calibration, the PIN screen, the recovery provider's app). Moving inside keeps the PIN entered; entering any app outside closes it.

## While Unlocked

PIN が通ると、次のどちらかまで開いたままです。

- 設定の内側から外のアプリへ移る（「戻る」で時計へ戻る、など）
- 無操作の時間が「無操作で戻る」を超える

次に入るときは、また PIN を聞きます。

起動時の長押しで入った端末では settings が home app なので、`app_shell` の無操作の復帰は働きません（戻る先が自分）。この場合は settings 自身が、無操作の時間が過ぎたら鍵を閉じて PIN の画面を出します (`settings_pin_relock_if_idle()`)。settings が home app でないときも、home への復帰が止められている（`app_shell_set_home_return_allowed_callback()` が false を返す）と同じことが起きるので、settings はそのあと 1 秒待って、まだ自分が出ていれば鍵を閉じます。復帰できる普通の場合は、その 1 秒の間に shell が home へ戻すので、PIN の画面は一瞬も出ません。「無操作で戻る」が「しない」のときは、外へ出るまで開いたままです。

## Forced Initialize

NVS に読めないデータがあり、起動時に「保存データが読めません」の画面へ強制で入る流れ (`nvs_health_requires_initialize()` → `system_settings_open_clear_nvs_confirm()`) は、**PIN を聞かずに通します**。

- この画面には「やめる」も「戻る」も無く、できるのは `nvs_flash_erase()` で全部消して再起動することだけ
- 全部消すと PIN も消える。PIN を飛ばしても、設定のページには入れない
- 通すのは「NVS の異常が報告されていて、かつ強制の初期化が予約されている」ときだけ (`system_settings_forced_initialize_armed()`)。普通に設定へ入るときは、NVS に異常があっても PIN を聞く

PIN の記録そのものが読めない（大きさや version が違う）ときは「鍵が掛かっていて確かめられない」として扱います。守る画面へ入ろうとすると、同じ強制の初期化の画面へ回します。PIN が読めないことを「PIN が無い」と読み替えて開けることはしません。

初期化のあとの端末は、初めて電源を入れた端末と同じで、誰でも設定できます。NVS を書き換えられる人（USB を挿せる人）だけが起こせる状態なので、これは受け入れています。

English contract: the forced Initialize NVS flow is the one way past the PIN without entering it, because its only action erases the PIN together with everything else. An unreadable PIN record fails closed into that same flow.

## Wrong PINs

間違いが続いたら待ち時間を入れます。

| 続けて間違えた回数 | 待ち時間 |
|---|---|
| 1〜4 | なし |
| 5 | 30 秒 |
| 6 | 1 分 |
| 7 | 2 分 |
| 以後 | 倍々 |
| 12 以上 | 1 時間 (上限) |

- 回数は NVS (`sys_pin` / `fails`) に持つ。電源を入れ直しても回数は消えず、待ち時間は**最初からやり直し**になる（時計が合っている保証が無いので、残り時間は持たない。入れ直しで短くなることはない）
- 回数は**照合の前に**1 つ増やして保存する。間違えた直後に電源を切っても、その 1 回は取り消されない
- 正しい PIN で 0 に戻る
- 待ち時間のあいだはキーが押せない。「戻る」と、下の解除のボタンは押せる

1 時間で頭打ちにすると、6 桁を総当たりするには平均で 50 年以上かかります。4 桁だと平均で約 7 か月です。これが 4 桁を許さない理由です。

## Digits

6〜8 桁の数字です (`SETTINGS_PIN_MIN_DIGITS` / `SETTINGS_PIN_MAX_DIGITS`)。

- 最小を 6 にしたのは、上の待ち時間と組み合わせたときの総当たりの時間のため。押す手間は 4 桁とほとんど変わらない
- 入力中も、照合のときも、桁数は画面に出す（伏せ字の点の数）。PIN の桁数そのものは保存しない
- 「決定」は 6 桁から押せる。8 桁を超える入力はできない

## Storage

namespace `sys_pin`（system scope。「アプリのデータを消去」では消えず、「すべて初期化」で消える）。

| key | 型 | 内容 |
|---|---|---|
| `pin_v1` | blob 60 バイト | version (1)、flags (bit0: 情報画面にも PIN)、PBKDF2 の回数、塩 16 バイト、ハッシュ 32 バイト |
| `fails` | u32 | 続けて間違えた回数 |

- `pin_v1` が無いことが「オフ」。オフにすると namespace ごと消す
- ハッシュは PBKDF2-HMAC-SHA256（mbedtls）。回数は 1024 (`SETTINGS_PIN_STORE_ITERATIONS`)。回数は記録ごとに持つので、変えても前の回数で保存した PIN はそのまま照合でき、新しい回数は次に PIN を設定・変更したときから使われる
- 最初は 4096 回にしたが、実機で 1 回 699 ms かかり、「決定」のたびに止まるのが分かったので 1024 回（約 175 ms）に下げた（2026-10-23）。画面からの総当たりを抑えているのは [Wrong PINs](#wrong-pins) の待ち時間で、回数はフラッシュを読み出されたときにしか効かない。6〜8 桁の数字はどちらの回数でも PC で短い時間に破れるので、下げて失うものはほぼ無い
- 塩は PIN を決めるたびに `esp_fill_random()` で作る 16 バイト。同じ PIN でも端末ごとに別のハッシュになる
- ログには PIN もハッシュも出さない。出すのは「オン / オフ」「何回続けて間違えたか」「ハッシュにかかった時間」だけ

NVS は暗号化していないので、フラッシュを読める人はハッシュを取り出して手元で総当たりできます。6〜8 桁の数字では塩とハッシュの回数でも長くは持ちません。PIN が守るのはタッチ画面だけで来る人からであり、USB を挿せる人からは守れません ([Limits](#limits))。

## PIN Screen

`settings_pin_view.c`（model だけから組み立てる view）。

```text
rows 0-3    [戻る] 設定            または [戻る] [解除のボタン]
rows 4-6    PIN を入力してください / PIN が違います / あと30秒お待ちください
rows 7-9    ● ● ● ● ● ●
rows 10-28  [1][2][3] / [4][5][6] / [7][8][9] / [削除][0][決定]
```

- キーは 96×32px、間に 8px。この端末で一番大きいキーで、抵抗膜のパネルを指先で押しても隣に当たらない
- 文字は他の設定画面と同じ 16px。大きくしたのはキーで、文字ではない
- 入力は点だけで表し、数字は画面のどこにも出ない（model に数字そのものを渡さない）
- 新しい PIN は 2 回入れる。2 回目が違えば「一致しませんでした。最初から入力」と出して 1 回目からやり直す。2 回とも同じになって保存できたときだけ PIN が変わる

シミュレーターの `pin_*` と `sys_security*`、`sys_pin_off` のシーンで全状態を見られます。

## Settings Page

設定の「設定のPIN」ページ（「一般」の次。内部の page ID は `SECURITY`）。

| 行 | 内容 |
|---|---|
| PIN機能 | オンにすると新しい PIN を 2 回入れる。オフにするときは確認のあと PIN を消す |
| 情報も保護 | システム情報にも PIN を求める。PIN機能がオフのあいだは見出しごとグレーになり、押せない |
| PIN を変更する | 新しい PIN を 2 回入れる。PIN がオフのあいだは押せない |

「PIN機能」と「情報も保護」はスイッチで、状態はつまみの位置と色で表し、「オン」「オフ」の字は出しません（2026-10-23、実機確認でのユーザーの判断）。行全体が 1 つのボタンなので、行のどこを押しても切り替わります。高さは 40px です。

オン・オフ・変更は、このページ、つまり設定に入れている人だけができます。変更のときに今の PIN を聞き直すことはしません。

このページは既存のページを詰めて場所を作るのではなく、ページを 1 つ足しました。「一般」はすでに一杯で、他のページに足すとそのページの見た目が変わるためです。そのかわりページの数は 1 つ増え（Wi-Fi ありで 7、なしで 5）、下の「n/N」の N が 1 つ大きくなります。既存シーンの書き出しはページ数をシーンの側で固定しているので、変わりません。

## Forgotten PIN

**画面の操作だけでは戻せません。** 戻せると、そこから接続先を書き換えられるためです。

PC から USB で `nvs` パーティションを消します。PIN は Wi-Fi・設定値・タッチ補正などと一緒に未設定へ戻ります。ファームウェアと他のパーティション（`storage`、派生プロジェクトの端末の鍵を置く `devid` など）はそのままです。

```bash
scripts/erase-nvs.sh /dev/cu.usbserial-11410
```

スクリプトは EIM の IDF を読み込み、`parttool.py` で `nvs` という名前のパーティションだけを消します。パーティション表は端末から読むので、派生プロジェクトの `partitions.csv` でも同じ手順で動きます。表の位置は `sdkconfig` の `CONFIG_PARTITION_TABLE_OFFSET`（このリポジトリは `0x9000`）を使います。

スクリプトを使わずに打つ場合:

```bash
source ~/.espressif/tools/activate_idf_v5.5.6.sh
```

```bash
python "$IDF_PATH/components/partition_table/parttool.py" --port /dev/cu.usbserial-11410 --partition-table-offset 0x9000 erase_partition --partition-name nvs
```

消したあと端末を再起動すると、タッチ補正から始まります（初めての起動と同じ）。

### Serial Console

シリアルのコンソールで PIN だけを消す方法は**作りません**（2026-10-23 の判断）。

- 得られるのは「Wi-Fi などの設定を残せる」ことだけ。USB を挿せる人は、コンソールが無くてもファームウェアを書き換えられるので、守れる範囲は変わらない
- 一方で、監視端末のように PC へ USB を挿したまま置く端末では、常に開いている入口が 1 つ増える
- 今の親にはコンソールが無く、作るには UART の受信タスク、コマンドの解釈、誤操作の防止が要る

設定を残したいときは、消す前に設定の値を控えておき、消したあとに入れ直してください。

## Recovery Provider

派生プロジェクトが「PIN を忘れたときの別の解除方法」を差し込むためのフックです。例: 管理画面で発行した使い捨てのコードを端末に入れ、サーバー (device API) で照合して 1 回だけ通す。サーバーが要るので、親には仕組みだけを置きます。

```c
#include "settings_pin.h"

static bool unlock_available(void *ctx)
{
    (void)ctx;
    return device_is_registered() && backend_uses_https();
}

static const settings_pin_recovery_provider_t s_unlock = {
    .label = "管理画面のコードで解除",
    .available = unlock_available,
    .app = &s_unlock_code_app, /* the derived project's own app_shell app */
};

ESP_ERROR_CHECK(settings_pin_set_recovery_provider(&s_unlock, NULL));
```

1. 登録されていて `available()` が true のとき、PIN の画面の上に `label` のボタンが出る。登録が無ければボタンは出ない
2. ボタンで `app` へ切り替わる (`from_app` は PIN の画面)
3. `app` が照合に成功したら `settings_pin_recovery_succeeded()` を呼ぶ。PIN の画面に戻り、新しい PIN を 2 回入れさせる。保存できたときだけ、行こうとしていた画面へ進む
4. `app` で取りやめたら `app_shell_return_to(from_app)` で PIN の画面へ戻る

新しい PIN が保存されるまで、古い PIN はそのまま有効です。新しい PIN の入力を「戻る」でやめると、解除はなかったことになり、古い PIN の入力に戻ります。「PIN が消えて誰でも入れる」時間は作りません。

English contract: the provider struct is not copied (pass a `static const`). `label` is drawn in 16px bold in a 248px header button: up to 14 full-width characters, and only characters the derived project's UI fonts carry (its own string literal is enough; the font build picks it up). `available()` runs on the shell task each time the PIN screen is drawn and must not block. `settings_pin_recovery_succeeded()` is ignored unless the PIN screen is waiting for a PIN; it may be called from any task.

`app` は PIN の画面と同じく「設定の内側」として扱います。`app` にいるあいだに外のアプリへ移ると、解除の途中の状態は捨てます。

## Integrating Into A Product

composition（派生プロジェクトでは `main/`）で、`system_boot_start()` のあと、`app_shell_start()` の前に 1 回呼びます。

```c
cyd_clock_composition_start_optional("settings PIN init failed", settings_pin_init());
cyd_clock_composition_preflight_nvs_health();
if (nvs_health_requires_initialize()) {
    ...
}
```

- NVS の健全性の確認より前に呼ぶ。読めない PIN の記録も、他の壊れたデータと同じく起動時の強制の初期化へ回るため
- 呼び忘れると、settings の `enter()` がそれを見つけてエラーのログを出し、その場で guard を入れて PIN を聞く。ただしそれまでのあいだ、Wi-Fi の設定などへの直接の切り替えは守られない。必ず composition で呼ぶこと
- 解除の手段を足すときは、同じ場所で `settings_pin_set_recovery_provider()` を呼ぶ

派生プロジェクトでの取り込みの手順:

1. upstream を取り込み、`scripts/ui_fonts/merge_upstream.sh` でフォントを作り直す（新しい漢字がある）
2. `main/` の composition に `settings_pin_init()` を足す
3. 解除の手段を作るなら、その app と `settings_pin_set_recovery_provider()` を足す。`label` の文字は派生側の文字列なので、フォントの作り直しで入る

## Limits

- USB を挿せる人からは守れない。ファームウェアの書き換えも NVS の読み出しもできる（flash encryption と secure boot は使っていない）
- 時計アプリは、Wi-Fi が未設定 (`SETUP_REQUIRED`) になると Wi-Fi の設定へ自動で切り替える。PIN がオンだとそのたびに PIN の画面が出て、「戻る」で時計に戻ってもまた出る。PIN を決めた端末で保存済みのネットワークを全部消したときに起きる。時計は仮のアプリなので、このままにしている
- 「無操作で戻る」が「しない」で、起動時の長押しで入った端末は、PIN が通ったまま設定に留まり続ける
- 鍵が開いたまま Wi-Fi の設定やアプリの設定画面に長くいて、そのあいだ home への復帰が止められている（`app_shell_set_home_return_allowed_callback()` が false を返す）と、その画面は開いたまま。settings に戻った時点で閉じる

## Tests

- `test/host/test_settings_pin_logic.c`: 入力の桁、待ち時間の表、2 回入力の一致・不一致、ハッシュの照合（塩と回数が効くこと、ハッシュの失敗は不一致になること）
- `test/host/test_settings_pin_view.c`: 全モード・全通知・全桁数・1 秒〜1 時間の全待ち時間で文字が枠に収まること、キーの大きさと重なり、数字が画面に出ないこと、解除のボタン
- `test/host/test_system_settings_view.c`: 「設定のPIN」ページの全組み合わせ（つまみの位置、グレーの見出し、「オン」「オフ」の字が無いこと）と、オフの確認画面

PBKDF2 そのものと NVS、guard の遷移は実機で確かめます（PR のチェックリスト）。
