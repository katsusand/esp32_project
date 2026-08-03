# App Shell

## Overview

`app_shell` は、CYD の foreground UI アプリを実行するための最小ランタイムです。

通常構成では、`main/app_main()` は `cyd_clock_composition_start()` を呼びます。compositionが共通bootと時計製品のserviceを初期化したあと、`app_shell_start()` により `app_shell` taskがLCD ownerとして常駐します。そのtask上で`clock app`や`wifi setup app`が1つずつ実行されます。

学習用・実験用に初期アプリを差し替える場合も、考え方は同じです。たとえば `hello_app` から `info_app` に切り替える場合でも、新しい FreeRTOS task を作るのではなく、`app_shell` task が「次に呼ぶ app」を差し替えます。

English supplement: `app_shell` is the single foreground UI task. It owns `cyd_display` and runs exactly one active app at a time by calling its lifecycle callbacks.

## Purpose

導入目的は以下です。

- LCD owner を 1 task に固定する
- 画面遷移責務を各コンポーネントから分離する
- `clock app`、`wifi setup app`、将来の `sensor app` / `settings app` を同じ型で載せる

`cyd_display` owner 制と組み合わせることで、background task が誤って描画するのを避けやすくしています。

## Public API

利用するファイルでは、次のヘッダーを include します。

```c
#include "app_shell.h"
```

アプリ型は `app_shell_app_t` です。

```c
typedef struct app_shell_app {
    const char *id;
    void *ctx;
    esp_err_t (*enter)(void *ctx, const app_shell_app_t *from_app);
    esp_err_t (*step)(void *ctx);
    esp_err_t (*leave)(void *ctx);
    bool (*idle_return_suppressed)(void *ctx);
} app_shell_app_t;
```

利用側は `app_shell_start()` で初期アプリを起動します。

```c
ESP_ERROR_CHECK(app_shell_start(cyd_clock_app_get_app()));
```

アプリ切り替えは `app_shell_switch_to()` で要求します。

```c
ESP_ERROR_CHECK(app_shell_switch_to(cyd_wifi_setup_get_app()));
```

切り替えは即時ではなく pending request として記録され、現在の `step()` のあとで `leave -> enter` の順に適用されます。

English supplement: `app_shell_switch_to()` is deferred until the shell regains control after the current app step returns.

home復帰timeoutは `app_shell_get_idle_return_timeout_seconds()` と
`app_shell_set_idle_return_timeout_seconds()` で実行時に参照・変更できます。
`app_shell_save_idle_return_timeout_seconds()` を呼ぶとNVSへ保存され、次回起動時はKconfig初期値よりNVS値が優先されます。`0` は自動復帰無効です。

入力中など一時的にhome復帰させたくないアプリは `idle_return_suppressed` で自身の状態を返します。アプリIDによる特例判定は行いません。

composition 側で起動中の service 状態に応じて home 復帰を止めたい場合は、`app_shell_set_home_return_allowed_callback()` で短い判定callbackを登録します。`app_shell` 自体は Wi-Fi や時刻同期などの domain service を直接参照しません。

English supplement: The suppression and home-return guard callbacks must be fast and side-effect free. Zero timeout disables idle return.

## Lifecycle

各 shell app は、`enter`、`step`、`leave` の lifecycle callbackと、任意の `idle_return_suppressed` callbackで構成されます。

- `enter`: その app に切り替わった直後に 1 回だけ呼ばれる。`from_app` には遷移元 app が入る
- `step`: その app が active な間、`app_shell` task から繰り返し呼ばれる
- `leave`: 別の app に切り替わる直前に 1 回だけ呼ばれる

典型的には、`enter` では画面描画や一時状態の初期化を行います。`step` では入力処理、周期処理、必要に応じた再描画、別 app への切り替え要求を行います。`leave` では後片付けや一時状態の保存を行います。

初期 app の `enter` では `from_app == NULL` です。`app_shell_switch_to()` による切り替えでは、直前に active だった app が `from_app` として渡されます。`BACK` ボタンを持つ app は、この `from_app` を戻り先として保持できます。

重要なのは、`step()` は短い処理をして戻る前提で書くことです。`step()` の中で戻らない無限ループを作ると、`app_shell` が pending switch を適用できません。

English supplement: `from_app` is the previous active app pointer. `step()` must return regularly. Long-running or blocking work should be modeled as state, events, or a separate service task.

## Task Model

`app_shell` は内部で FreeRTOS task を 1 つ作ります。

- task 名: `app_shell`
- display owner: この task が `cyd_display_claim_owner()` を実行する
- active app: `enter/step/leave` をこの task 上で呼ぶ

つまり `cyd_clock_app` や `cyd_wifi_setup` は独自の UI task を持たず、`app_shell` task 上で動作します。

English supplement: App callbacks share the shell task stack. They are not separate FreeRTOS tasks.

`app_shell_switch_to()` による画面遷移では、FreeRTOS task をその都度 `create/delete` しません。`hello_app` や `info_app` のような shell app は、通常 `static const app_shell_app_t` としてメモリ上に存在しており、切り替え時には active app のポインタを差し替えるだけです。

簡略化すると、動きは次のようになります。

```text
app_shell task starts once
  active_app = initial_app
  active_app->enter(ctx, NULL)

loop:
  active_app->step()

  if pending_app exists:
    active_app->leave()
    active_app = pending_app
    active_app->enter(ctx, from_app)
```

この構造により、UI 用の task stack や描画 owner を 1 つにまとめられます。画面ごとに task を増やさないため、組み込み機器では軽量で扱いやすい構成です。

## Error Policy

app の `enter` / `step` / `leave` が非 OK の `esp_err_t` を返しても、shell は**再起動しません**。

これは意図的な契約です。I2C の NACK、UART overflow、センサ read timeout などは app にとって正常系の実行時条件であり、プログラミングエラーではありません。以前は `ESP_ERROR_CHECK` で受けていたため、これらが即座に panic reboot になっていました。

English contract: an app failure is an expected runtime condition, never a panic. Only invariant violations that leave the device unable to render at all stay fatal.

既定の方針は次のとおりです。

- エラーを `ESP_LOGE` に記録し、連続失敗回数を数える
- 連続 `CONFIG_APP_SHELL_MAX_CONSECUTIVE_ERRORS` 回に達したら、その app を諦めて home app へ復帰する
- 成功したら連続カウントを 0 に戻す
- **エラー1回ごとに必ず `CONFIG_APP_SHELL_ERROR_BACKOFF_MS` 待つ**

最後の backoff は必須です。即座に失敗を返す `step()` は、backoff が無いと shell task を自身の priority で回し続け、他のタスクを餓死させます。

English supplement: the backoff is not a convenience. Without it a permanently failing step() starves every lower priority task.

app 側で方針を変えたい場合は `on_error` を実装します。

```c
static app_shell_error_action_t my_app_on_error(void *ctx, const app_shell_error_t *error)
{
    if (error->err == ESP_ERR_TIMEOUT) {
        return APP_SHELL_ON_ERROR_CONTINUE;   /* センサ無応答は許容して継続 */
    }
    return APP_SHELL_ON_ERROR_RETURN_HOME;
}
```

`on_error` が NULL の app は既定方針が適用されるため、既存 app に変更は不要です。

段階ごとの扱いには差があります。

- `step` 失敗: 上記の既定方針
- `enter` 失敗: home へ復帰する。復帰の遷移は、**半端に enter された app の `step()` が走る前に**適用される
- `leave` 失敗: 要求されたアクションに関わらず遷移を続行する。失敗した app にユーザーを閉じ込めないため
- 起動時の初期 app の `enter` 失敗: 初期 app は home app でもあるため復帰先が無い。panic せず backoff に任せる

再起動しなくなった分エラーが見えにくくなるので、観測用に以下を用意しています。

```c
bool app_shell_get_last_error(app_shell_error_t *error);
uint32_t app_shell_get_total_error_count(void);
```

## Back Navigation

戻るボタンは `app_shell_return_to()` を使います。

```c
esp_err_t app_shell_return_to(const app_shell_app_t *return_app);
```

app は `enter()` で受け取った `from_app` を戻り先として覚えますが、**このポインタは NULL になり得ます**。

- その app が initial app のとき
- 製品が起動時に直接その画面へ入るとき（`cyd_clock_composition` の NVS 強制初期化フローなど）

`app_shell_switch_to(NULL)` は何も起きずに終わるため、この場合ユーザーは「戻るボタンが壊れた画面」に取り残されます。実際に、無操作タイムアウト以外に home へ戻る手段が無くなる不具合が出ました。

`app_shell_return_to()` は戻り先が NULL のとき **home app にフォールバック**するので、戻る操作には必ず行き先があります。

English contract: prefer `app_shell_return_to()` over `app_shell_switch_to()` for back controls. Recording `from_app` remains the primary behavior; home is only the fallback.

戻り先を home 固定にはしていません。`clock -> settings -> wifi_setup -> settings -> <<` が `clock` へ戻る、という「来た道を戻る」挙動が正しいためです。

## Current Apps

現時点で shell に載っているアプリは以下です。

- `clock`: `cyd_clock_app_get_app()`
- `info`: `system_info_app_get_app()`
- `settings`: `system_settings_app_get_app()`
- `wifi_setup`: `cyd_wifi_setup_get_app()`

`clock app` は通常時の時計表示、`SYNC NOW`、Wi-Fi failed 画面、retry progress を担当します。`info app` は firmware / chip / heap / Wi-Fi 状態などの参照情報を表示します。`settings app` は system-level settings UI で、brightness、volume、time sync interval、timezone、stored SSIDs、Wi-Fi setup、touch calibration、NVS maintenance、app-specific extension entry を扱います。`wifi setup app` は scan / password / setup completion を担当します。

English supplement: `info` and `settings` are regular shell apps. They use the `from_app` passed to `enter()` as their return target, keeping them independent from `clock`.

学習用の `hello app` は、`components/framework/app_shell/sample/hello_app/` にサンプルコードとして置いています。通常ビルドには含めません。1 秒ごとに `hello world` をログ出力し、画面下部の `info` ボタンで `info app` に切り替えます。`info app` はアプリ情報を表示し、`OK` ボタンで `hello app` に戻ります。

English supplement: `hello` is a learning app for observing app switching without involving clock or Wi-Fi setup behavior.

## Display Ownership

描画 API を呼べるのは `app_shell` task 文脈だけです。

- `app_shell` が `cyd_display_claim_owner()` する
- foreground app は shell task 上で動くので描画できる
- `wifi_connection`、`time_sync` などの background task は描画しない

このルールにより、タッチ/描画競合を減らします。

## Configuration

主な設定項目は `idf.py menuconfig` の `App Shell` から変更できます。

- `CONFIG_APP_SHELL_TASK_STACK_SIZE`
- `CONFIG_APP_SHELL_TASK_PRIORITY`
- `CONFIG_APP_SHELL_IDLE_RETURN_TIMEOUT_SECONDS`
- `CONFIG_APP_SHELL_MAX_CONSECUTIVE_ERRORS`
- `CONFIG_APP_SHELL_ERROR_BACKOFF_MS`

foreground app は shell task の stack を共有するため、stack size は active app の最大使用量を見て調整します。

## Shared Data

`app_shell` は「どの app を実行するか」を管理しますが、「app 間でどのデータを共有するか」は別の設計問題です。

このプロジェクトでは、共有データは次のように分けると理解しやすいです。

- 電源を切っても残す設定: NVS や store component に保存する
- 起動中だけ必要なシステム状態: `wifi_connection`、`time_sync` のような manager component が持つ
- app 固有の一時状態: app 内部の `static` state や `app_shell_app_t.ctx` に持つ
- 画面遷移の一時パラメータ: `enter()` の `from_app` や、必要に応じた遷移先 app の setter API で渡す

たとえば Wi-Fi 設定は `wifi_profile_store` / NVS に保存され、Wi-Fi の現在状態は `wifi_connection` が管理します。Wi-Fi setup 中に home 復帰を止めるような製品固有の接続判断は、composition が `app_shell_set_home_return_allowed_callback()` に登録します。これは `app_shell` に Wi-Fi 知識を持たせるより、意味のある component に分けた方が責務が明確です。

一方で、PC のクリップボードのように「特定 app の所有物ではないが、複数 app が一時的に使う作業状態」が必要になった場合は、`app_shell` または `app_workspace` のような共通 component に持たせる選択肢があります。

English supplement: Keep `app_shell` thin unless the shared state is truly UI-session-wide rather than a domain service such as Wi-Fi, time sync, or profile storage.

## Notes

現時点の `app_shell` は intentionally minimal です。

- app stack / modal stack は未実装
- `push/pop` ではなく `switch_to` のみ
- 共通 header/footer 描画機構は未実装

English supplement: This is a replace-only shell, not a full navigation stack yet.
