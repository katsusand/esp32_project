# ESP-IDF Project Structure Guide

## 概要 / Overview

このリポジトリは **ESP-IDF Framework Project** です。  
本ドキュメントは、このプロジェクトにおける各ディレクトリの役割と、  
コード解析・編集時の優先順位を説明するものです。

特に AI / IDE / 開発者が、

- どこを主に読むべきか
- どこを低優先度で扱うべきか
- どこを編集してはいけないか

を明確にすることを目的とします。

---

## Directory Roles / ディレクトリの役割

### `main/`

**Primary Application Source**

- アプリケーションのエントリポイントを含む主要ソースコード
- 通常 `app_main()` を含む
- プロジェクト固有のトップレベルロジックを配置する

---

### `components/`

**User Authored Components**

- ユーザーが作成・管理する再利用可能コンポーネント
- モジュール化されたアプリケーション機能を配置する

---

### `managed_components/`

**Managed Dependency Components (Read Only)**

- IDF Component Manager により管理される依存コンポーネント
- 通常は **Read Only / 編集禁止**
- 必要に応じて参照してよいが、Primary Source ではない
- 手動編集しても再取得・更新時に上書きされる可能性がある

---

### `build/`

**Generated Build Artifacts**

- ESP-IDF / CMake により生成されるビルド生成物
- 中間生成物、オブジェクト、ELF、MAP、CMake Cache 等を含む

このディレクトリは通常：

- **Generated / 自動生成物**
- **Low Priority / 低優先度**
- **Not Source of Truth / 設計意図の所在ではない**

として扱うこと。

---

## Source Priority / 参照優先順位

通常のコード理解・設計把握・機能追加時は、  
以下の優先順位で参照すること。

1. `main/`
2. `components/`
3. `README.md` / `docs/`
4. `idf_component.yml` / `CMakeLists.txt` / `sdkconfig`
5. `managed_components/`
6. `build/`

---

## Build Environment / ビルド環境

このプロジェクトのビルドは **ESP-IDF v5.4.3** に依存する。  
そのため、通常のシェルでは `idf.py` にパスが通っていない前提で扱うこと。

`idf.py` を用いた設定・ビルド・書き込み・モニタ実行の前には、  
EIM 管理環境ではまず以下を実行して ESP-IDF 環境をロードすること：

```bash
source ~/.espressif/tools/activate_idf_v5.4.3.sh
```

その後、同じシェル環境で `idf.py build`、`idf.py flash`、`idf.py menuconfig` などを実行すること。

補足:

- EIM により ESP-IDF を管理している環境では、実際の Python venv が `~/.espressif/python_env/...` ではなく `~/.espressif/tools/python/v5.4.3/venv` に配置される場合がある
- そのため、EIM 前提の運用では `source ~/.espressif/tools/activate_idf_v5.4.3.sh` を優先する
- `~/.espressif/python_env/...` が存在する環境では `source ~/.espressif/v5.4.3/esp-idf/export.sh` がそのまま動くことがある
- 一方で `python_env` が無く、EIM の `tools/python/.../venv` だけが存在する環境では `source ~/.espressif/v5.4.3/esp-idf/export.sh` が単独では失敗する場合がある
- そのような環境では、`export.sh` より `activate_idf_v5.4.3.sh` を優先する

```bash
source ~/.espressif/tools/activate_idf_v5.4.3.sh
```

English supplement: In EIM-managed environments, prefer the generated activation script under `~/.espressif/tools/`. Plain `export.sh` may still work when a compatible `python_env` directory exists, but `activate_idf_v5.4.3.sh` is the primary entry point for EIM-based setups.

AI / IDE Assistant は以下を遵守すること：

- `idf.py` が常に利用可能だと仮定しない
- ビルド手順を案内・自動化する際は、EIM 前提ならまず `source ~/.espressif/tools/activate_idf_v5.4.3.sh` の読み込みを前提にする
- `export.sh` が `python_env` 不在または `python_env` 不一致で失敗した場合は、`source ~/.espressif/tools/activate_idf_v5.4.3.sh` を優先候補として案内する
- ビルド失敗時は、ソース不備だけでなく ESP-IDF 環境未読込の可能性も確認する
- `idf.py fullclean` が「別の環境で作られた build directory のため自動削除できない」旨で失敗した場合は、AI が勝手に削除せず、ユーザーに `build/` ディレクトリを手動で削除してもらうよう案内する

---

## Editing Rules / 編集ルール

### Safe To Edit / 編集可

- `main/`
- `components/`
- プロジェクト管理ドキュメント
- 設定ファイル (`idf_component.yml`, `sdkconfig.defaults` 等)

---

### Normally Do Not Edit / 通常編集禁止

- `managed_components/`
- `build/`

---

## Guidance For AI / AI向けガイドライン

AI / IDE Assistant は以下を遵守すること：

- `main/` と `components/` を **Primary Source of Truth** として扱う
- `managed_components/` は依存ライブラリとして低優先度参照する
- `build/` は Build / Toolchain / Linker / Generated Output の調査時のみ重点参照する
- 通常タスクで `build/` を設計根拠として扱わない
- `managed_components/` / `build/` への編集提案は原則行わない

---

## Documentation Style

このプロジェクトでドキュメントを追加・更新する場合は、以下の方針に従うこと。

- ドキュメント本文は、基本的に日本語で記述する
- 日本語だけでは表現しにくい部分、または誤解を招きそうな部分には英語の補足説明を入れる
- 英語補足は、人間向けの装飾ではなく、AI / IDE Assistant が意図を正確に解釈しやすくするために書く
- 見出しは、半角 ASCII 文字の英語で記述する
- コード上の識別子、API 名、設定名、ファイル名は、原則として実際の表記をそのまま使う

English supplement guidance:

- Prefer English supplements for intent, constraints, invariants, API contracts, and concurrency assumptions.
- Keep English supplements concise and explicit so AI tools can use them as implementation guidance.
- Do not translate every Japanese sentence into English. Add English only where it reduces ambiguity.

---

## Typical Task Guidance / 用途別ガイド

### 機能追加 / Feature Development

主に参照：

- `main/`
- `components/`

---

### 設計理解 / Architecture Understanding

主に参照：

- `main/`
- `components/`
- `README.md`
- `docs/`

---

### 依存ライブラリ調査 / Dependency Investigation

追加参照：

- `idf_component.yml`
- `managed_components/`

---

### ビルド問題調査 / Build Troubleshooting

追加参照：

- `CMakeLists.txt`
- `sdkconfig`
- `build/`

実行前提：

- EIM 管理環境では `source ~/.espressif/tools/activate_idf_v5.4.3.sh` を読み込んだシェルで `idf.py` を実行する
- `python_env` が存在する環境では `source ~/.espressif/v5.4.3/esp-idf/export.sh` でも動作することがある

ビルド確認の実績例：

- このリポジトリでは、EIM 前提の増分ビルド確認として `source ~/.espressif/tools/activate_idf_v5.4.3.sh && DEV=1 ninja -C build -v` が有効である
- EIM 管理かつ `python_env` が存在しない環境では、`source ~/.espressif/tools/activate_idf_v5.4.3.sh || true && python "$IDF_PATH/tools/idf.py" fullclean && python "$IDF_PATH/tools/idf.py" build` でフルクリーン後の再ビルド完走を確認している
- `python_env` が存在する環境では、`source ~/.espressif/v5.4.3/esp-idf/export.sh && DEV=1 ninja -C build -v` でも動作することがある
- `idf.py build` が既存 `build/` ディレクトリの Python / 環境差分で扱いづらい場合でも、環境を正しく読み込んだ上での `DEV=1 ninja -C build -v` は有効な切り分け手段になりうる
- 明示的に release 相当を確認したい場合を除き、通常のローカル開発ビルド確認では `DEV=1` を優先する

English supplement: For EIM-based setups, `source ~/.espressif/tools/activate_idf_v5.4.3.sh && DEV=1 ninja -C build -v` is the preferred quick compile verification flow. Prefer `DEV=1` for normal local development unless release behavior is being validated on purpose.

`idf.py fullclean` が失敗して `build/` の自動削除を拒否された場合：

- これは異なる環境・異なる Python / ESP-IDF セットアップで作られた `build/` が原因で起きうる
- AI / IDE Assistant は `build/` を自動削除しない
- ユーザーに `build/` ディレクトリを手動で削除してもらってから、あらためてビルドを案内する

---

## Notes

このプロジェクトでは、  
設計意図 (Design Intent) は主に **`main/` および `components/`** に存在する。

`managed_components/` および `build/` は補助的情報または生成物であり、  
通常の実装・設計判断における Primary Source ではない。
