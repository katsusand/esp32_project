# License

このプロジェクトで `main/`、`components/`、`docs/`、ルートの設定ファイルとして管理している自作コードとドキュメントは、Apache License 2.0 の下で公開します。ライセンス本文はリポジトリルートの `LICENSE` を参照してください。

English supplement: Project-authored source code, documentation, and configuration files are licensed under Apache License 2.0 unless a file explicitly states otherwise.

`third_party/lovyangfx_upstream/` は LovyanGFX の upstream source を同梱している領域です。このディレクトリ配下のコード、フォント、ユーティリティは、このプロジェクト本体の Apache License 2.0 ではなく、LovyanGFX upstream と各ファイルに含まれる第三者ライセンスに従います。

主な参照先は以下です。

- `third_party/lovyangfx_upstream/license.txt`
- `third_party/lovyangfx_upstream/README.md` の `ライセンス License`
- `third_party/lovyangfx_upstream/src/lgfx/Fonts/IPA/IPA_Font_License_Agreement_v1.0.txt`
- `third_party/lovyangfx_upstream/src/lgfx/Fonts/efont/COPYRIGHT.txt`

English supplement: Keep third-party notices and license files intact when redistributing this repository or firmware source packages.

UI の日本語フォント (`components/support/cyd_ui_fonts/generated/`) は BIZ UDPGothic から必要な文字だけを取り出して作った表で、BIZ UDPGothic の改変版として SIL Open Font License 1.1 に従います。ライセンス本文は `third_party/fonts/biz_udpgothic/OFL.txt` です。ファームウェアやソースを配布する際は同梱してください。

English supplement: the generated font tables are a Modified Version of BIZ UDPGothic under OFL 1.1, not Apache 2.0. Keep `OFL.txt` with any distribution that contains them.

ESP-IDF および ESP-IDF managed components は、それぞれ上流プロジェクトのライセンスに従います。このリポジトリの Apache License 2.0 は、ESP-IDF や外部依存ライブラリのライセンス条件を置き換えるものではありません。
