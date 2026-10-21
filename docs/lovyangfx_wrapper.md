# LovyanGFX Wrapper

## Overview

`lovyangfx_wrapper` は、`third_party/lovyangfx_upstream` の LovyanGFX ソースを ESP-IDF component としてビルドするためのラッパーコンポーネントです。

アプリケーションが直接使う公開 API は基本的にありません。`cyd_display` が `#include <LovyanGFX.hpp>` して LCD を扱うための依存コンポーネントです。タッチは LovyanGFX ではなく `xpt2046_softspi` が扱います。

English supplement: Treat this component as a build integration layer for LovyanGFX, not as project application logic.

## Version

`third_party/lovyangfx_upstream` は submodule で、LovyanGFX 1.2.34 のタグを指しています。

`cyd_display` が使うのは、パネル (`Panel_ST7789` / `Panel_ILI9341` / `Panel_ILI9341_2`)、`Bus_SPI`、`Light_PWM` (バックライト)、スプライトと `pushImage` だけです。文字は `cyd_ui_fonts` の自前の描画で、LovyanGFX のフォントは使いません。LovyanGFX を更新したときは、この範囲を実機で確かめてください。

English supplement: when bumping the submodule, re-verify on a unit only the parts `cyd_display` uses: panel init, SPI/DMA writes, sprites, and the PWM backlight.

## Build Role

`components/support/lovyangfx_wrapper/CMakeLists.txt` は、upstream の `boards.cmake/esp-idf.cmake` と同じソース一覧を ESP-IDF component として登録します。

LovyanGFX 1.2.x の途中から、各プラットフォームのコードは `.inl` ファイルになり、`lgfx_v1_platforms.cpp` などの少数の翻訳単位から `#include` されます。各 `.inl` は自分のターゲットのときだけ中身がコンパイルされます。そのため、以前のように `platforms/esp32/*.cpp` を `GLOB` で集めたり、使わない backend をファイル単位で外したりはできません。使わないコードはリンク時に捨てられます。

English supplement: since the `.inl` unity layout, the platform sources are not separate files any more; follow upstream's SRCS list instead of globbing, and do not try to exclude backends per file.

登録される include path は `third_party/lovyangfx_upstream/src` だけです。以前あった `driver/i2s.h` の互換ヘッダは、LovyanGFX が `driver/i2s_types.h` を先に探すようになったので削除しました。

## Excluded Sources

upstream の一覧から、以下の 2 つだけを外しています。どちらも独立した翻訳単位で、CYD にないハードウェア用です。

- `panel/Panel_M5HDMI.cpp`
- `touch/Touch_GSLx680.cpp`

## Dependencies

`lovyangfx_wrapper` は以下の ESP-IDF component に依存します。upstream の ESP-IDF 5.1 以降の指定と同じです。

- `nvs_flash`
- `efuse`
- `esp_lcd`
- `driver`
- `esp_timer`
- `esp_mm`

ESP-IDF 6 以降では、さらに `esp_driver_ledc`、`esp_driver_i2s`、`hal` にも依存します。v6 では旧 `driver` component がこれらを公開の依存として引き込まなくなったためで、upstream の指定と同じです。

English supplement: the extra IDF 6 requirements are selected by `IDF_VERSION_MAJOR` in the wrapper's CMakeLists.txt; keep them in step with upstream `boards.cmake/esp-idf.cmake` when bumping LovyanGFX.

`cyd_display` はこの component に依存し、プロジェクト側の画面 API を提供します。通常のアプリケーションコードでは `lovyangfx_wrapper` ではなく `cyd_display` を利用してください。
