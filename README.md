# ESP32 CYD Clock

ESP32-2432S028R 系の CYD (Cheap Yellow Display) 向け ESP-IDF プロジェクトです。

TFT 表示、XPT2046 タッチ、オンボード RGB LED、ブザー、Wi-Fi セットアップ、NTP 時刻同期を組み合わせて、タッチ操作できる時計アプリとして動作します。

English supplement: This project targets ESP-IDF v5.4.3 and an ESP32 CYD-class board with 4 MB flash.

## Features

- LovyanGFX ベースの 320x240 TFT 表示
- XPT2046 タッチ入力とタッチ補正値の NVS 保存
- 起動時 shortcut、未同期時、設定画面から入れる Wi-Fi scan/password setup UI
- 保存済み Wi-Fi credential によるオンデマンド接続
- NTP/SNTP による時刻同期と POSIX timezone 設定
- 時計画面の 24時間/12時間表示切り替え
- RGB status LED と PWM speaker
- `sdkconfig.defaults` による CYD ボード設定と 4 MB flash 設定の固定

## Hardware

主な想定ハードウェアは、ESP32-2432S028R 系の CYD です。

現在の `sdkconfig.defaults` では以下を固定しています。

- target: `esp32`
- flash size: `4MB`
- flash mode: `dio`
- flash frequency: `40m`
- display panel: `ST7789`
- display: 320x240 landscape
- touch: XPT2046 on SPI3
- speaker GPIO: 26
- RGB LED GPIO: red 4, green 16, blue 17

パネル違いの CYD clone では、`idf.py menuconfig` の `CYD Display` から `ILI9341` / `ILI9341 variant 2` / `ST7789`、色順、反転、offset を調整してください。

English supplement: CYD clone boards often look identical but use different LCD controller initialization or color order.

## Project Structure

- `main/`: `app_main()` と起動順
- `components/framework/system_boot/`: 起動 orchestration と各 subsystem の初期化順
- `components/platform/cyd_display/`: TFT 表示、画面モデル、ログビュー、タッチ低レベルアクセス
- `components/platform/cyd_input/`: タッチ/BOOT ボタン入力イベント、タッチ補正保存
- `components/framework/app_shell/`: foreground UI アプリの実行基盤
- `components/services/cyd_wifi_setup/`: Wi-Fi scan/password setup app と helper
- `components/services/esp32_wifi_sta/`: ESP-IDF Wi-Fi STA wrapper と credential 保存
- `components/services/wifi_manager/`: Wi-Fi 接続状態と接続要求の orchestration
- `components/services/time_sync/`: SNTP/NTP 時刻同期
- `components/apps/cyd_clock_app/`: clock app
- `components/apps/cyd_clock_settings_app/`: clock-specific settings app
- `components/apps/cyd_system_apps/`: reusable `INFO` / `SETTINGS` / touch calibration apps
- `components/platform/cyd_status_led/`: RGB status LED
- `components/platform/cyd_speaker/`: PWM speaker
- `components/support/lovyangfx_wrapper/`: LovyanGFX を ESP-IDF component として組み込む wrapper
- `docs/`: コンポーネントごとの詳細ドキュメント
- `docs/components_reorg_plan.md`: `components/` 再編 TODO と今後の見通し
- `docs/system_apps_split_plan.md`: 共通 `INFO/SETTINGS` と clock-specific settings 分離 TODO
- `third_party/lovyangfx_upstream/`: LovyanGFX upstream source

## Setup

EIM 管理の ESP-IDF v5.4.3 環境を読み込んでから操作します。

```bash
source ~/.espressif/tools/activate_idf_v5.4.3.sh
```

`python_env` が存在する環境では `source ~/.espressif/v5.4.3/esp-idf/export.sh` でも動作することがありますが、このリポジトリでは EIM 前提のため `activate_idf_v5.4.3.sh` を優先します。

VS Code の ESP-IDF 拡張を使っている場合は、通常これらを手で意識しなくても拡張側が環境を扱います。

English supplement: For CLI usage in this repository, prefer the EIM-generated activation script. VS Code ESP-IDF extension users usually do not need to source it manually.

初回または設定変更時は menuconfig を開きます。

```bash
python "$IDF_PATH/tools/idf.py" menuconfig
```

Wi-Fi をビルド時に固定したい場合は、`ESP32 Wi-Fi STA` の `Wi-Fi SSID` と `Wi-Fi password` を設定します。

公開リポジトリでは credential を `sdkconfig.defaults` に入れないでください。このプロジェクトは SSID/password が空でも起動し、タッチ UI から Wi-Fi を選択して、接続成功後に NVS へ保存できます。

English supplement: Credentials are runtime/local configuration. Do not commit real SSID/password values.

## Build and Flash

```bash
source ~/.espressif/tools/activate_idf_v5.4.3.sh
python "$IDF_PATH/tools/idf.py" build
python "$IDF_PATH/tools/idf.py" -p PORT flash monitor
```

monitor を終了するには `Ctrl-]` を押します。

`idf.py` が見つからない場合は、ESP-IDF 環境が読み込まれていない可能性があります。先に `source ~/.espressif/tools/activate_idf_v5.4.3.sh` を実行してください。

人間や AI / IDE Assistant が `source` を読み落としやすい場合は、`scripts/` のラッパーを優先してください。

```bash
./scripts/idf-build.sh
./scripts/idf-menuconfig.sh
./scripts/idf-flash-monitor.sh /dev/cu.usbserial-XXXX
```

English supplement: These wrapper scripts are the preferred execution entry points for repeatable local work because they load the pinned ESP-IDF environment and default `DEV=1` automatically.

## New Mac Setup

新しい Mac でこのプロジェクトを扱うときは、まず「Codex の状態を移す」のではなく、「このリポジトリを同じ前提で扱える環境を作る」と考えてください。

このセクションは、ユーザー自身の覚書としても、AI / IDE Assistant がセットアップや運用を案内する際の基準としても使うことを意図しています。

ESP-IDF の導入は、[Espressif 公式ドキュメントの macOS 向け EIM CLI 手順](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/get-started/macos-setup.html)を基準にします。

English supplement: Use Espressif Installation Manager (EIM) CLI as the preferred macOS setup path for this project.

Homebrew がまだ入っていない場合は、先に Homebrew をインストールして、このシェルで `brew` を使えるようにします。

```bash
command -v brew
```

`brew` が見つからない場合：

```bash
/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"

echo >> ~/.zprofile
echo 'eval "$(/opt/homebrew/bin/brew shellenv zsh)"' >> ~/.zprofile
eval "$(/opt/homebrew/bin/brew shellenv zsh)"
```

English supplement: The `/opt/homebrew` path is the default Homebrew location on Apple Silicon Macs.

次に EIM CLI の前提パッケージを入れます。

```bash
brew install libgcrypt glib pixman sdl2 libslirp dfu-util cmake python
```

EIM repository を Homebrew に追加します。

```bash
brew tap espressif/eim
```

EIM CLI を入れます。

```bash
brew install eim
```

インストール後、`brew` や `eim` がすぐ使えない場合は、新しい terminal session を開くか、現在の zsh を再起動します。

```bash
exec zsh
```

次に、このプロジェクトで固定している ESP-IDF v5.4.3 を EIM CLI で導入します。

```bash
eim install -i v5.4.3
```

インストール後、`~/.espressif/tools/activate_idf_v5.4.3.sh` が存在することを確認します。

```bash
test -f ~/.espressif/tools/activate_idf_v5.4.3.sh
```

基本手順は以下です。

1. Git でこのリポジトリを clone する
2. Homebrew がなければインストールする
3. EIM CLI で ESP-IDF v5.4.3 を導入する
4. LovyanGFX submodule を取得する
5. `~/.espressif/tools/activate_idf_v5.4.3.sh` が存在することを確認する
6. このリポジトリ直下で `source ~/.espressif/tools/activate_idf_v5.4.3.sh` を実行する
7. 必要に応じて `idf.py menuconfig` を開く
8. `DEV=1 python "$IDF_PATH/tools/idf.py" build` で開発用ビルドを行う

English supplement: Treat this repository as the source of truth for project intent and workflow. A new Mac should recreate the same ESP-IDF environment rather than trying to reuse another machine's generated build outputs.

LovyanGFX は `third_party/lovyangfx_upstream` に submodule として配置します。新しく clone する場合は submodule も一緒に取得します。

```bash
git clone --recurse-submodules REPOSITORY_URL
```

すでに clone 済みの場合は、このリポジトリ直下で submodule を初期化・取得します。

```bash
git submodule update --init --recursive
```

English supplement: `components/support/lovyangfx_wrapper` builds LovyanGFX from `third_party/lovyangfx_upstream/src`, so an empty submodule directory is not a valid build state.

AI / IDE Assistant 向け運用ルール：

- このプロジェクトでは、通常のローカル開発は `DEV=1` を前提として案内する
- 新しい Mac への ESP-IDF 導入は、まず EIM CLI による `eim install -i v5.4.3` を案内する
- clone 後は `third_party/lovyangfx_upstream` の LovyanGFX submodule が取得済みか確認する
- 可能なら `./scripts/idf-build.sh`、`./scripts/idf-menuconfig.sh`、`./scripts/idf-flash-monitor.sh` を優先して使う
- `idf.py` を直接使う場合は、EIM 前提として `source ~/.espressif/tools/activate_idf_v5.4.3.sh` の実行を前提にする
- `build/` は環境依存の生成物として扱い、別の Mac で作られた `build/` の再利用を前提にしない
- `sdkconfig` はローカル生成物として扱い、共有すべき基準値は `sdkconfig.defaults` を参照する

ユーザー向けメモ：

- 普段の自分用運用では、`DEV=1` を付けたビルドを標準とする
- 実機書き込み前提の開発では、release 相当よりもまず `DEV=1` の開発用ビルドを使う
- 別 Mac で作った `build/` を残したまま別環境で触ると、`fullclean` や `menuconfig` まわりで扱いづらくなることがある

## Development Flow

このプロジェクトでは、通常の組み込み開発フローとして `DEV=1` を付けた開発用ビルドを標準とします。

```bash
source ~/.espressif/tools/activate_idf_v5.4.3.sh
DEV=1 python "$IDF_PATH/tools/idf.py" build
DEV=1 python "$IDF_PATH/tools/idf.py" -p PORT flash monitor
```

または、ラッパースクリプトを使っても同じです。

```bash
./scripts/idf-build.sh
./scripts/idf-flash-monitor.sh /dev/cu.usbserial-XXXX
```

`DEV=1` が設定されている場合、ビルド時に `APP_DEV=1` が定義されます。

VS Code の ESP-IDF 拡張からビルドする場合も、`DEV=1` が漏れないように `.vscode/settings.json` に以下を入れておきます。

```json
"idf.customExtraVars": {
    "DEV": "1"
}
```

このリポジトリの `.vscode/settings.json` には、上記設定を入れておくことを前提とします。

English supplement: Unless there is a specific reason to validate a non-development build, assume `DEV=1` for normal firmware iteration and device flashing.

AI / IDE Assistant は、ユーザーから明示的に別条件を指定されていない限り、ビルド手順の案内では `DEV=1` を優先してください。

## Build Directory Handling

`build/` はマシン依存・環境依存の生成物です。別の Mac、別の Python 環境、別の ESP-IDF セットアップで作られた `build/` を、そのまま安全に再利用できる前提では扱いません。

このプロジェクトでは、`fullclean` や `menuconfig` を安定して使える状態を保つため、以下を運用ルールとします。

- `build/` を共有資産として扱わない
- 別環境で生成された `build/` を設計根拠や再利用前提にしない
- `idf.py fullclean` が削除を拒否した場合、AI / IDE Assistant は自動削除しない
- 必要ならユーザーが `build/` を手動削除してから再生成する

English supplement: Avoid preserving a build directory across different machines when you still need reliable `idf.py menuconfig` and `idf.py fullclean` behavior.

特に、別 Mac で一度ビルドしたあとに元の Mac へ戻る運用では、`build/` を持ち回るより、その環境ごとに再生成する方が安全です。

## Wi-Fi Setup

起動時に設定済み credential がある場合でも、`wifi_manager` は通常 Wi-Fi radio を開始せず、`OFF` 状態で待機します。NTP 同期など Wi-Fi を使う処理が `wifi_manager_acquire()` した期間だけ接続し、処理完了後の `wifi_manager_release()` で利用者がいなくなると Wi-Fi を OFF に戻します。

現在の UI 構造は `main -> app_shell -> clock app / settings app / wifi setup app` です。SSID が未設定、未同期状態での接続失敗、または起動時にタッチ IRQ が LOW の場合は Wi-Fi setup app に入れます。同期済みの通常時計画面から Wi-Fi 設定へ入る場合は、`SETTINGS` から `Wi-Fi Setup` を選択します。

一度でも NTP 同期に成功した後は、再同期の接続失敗だけでは自動的に setup UI へ入らず、時計画面を維持します。

setup flow は以下です。

1. 周辺 AP をスキャンする
2. SSID 一覧を表示する
3. タッチキーボードで password を入力する
4. `SAVE` で接続テストを行う
5. 成功した場合だけ SSID/password を NVS に保存する

保存先は `esp32_wifi_sta` コンポーネントの NVS namespace です。`CONFIG_ESP32_WIFI_STA_SSID` が空の場合、次回起動時に保存済み credential が default configuration として使われます。

## Time Sync and Clock

`CONFIG_ESP32_WIFI_STA_AUTO_START` が有効な場合、`main/app_main()` は `wifi_manager_start()` の後に `time_sync_start()` を呼びます。`wifi_manager_start()` は manager task を常駐させますが、通常起動では Wi-Fi radio を開始しません。`time_sync` は同期待ちの間だけ `WIFI_MANAGER_USER_TIME_SYNC` として `wifi_manager_acquire()` し、同期処理が終わると `wifi_manager_release()` します。

一度も NTP 同期に成功していない状態では、Wi-Fi 接続失敗時に setup UI へ遷移できます。一度でも同期に成功した後は、再同期の Wi-Fi 接続失敗では setup UI に遷移せず、時計表示を維持します。

時刻同期前は時計画面に `Waiting for NTP` が表示されます。年が 2024 年以上になったら同期済みとして扱い、現在時刻と日付を表示します。

時刻表示部分をタップすると 24時間表示と 12時間表示が切り替わります。長押しはタップ扱いをキャンセルするだけで、Wi-Fi setup app への shortcut ではありません。`SYNC NOW` ボタンを押すと、その場で time sync を前倒し要求できます。Wi-Fi が落ちている場合は再接続も試みます。

timezone は `Time Sync` の `CONFIG_TIME_SYNC_TIMEZONE` で設定します。標準は `JST-9` です。

## Configuration Defaults

このリポジトリでは `sdkconfig` は `.gitignore` 対象で、`sdkconfig.defaults` を公開用の既定値として管理します。

`build/` を削除しても、通常 `sdkconfig` は消えません。ただし `sdkconfig` を削除して再生成する場合、`sdkconfig.defaults` に入っていない値は Kconfig の default に戻ります。

4 MB flash を維持するため、`sdkconfig.defaults` には以下を入れています。

```ini
CONFIG_ESPTOOLPY_FLASHSIZE_4MB=y
CONFIG_ESPTOOLPY_FLASHSIZE="4MB"
```

English supplement: `sdkconfig.defaults` is the project baseline. Local `sdkconfig` is generated and may contain machine/user-specific values.

## Documentation

詳細は `docs/` を参照してください。

- [CYD Display Driver](docs/cyd_display.md)
- [App Shell](docs/app_shell.md)
- [CYD UI Helper](docs/cyd_ui.md)
- [CYD Input Driver](docs/cyd_input.md)
- [CYD Wi-Fi Setup](docs/cyd_wifi_setup.md)
- [CYD System Apps](docs/cyd_system_apps.md)
- [ESP32 Wi-Fi STA](docs/esp32_wifi_sta.md)
- [Wi-Fi Manager](docs/wifi_manager.md)
- [Time Sync](docs/time_sync.md)
- [CYD Clock App](docs/cyd_clock_app.md)
- [CYD Status LED Driver](docs/cyd_status_led.md)
- [CYD Speaker Driver](docs/cyd_speaker.md)
- [LovyanGFX Wrapper](docs/lovyangfx_wrapper.md)
- [License](docs/license.md)

## Troubleshooting

### Display colors or orientation are wrong

`idf.py menuconfig` の `CYD Display` で panel type、rotation、RGB order、invert、offset を調整してください。

### Flash size becomes 2 MB

`sdkconfig.defaults` に 4 MB 設定が入っているか確認し、必要なら `sdkconfig` を再生成してください。

```bash
source ~/.espressif/v5.4.3/esp-idf/export.sh
idf.py reconfigure
```

それでもローカルの `sdkconfig` が 2 MB のままなら、credential など必要なローカル設定を退避した上で `sdkconfig` を作り直してください。

### Fullclean cannot remove build

別環境で生成された `build/` のため `idf.py fullclean` が削除を拒否する場合があります。その場合、このプロジェクトでは自動削除せず、内容を確認した上で手動で `build/` を削除してください。

### LovyanGFX is missing

`LovyanGFX.hpp` が見つからない、または `third_party/lovyangfx_upstream/src` が存在しない場合は、LovyanGFX submodule が未取得です。

```bash
git submodule update --init --recursive
```

`third_party/lovyangfx_upstream` が空のままでは `components/support/lovyangfx_wrapper` をビルドできません。

### Wi-Fi setup does not start

タッチ IRQ GPIO、タッチ補正、`CONFIG_CYD_TOUCH_ENABLED`、`CONFIG_CYD_TOUCH_PIN_INT` を確認してください。タッチ IRQ を使った起動時 shortcut が使えない場合でも、SSID 未設定や未同期状態での接続失敗時には setup UI へ入れます。同期済みの時計画面からは `SETTINGS` の `Wi-Fi Setup` で setup UI に入ります。

## License

このプロジェクトで `main/`、`components/`、`docs/`、ルートの設定ファイルとして管理している自作コードとドキュメントは、Apache License 2.0 の下で公開します。ライセンス本文は [LICENSE](LICENSE) を参照してください。

`third_party/lovyangfx_upstream/` に含まれる LovyanGFX upstream source、フォント、ユーティリティは、このプロジェクト本体の Apache License 2.0 ではなく、同ディレクトリ内の upstream ライセンスに従います。詳細は [License](docs/license.md) を参照してください。
