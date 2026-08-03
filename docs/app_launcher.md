# App Launcher

## Overview

`app_launcher` は、`app_registry` に登録された app を一覧して選ばせるだけの汎用 app です。

`components/apps/app_launcher/` に置いています。

製品固有の知識を一切持たないため、**任意の製品の home app として使えます**。時計を home にしたままメニュー画面として使うこともできます。

English contract: the launcher holds no product knowledge. Whether it is home or a normal screen depends only on what the composition passes to `app_shell_start()`.

## Home App Is A Composition Choice

まず前提として、**home app は以前から composition のパラメータです**。framework 側は home を固定していません。

```c
return app_shell_start(initial_app);   /* initial_app が home になる */
```

つまり "Hello World だけ表示する app" を home にすることは、launcher が無くても以前から可能でした。`app_shell` は `initial_app` をそのまま home として扱い、無操作時の自動復帰先にもします。

launcher が加えるのは「**汎用の** home をひとつ用意する」ことです。app が増減しても launcher 自身は変更不要になります。

## Selecting The Home App

`cyd_clock_composition` では Kconfig で切り替えます。

- `CONFIG_CYD_CLOCK_COMPOSITION_HOME_LAUNCHER = n` (既定): home は clock
- `CONFIG_CYD_CLOCK_COMPOSITION_HOME_LAUNCHER = y`: home は launcher

無効時、launcher はリンカに落とされるため **バイナリに含まれません**。有効時の増加は約 976 バイトです。

別の app を home にしたい場合は、composition の `CYD_CLOCK_COMPOSITION_HOME_APP()` を差し替えます。

English supplement: idle auto-return targets the home app, so this switch also changes where the device settles after inactivity. On a clock product that is usually a reason to keep the clock as home.

## Behavior

- 登録 app を 1 ページ 5 件まで縦に並べます。**app のみで、app 固有の設定画面は並びません**（設定画面は所有 app の `settings_app` であり、対等な app ではないため）
- 6 件以上あるときだけページ送りボタンが出ます
- home として動いている間は `<<` ボタンを出しません。home の上には戻り先が無く、押せないボタンを描くより出さないほうが正しいためです
- 自分自身が登録されていても、選択しても何も起きません（同じ app への遷移は無意味なため）
- 登録が 0 件のときは `no apps registered` を表示します

## Registration Timing

launcher は `app_registry` の内容をそのまま並べるだけなので、**登録は composition の起動時に済んでいる必要があります**。

かつて `cyd_clock_app` は自分の `enter()` の中で `Clock Settings` を登録していました。この形だと clock が home でなくなった瞬間に登録が走らなくなり、launcher にも settings の `APPS` page にも出てこなくなります。

現在は composition が起動時にまとめて登録します。

```c
static void cyd_clock_composition_register_apps(void)
{
    cyd_clock_composition_start_optional("register clock app failed",
                                         cyd_clock_app_register());
    cyd_clock_composition_start_optional("register clock settings app failed",
                                         cyd_clock_settings_app_register());
}
```

English contract: apps expose a `*_register()` function; the composition decides which apps exist. Registration must never depend on which app happens to run.

## Relationship To The Settings APPS Page

`system_settings_app` の `APPS` page も同じ registry を並べます。役割が重複して見えますが、想定は次のとおりです。

- home が clock のとき: `APPS` page が実質の app 一覧になる
- home が launcher のとき: launcher が一覧で、`APPS` page は設定からの補助導線になる

どちらも registry を読むだけなので、app を追加したときに更新が要るのは registry への登録だけです。
