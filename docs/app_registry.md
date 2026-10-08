# App Registry

## Overview

`app_registry` は、app が自分自身を登録し、汎用 UI がそれを列挙するための仕組みです。

`components/framework/app_registry/` に置いています。

## Why

以前は、app 間の遷移がコンパイル時のシンボル参照でした。

```c
app_shell_switch_to(system_settings_app_get_app());
```

この形だと 2 つの問題があります。

- app を追加するたびに、導線を持つ既存 app を編集することになる
- 設定画面を拡張できる app が **1 つだけ** だった（`system_settings_extension_t` はスロットが 1 個しかなかった）

registry はこれを反転させます。app が自分を登録し、shell 側の UI は「そこにあるもの」を並べます。

English contract: registration inverts the dependency. Generic UI enumerates what is present instead of naming apps at compile time.

## Public API

```c
#include "app_registry.h"

typedef struct {
    const char *id;
    const char *title;
    const app_shell_app_t *app;
    const app_shell_app_t *settings_app;   /* optional */
} app_registry_entry_t;

esp_err_t app_registry_register(const app_registry_entry_t *entry);

size_t app_registry_count(void);
const app_registry_entry_t *app_registry_at(size_t index);
const app_registry_entry_t *app_registry_find(const char *id);

size_t app_registry_settings_count(void);
const app_registry_entry_t *app_registry_settings_at(size_t index);
```

## Settings Screens Are Children, Not Peers

app 固有の設定画面は、**独立した entry として登録しません**。所有する app の `settings_app` に付けます。

```c
static app_registry_entry_t entry = {
    .id = "clock",
    .title = "Clock",
};

entry.app = cyd_clock_app_get_app();
entry.settings_app = cyd_clock_settings_app_get_app();
```

これで 2 つの性質が構造的に保証されます。

- **launcher に設定画面が並ばない。** launcher は app の一覧なので、`Clock` は出ますが `Clock Settings` は出ません
- **app を載せなければ設定画面も入らない。** `Clock Settings` は `Clock` を register しない限り、どこからも到達できません

以前は両者を対等な entry として登録していたため、launcher に `Clock` と `Clock Settings` が並び、home app の候補として同格に見えていました。

English contract: a settings screen is owned by its app. Attaching it to the owner's entry makes "no app, no settings screen" a structural property instead of a convention someone has to remember.

### Filtered View

設定画面を持つ app だけを詰めて列挙するアクセサがあります。

```c
size_t app_registry_settings_count(void);
const app_registry_entry_t *app_registry_settings_at(size_t index);
```

一覧を描いてからタップを entry に戻すコードは、**描画と解決で同じ添字体系を使う必要があります**。呼び出し側が `app_registry_at()` を自前でフィルタすると添字がずれるため、フィルタ済みビューを registry 側で提供しています。

## Installed vs Home

「載っているか」と「home か」は別の概念です。

- **installed**: composition が `app_registry_register()` を呼んだか
- **home**: composition が `app_shell_start()` に何を渡したか

`CONFIG_CYD_CLOCK_COMPOSITION_HOME_LAUNCHER=y` にしても clock は登録されたままなので、launcher の一覧に出るし設定画面も残ります。clock ごと外したい場合は register を呼ばないようにします。

## Registration

`entry` は **コピーされません**。`app_shell` が `app_shell_app_t` を借用するのと同じで、プロセス寿命より長く生きる領域を渡す必要があります。実質 `static` です。

```c
static void cyd_clock_app_prepare_settings_extension(void)
{
    static app_registry_entry_t entry = {
        .id = "clock_settings",
        .title = "Clock Settings",
    };

    entry.app = cyd_clock_settings_app_get_app();
    (void)app_registry_register(&entry);
}
```

同じ `id` の二重登録は `ESP_ERR_INVALID_STATE` になります。黙って 2 行に増えるより、エラーとして見えるほうが良いためです。

登録は composition の起動中、`app_shell` task が走り出す前に単一 task から行う想定なので、内部にロックは持っていません。

English contract: register during composition startup, before the shell task runs. The registry is not thread-safe by design.

## Settings APPS Page

`system_settings_app` は、登録された app を一覧する `APPS` ページを持ちます。

- 登録が 0 件のときはページごと出ません（空ページが遷移列に混ざるのを避けるため）
- 表示できるのは `SYSTEM_SETTINGS_VIEW_APPS_MAX` (4) 件までです。スクロールはまだありません
- ボタンを押すとその app へ遷移します

以前 `GENERAL` ページに 1 個だけ置いていた拡張ボタンは、このページに移りました。

## Configuration

- `CONFIG_APP_REGISTRY_MAX_ENTRIES` (既定 8)

entry はポインタで保持するため、消費 RAM は この値 x 4 バイトです。

## Registration Timing

登録は **composition の起動時**に行います。app の `enter()` の中で自分を登録してはいけません。

その形だと、その app が home でなくなった瞬間に登録が走らなくなり、launcher にも `APPS` page にも出てこなくなります。実際に `cyd_clock_app` が `Clock Settings` を `enter()` で登録していて、home app を差し替え可能にする際に問題になりました。

各 app は `*_register()` を公開し、composition がどの app を載せるか決めます。

## Consumers

- `app_launcher` — 登録 app を一覧する汎用 home / メニュー app
- `system_settings_app` の `APPS` page — 設定からの導線

どちらも registry を読むだけなので、app 追加時に更新が要るのは登録だけです。
