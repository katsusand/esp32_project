# NVS Storage

## Overview

このドキュメントは、このリポジトリにおける NVS の保存方針と管理手段をまとめたものです。

目的は以下です。

- system 設定と app / feature 設定を混同しない
- 派生プロジェクトで「残す設定」と「消す設定」を分けやすくする
- アプリを載せ替えたときに、前のアプリのデータを見つけて消せるようにする

English supplement: Keep NVS ownership explicit. Do not treat NVS as one flat bag of unrelated values.

## Design Rule

ownership は各 component が持ち、規約だけを共通化します。

- **保存値の所有権は component 側**。descriptor もその component のソースで宣言する
- **`support/nvs_schema` は型・命名規約・走査ユーティリティだけ**を提供し、実体テーブルは持たない

以前は `nvs_schema` が全 namespace と key を中央テーブルで抱えていましたが、それは 2 つの意味で機能していませんでした。app を 1 つ足すたびに `support/` の編集が必要になる一方、その見返りであるはずの一括管理は実装されておらず、`scope` / `owner_component` / `reset_class` / `value_type` / `versioned_payload` の 5 フィールドは**どこからも読まれていませんでした**。

English contract: ownership stays local, conventions are shared. A central table that nobody reads is pure coupling.

## Scope Prefix

scope は namespace 名の prefix で表します。

| prefix | 意味 | app 載せ替え時 |
|---|---|---|
| `sys_` | platform / framework | 残る |
| `ftr_` | 再利用する service | 残る |
| `app_` | foreground app 専用 | 一緒に消える |

**prefix にした理由は、フラッシュを走査するだけで分類できるからです。** レジストリに自己申告させる方式では、「もうビルドに含まれていない component の namespace」を報告できません。そして孤児検出で見つけたいのは、まさにそれです。

```c
NVS_SCHEMA_DECLARE_NS(NVS_NS, "sys_shell");
static const nvs_key_descriptor_t NVS_KEY_APP_SHELL_CONFIG = {
    .ns = NVS_NS,
    .key = "config_v1",
};
```

### Name Length

**NVS の namespace 名は 15 文字までです**（`NVS_NS_NAME_MAX_SIZE` = 16、null 終端込み）。

超過分は**エラーにならず黙って切り詰められ、実行時に別の namespace になります**。`NVS_SCHEMA_DECLARE_NS` はこれを静的アサートで弾きます。

```
error: static assertion failed: "NVS namespace name is too long: ftr_radio_way_too_long"
```

prefix が 4 文字なので、component 側に使えるのは 11 文字です。`feat_` ではなく `ftr_` にしているのは、この 1 文字が効くためです。

## Current Inventory

| namespace | scope | 所有 component |
|---|---|---|
| `sys_display` | system | `cyd_display` |
| `sys_input` | system | `cyd_input` |
| `sys_ui` | system | `cyd_ui` (配色のテーマ。key `theme`、u8) |
| `sys_shell` | system | `app_shell` |
| `ftr_radio` | feature | `radio_manager` |
| `ftr_sched` | feature | `app_scheduler` |
| `ftr_timesync` | feature | `time_sync` |
| `ftr_wifi` | feature | `wifi_profile_store` |
| `app_sched` | app | `app_scheduler` (app scope の schedule) |

`app_scheduler` は 2 つの namespace を持ち、schedule ごとの `scope` で保存先を選びます ([app_scheduler.md](app_scheduler.md#scope))。`app_sched` の中身は現在、時計のアラーム (`cyd_clock_alarm`) だけです。時計アプリ自身は NVS を直接使いません。

`Clear App Data` で `app_sched` を消しても、次回起動時に時計が既定の (無効な) アラームを作り直すので、namespace はすぐに戻ります。消えるのはユーザーが設定した時刻と曜日です。

English supplement: `app_sched` belongs to `app_scheduler`, but its contents belong to apps. Clearing it resets the clock's alarms to their disabled defaults; the namespace itself reappears on the next boot.

タッチ補正は以前 `cyd_display` の namespace に間借りしていましたが、所有者は `cyd_input` なので `sys_input` として分離しました。

### Namespaces Are Created On First Write

**namespace は書き込みが発生した瞬間に作られます。** 一覧に出てこないのは「壊れている」ではなく「まだ誰も保存していない」だけです。

このため、設定画面を離れるときの保存処理が namespace を新規に作ることがあります。実機で `Clear App Data` を実行した際、1 件消したのに合計が 10 → 12 に増えたのはこれが理由でした（再起動前の保存で `sys_display` / `ftr_timesync` / `ftr_radio` が生まれた）。動作としては正しく、値を失わないための処理です。

## Namespaces We Do Not Own

フラッシュには ESP-IDF 自身の namespace も入っています。実機で確認できたものは 2 つです。

| namespace | 出所 | 消すと |
|---|---|---|
| `nvs.net80211` | Wi-Fi ドライバ（バイナリブロブ） | Wi-Fi 設定が失われる |
| `phy` | `esp_phy` の RF キャリブレーション | 次回起動で再キャリブレーションが走る |

`phy` は `esp_phy/src/phy_init.c` の `PHY_NAMESPACE` で確認できます。`nvs.net80211` はブロブ側なのでソースでは確定できませんが、ESP-IDF 自身の NVS テストに実例として現れます。

**これらは prefix を持たないため `unknown` に分類されます。** つまり `unknown` は「うちの孤児」ではなく「**分類できないもの全部**」であり、ESP-IDF のデータが混ざります。

English contract: the unknown bucket is not a synonym for "our leftovers". It holds ESP-IDF's own namespaces too, which is why erasing it is never automated.

## Management

`nvs_schema` はフラッシュを走査するユーティリティを提供します。

```c
esp_err_t nvs_schema_for_each_namespace(nvs_schema_namespace_cb_t cb, void *ctx);
esp_err_t nvs_schema_erase_scope(nvs_schema_scope_t scope, size_t *erased_count);
```

### 一覧表示

`INFO` app の `NVS` page が、実際にフラッシュへ入っている namespace を scope とエントリ数つきで一覧します。**コンポーネントの自己申告ではなくフラッシュの実データ**なので、孤児がここに現れます。

### scope 単位の消去

設定の「初期化」page にある「アプリのデータを消去」(`Clear App Data`) が `app_` scope だけを消し、再起動します。

**再起動は必須です。** app は起動時に自分の NVS データを読むため、消したあとも動き続けていると古い状態を保持したままになります。

`nvs_schema_erase_scope()` は `NVS_SCHEMA_SCOPE_UNKNOWN` を `ESP_ERR_INVALID_ARG` で拒否します。上記のとおり ESP-IDF のデータが混ざるバケツなので、一括消去を自動化してはいけません。分類できない namespace が孤児かどうかは人間の判断であり、必要なら `Initialize NVS`（全消去）を使います。

## Reset Actions

`SETTINGS` の `NVS` page には、破壊範囲の小さい順に 3 つ並んでいます。

| 操作 | 消える範囲 | 再起動 |
|---|---|---|
| 「タッチ補正を消去」(`Clear Touch Calib`) | `sys_input` のタッチ補正 key のみ | あり |
| 「アプリのデータを消去」(`Clear App Data`) | `app_` scope の namespace 全部 | あり |
| 「すべて初期化」(`Initialize NVS`) | `nvs_flash_erase()` で全部 | あり |

`Initialize NVS` は `phy` と `nvs.net80211` も消しますが、どちらも起動時に再生成されます。

## Migration

保存形式を変えるとき（namespace のリネーム、blob の形式変更）は、移行コードを書かず**全消去**します。

これは開発中の一時的な割り切りではなく、この基盤の方針です。この基盤は汎用の app 基盤ですが、その上に作る app 同士には関連を持たせません。そのため、ある firmware の保存データを別の形式の firmware へ引き継ぐ必要がなく、形式が合わなければ `Initialize NVS` で全部消してやり直すのが最も単純で確実です。key ごとに既定値へ戻す仕組みは用意しません（2026-09-26 のレビューの指摘を受けて検討し、2026-09-28 にこの方針の維持を決めました）。

```bash
source ~/.espressif/tools/activate_idf_v5.4.3.sh
python "$IDF_PATH/tools/idf.py" erase-flash flash monitor
```

English contract: a storage format change (namespace rename, blob layout or version) is handled by a full erase, not by migration code or per-key resets. This is the platform's intended policy: apps built on it are unrelated to each other, so no firmware needs to carry another's saved data forward.

## Implementation Guidance

新しい保存値を足すときの手順です。

1. 所有する component のソースで `NVS_SCHEMA_DECLARE_NS` と `nvs_key_descriptor_t` を宣言する
2. prefix を scope に合わせて選ぶ（app 載せ替えで残すべきかで判断する）
3. `nvs_open_descriptor(DESC.ns, ...)` で開く
4. blob を保存するなら先頭に version を持たせ、不一致は `nvs_health_report_invalid()` で報告する
5. `support/nvs_schema` は編集しない

blob の version チェックに失敗すると、起動時に `Initialize NVS` 画面へ強制遷移します。壊れたデータや形式の違うデータを黙って使わず、[Migration](#migration) の方針どおり全消去で回復するための仕組みです。

## Notes

- `cyd_speaker` の音量は runtime API のみで、NVS 保存は未実装です
- `nvs_schema` が一度に扱える namespace 数は `NVS_SCHEMA_MAX_NAMESPACES`（24）です
