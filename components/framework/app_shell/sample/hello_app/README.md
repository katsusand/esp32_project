# Hello App Sample

`app_shell` の lifecycle と app 切り替えを確認するためのチュートリアルコードです。

このディレクトリは通常ビルドには含めません。実験するときだけ、`main/app_main()` の初期 app を `hello_app_get_app()` に差し替え、`main/CMakeLists.txt` からこのサンプルを component として参照できる位置へ移すか、別途 component 登録してください。

English supplement: This sample is intentionally stored under `components/framework/app_shell/sample/` so it remains close to the shell implementation but outside the normal ESP-IDF component build.

## Behavior

- `hello_app`: 1 秒ごとに `hello world` をログ出力する
- 「情報」ボタン: `info_app` に切り替える
- `info_app`: 名前 (`hello_app`) と作者 (`katsusand`) を表示する
- 「OK」ボタン: `hello_app` に戻る

画面は今の UI の決まりに沿って組んであるので、新しいアプリの画面の見本にもなります。全面の背景の板から始め、色は `CYD_UI_THEME_*` (設定の配色に追従する)、文字は `cyd_ui_add_label()` の日本語書体、ボタンは高さ 40px 以上です ([CYD UI](../../../../../docs/cyd_ui.md))。

## Learning Points

- app は FreeRTOS task として作り直されない
- `app_shell` task が active app の関数ポインタを差し替える
- `enter` は切り替え直後に 1 回呼ばれる
- `step` は active な間、繰り返し呼ばれる
- `leave` は別 app へ移る直前に 1 回呼ばれる
