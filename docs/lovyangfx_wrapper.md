# LovyanGFX Wrapper

## Overview

`lovyangfx_wrapper` は、`third_party/lovyangfx_upstream` の LovyanGFX ソースを ESP-IDF component としてビルドするためのラッパーコンポーネントです。

アプリケーションが直接使う公開 API は基本的にありません。`cyd_display` が `#include <LovyanGFX.hpp>` して LCD と touch を扱うための依存コンポーネントです。

English supplement: Treat this component as a build integration layer for LovyanGFX, not as project application logic.

## Build Role

`components/support/lovyangfx_wrapper/CMakeLists.txt` は、`third_party/lovyangfx_upstream/src` 以下から LovyanGFX の C/C++ ソースを `GLOB` で集め、ESP-IDF component として登録します。

登録される include path は以下です。

- `components/support/lovyangfx_wrapper/include`
- `third_party/lovyangfx_upstream/src`

`components/support/lovyangfx_wrapper/include/driver/i2s.h` は、古い include path を期待する LovyanGFX 側コード向けに、ESP-IDF v5 系の `driver/i2s_std.h` と `driver/i2s_types.h` を include する互換ヘッダです。

English supplement: The local `driver/i2s.h` shim is compatibility glue for ESP-IDF v5 header layout.

## Excluded Sources

現在の CMake では、以下の LovyanGFX ソースを明示的に除外しています。

- `Panel_M5HDMI.cpp`
- `Bus_HUB75.cpp`
- `Bus_Parallel8.cpp`
- `Panel_CVBS.cpp`
- ESP32-S2 の `Bus_Parallel8.cpp`
- ESP32-S2 の `Bus_Parallel16.cpp`

これは CYD の SPI TFT/touch 用途に不要な backend を外し、ビルド対象を絞るためです。

## Dependencies

`lovyangfx_wrapper` は以下の ESP-IDF component に依存します。

- `nvs_flash`
- `efuse`
- `esp_lcd`
- `driver`
- `esp_timer`

`cyd_display` はこの component に依存し、プロジェクト側の画面 API を提供します。通常のアプリケーションコードでは `lovyangfx_wrapper` ではなく `cyd_display` を利用してください。
