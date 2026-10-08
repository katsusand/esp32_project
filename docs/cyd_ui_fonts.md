# CYD UI Fonts

## Overview

`components/support/cyd_ui_fonts` は、UI で使うアンチエイリアスの日本語フォントの表を持つコンポーネントです。元の書体は BIZ UDPGothic (SIL OFL 1.1) です。

- 表はすべて `const` で flash に置いたまま使う。描画時に RAM へコピーしない
- 1 ピクセル 4 bit (16 階調)
- 収録する文字は、ファームウェアが実際に表示する文字に絞る

English supplement: These are not LovyanGFX VLW fonts on purpose. The VLW loader copies about 9 bytes of metrics per glyph to the heap, which for three faces of several hundred glyphs is 10-20KB on an ESP32 without PSRAM. These tables are read straight from flash and cost no RAM.

## Faces

| Symbol | Size | 収録文字 |
|---|---|---|
| `cyd_ui_font_body` | 16px Regular | テキスト用の文字集合 (プロファイルによって JIS X 0208 全体も) |
| `cyd_ui_font_body_bold` | 16px Bold | テキスト用の文字集合 |
| `cyd_ui_font_title` | 24px Bold | テキスト用の文字集合 |
| `cyd_ui_font_clock_medium` | 48px Bold | `0-9 : / - . 空白 ✓ !` |
| `cyd_ui_font_clock_large` | 64px Bold | `0-9 : / - . 空白 ✓ !` |

アプリは表を直接使わず、`cyd_display_font_t` で書体を選びます ([CYD Display Driver](cyd_display.md#text-and-fonts))。

## Character Set

テキスト用の文字集合は次の 2 つの和です。

- 必須: ASCII (URL・SSID・コードの入力と表示に必要)、`components/` と `main/` の C/C++ ソースにある文字列リテラル中の全文字、`scripts/ui_fonts/extra_chars.txt` の文字。元フォントに無ければ生成はエラーになる
- 任意: ひらがな、カタカナ、和文の記号、全角英数記号など。元フォントにあるものだけ入れる

## Font Profile

`scripts/ui_fonts/font_profile.json` で、プロジェクトごとに収録する文字を切り替えます。

| キー | 既定 | 内容 |
|---|---|---|
| `body_jis_x0208` | `false` | `true` にすると、16px 標準書体 (`cyd_ui_font_body`) だけに JIS X 0208 の全 6,879 文字と、Windows で入力される異体の記号 (`～－∥￠￡￢`) を加える。あわせて `generated/message_font.json` を書き出す |

このリポジトリは `false` です。サーバーから届く文章のように、ファームウェアが前もって知らない文章を描く派生プロジェクトだけが `true` にします。`true` にすると flash が約 800KB 増えます。IBM 拡張漢字 (「髙」「﨑」など) は含みません。

English supplement: the profile is a data file, not a command-line flag, so regenerating from the repository alone always gives the same tables. Keys other than those listed are rejected.

文字列リテラルだけを数えます。コメントは先に取り除くので、日本語のコメントがフォントを大きくすることはありません。実行時に組み立てるだけでリテラルに現れない文字は `extra_chars.txt` に書いてください。

English contract: a string literal using a character the fonts lack fails the firmware build (see Build Check). Text that reaches the screen from outside the firmware, such as an employee name from the backend, can still contain such characters; they render as a visible box, never as a gap.

## Regenerating

文言を追加・変更したら、フォントを作り直してください。生成物 (`generated/`) はリポジトリに含めるので、通常のビルドに freetype は不要です。

```bash
scripts/ui_fonts/fetch_fonts.sh
```

```bash
python3 -m venv scripts/.venv
```

```bash
scripts/.venv/bin/pip install -r scripts/ui_fonts/requirements.txt
```

```bash
scripts/.venv/bin/python scripts/ui_fonts/gen_ui_fonts.py
```

`fetch_fonts.sh` は固定したコミットの TTF を取得し、SHA-256 を照合します ([third_party/fonts/biz_udpgothic/README.md](../third_party/fonts/biz_udpgothic/README.md))。TTF はリポジトリに含めません。

`--gamma` で文字の太さの見え方を調整できます (既定 0.85。1 未満で太め)。

## Build Check

ファームウェアのビルドは、`scripts/ui_fonts/check_ui_font_glyphs.py` で文字列リテラルとフォントの収録文字 (`generated/text_font_chars.txt`) を照合します。足りない文字があると、ファイル名と文字を表示してビルドが止まります。

```text
UI fonts lack characters used by the firmware:
  components/apps/app_launcher/app_launcher_view.c: 無
Regenerate them: scripts/.venv/bin/python scripts/ui_fonts/gen_ui_fonts.py
```

このチェックは標準ライブラリだけで動くので、ESP-IDF の Python 環境でそのまま実行されます。

## Message Font JSON

`body_jis_x0208` が `true` のときだけ、生成スクリプトは `generated/message_font.json` も書き出します。16px 標準書体の全収録文字と送り幅 (ピクセル) の一覧で、サーバー側の編集画面が文言を保存する前に「端末で描けるか」「幅に収まるか」を判定するためのものです。`false` で作り直すと削除されます。

## Merging Into A Derived Project

`generated/` の表は、プロジェクトごとに中身が違います (文字列とプロファイルが違うため)。派生プロジェクトがこのリポジトリを `upstream` として取り込むときは、取り込みのたびに表を作り直します。

```bash
scripts/ui_fonts/merge_upstream.sh
```

初回は派生プロジェクトにまだこのスクリプトが無いので、upstream 側のものを実行します。

```bash
git fetch upstream && git show upstream/main:scripts/ui_fonts/merge_upstream.sh | bash
```

このスクリプトは次を行います。コミットはしません。

1. `git fetch upstream` のあと、`git merge --no-commit --no-ff upstream/main` を実行する
2. `generated/` が衝突していれば、どちらかの版で解決済みにする (直後に作り直すので、どちらでもよい)
3. `generated/` 以外に衝突があれば、そこで止まる。解決してから `scripts/ui_fonts/merge_upstream.sh --regen` を実行する
4. フォントを作り直し、`generated/` をステージする

衝突が無くても毎回作り直します。派生プロジェクトが前回の取り込みから `generated/` を変えていなければ、親の版が衝突なしで入ってくるためです。親の版は派生プロジェクトの文字列を含みません。

取り込み前に `message_font.json` があったのに `font_profile.json` が `true` でなければ、スクリプトは止まります。初めての取り込みでは、親の `font_profile.json` (`false`) が入ってくるので、`true` に直してから `--regen` を実行します。

English contract: the generator is deterministic. The same TTFs (pinned by `fetch_fonts.sh`), the same freetype-py (pinned in `requirements.txt`), the same profile and the same literals give byte-identical tables. This is what makes rebuilding after every merge safe. Verified on 2026-10-08: the derived project's committed tables were reproduced byte for byte.

## Size

2026-10 時点 (このリポジトリ、`body_jis_x0208: false`):

| 書体 | 文字数 | flash |
|---|---|---|
| 16px 標準、16px 太字 | 各 449 | 約 40KB |
| 24px 太字 | 449 | 約 84KB |
| 48px、64px (数字) | 各 17 | 約 21KB |

合計は約 187KB です。テキスト用の文字集合に漢字を 1 文字足すと、3 書体の合計で約 400 バイト増えます。`body_jis_x0208: true` にすると、16px 標準書体は約 950KB になります。

生成には 20 秒ほどかかります (`true` では 1 分ほど)。

## License

生成した表は BIZ UDPGothic の改変版 (Modified Version) として SIL Open Font License 1.1 に従います。配布する際は `third_party/fonts/biz_udpgothic/OFL.txt` を同梱してください ([License](license.md))。
