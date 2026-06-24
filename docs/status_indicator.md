# Status Indicator

## Overview

`status_indicator` は、各 service の意味的な状態を CYD status LED の表示へ変換する application policy service です。

現在は `wifi_connection` の接続結果を購読し、次の表示へ変換します。

- credential 未設定: 赤点灯
- credential 設定済み、接続成功前または失敗後: 赤点滅
- 最新接続成功: 緑点灯

English supplement: Producers report semantic state only. `status_indicator` owns product-specific color, effect, and priority policy.

## Dependency Direction

```text
wifi_connection --connectivity status--> status_indicator --> cyd_status_led
```

`wifi_connection` は `status_indicator` と `cyd_status_led` のどちらにも依存しません。将来 `time_sync` や他の service を追加する場合も、各 producer からLEDパターンを直接指定せず、このserviceで優先順位を決定します。

English supplement: Lower-level services must not select LED colors or effects directly.
