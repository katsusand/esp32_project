# Sampling Service Pattern

## Overview

センサー値やリンク品質のように「時間とともに変わる値を集めて画面に出す」機能は、このリポジトリでは **service が値を集め、app は薄い view に徹する** 形で書きます。

このドキュメントはその規約です。実装例は 2 つあります。

- `components/services/wifi_rssi_history` — polled snapshot 型
- `components/services/time_tick` — push / subscribe 型

English supplement: a service owns the task and the buffer. A foreground app renders a view over it and owns nothing.

## Why Not In The App

`app_shell` は foreground app を 1 つの task 上で協調実行します。app の `step()` は同じ task を占有するため、そこに計測処理を置くと 2 つの問題が起きます。

1. **画面を離れると計測が止まる。** app が active でない間は `step()` が呼ばれません
2. **ブロックすると UI ごと止まる。** `step()` の中で待つと、描画も入力処理も止まります

2 は実際に踏みました。`radio_manager_acquire()` を `step()` から呼んだ結果、Wi-Fi 起動の約 2.8 秒間 UI が固まりました。

English contract: never block in `step()`. Anything that waits on hardware, radio, or another task belongs on the service side.

## Shape

### 1. Service owns the task

計測処理とブロックし得る呼び出しは、service 自身の task に置きます。

`esp_timer` の periodic callback は手軽ですが、**ブロックする処理を置いてはいけません**。esp_timer callback は共有の timer task で走るため、そこで待つとシステム全体の timer が遅延します。待つ可能性があるなら専用 task にします。

### 2. Public API is non-blocking

app から呼ぶ API は、フラグを立てて task に notify するだけにします。

```c
esp_err_t wifi_rssi_history_set_monitoring(bool monitoring);  /* notify するだけ */
```

冪等にしておくと、`step()` から毎フレーム呼んでも安全になります。

### 3. Buffer is service-owned and long-lived

`cyd_display` はサンプル配列をコピーしません。widget にはポインタを渡すため、バッファは submit 後も生存している必要があります。service の static 配列がその条件を自然に満たします。

```c
bool wifi_rssi_history_get(const int16_t **samples, uint16_t *count, uint16_t *revision);
```

### 4. Revision counter

内容が変わるたびに加算するカウンタを持たせ、そのまま `cyd_display_sparkline_t.revision` へ渡します。

これが無いと、`cyd_display` の dirty-rect 差分は widget を値で比較するだけなのでポインタの先の変化に気付けず、**グラフが永久に再描画されません**。

同じ値は app 側の「再描画すべきか」の判定にも使えます。`system_info_app` の RSSI ページは、前回描画した revision と比較して変化があったときだけ再描画しています。

### 5. Record dropouts, do not skip them

値が取れなかったときは、サンプルを飛ばすのではなく**欠測値を記録**します。

飛ばすと欠測が時間軸から消えてグラフが詰まり、「値が取れていなかった」のか「その間ずっと安定していた」のか区別できなくなります。`cyd_display_sparkline_t` の `has_gap_value` / `gap_value` で、欠測は線の途切れとして描かれます。

English contract: a gap must stay visible as a gap. Compressing it silently is a data integrity bug, not a cosmetic one.

### 6. Scope shared resources to the view

radio のような共有資源は、**画面に出ている間だけ**確保します。

`radio_manager` は誰も lease を持たなくなると radio を落とします。service が常時 lease を持つとこの省電力設計が無意味になり、逆に一切持たないと `wifi_rssi_history` の初期実装のようにサンプリングが 33 件で止まります。

正解は view の寿命に合わせることです。app の `leave()` で必ず解放し、解放漏れの保険として `max_hold_ticks` を掛けます。`app_shell` の idle 復帰も `leave()` を通るため、これで lease が view より長生きしません。

### 7. Respect the build feature switch

Wi-Fi など build feature switch の対象に依存する service は、stub 実装を用意します。app 側に `#if` を書かせないためです。詳細は `docs/build_feature_switches.md` を参照してください。

## Two Delivery Shapes

| | polled snapshot | push / subscribe |
|---|---|---|
| 例 | `wifi_rssi_history` | `time_tick` |
| 取得 | `_get()` で最新状態を読む | `_subscribe()` で queue を受け取る |
| 向き | グラフ、統計、履歴 | イベント、時刻更新、状態遷移 |
| 取りこぼし | 起きない（常に最新を読む） | queue 長を超えると起きる |

履歴を描くなら polled snapshot、1 回ごとの発生を逃したくないなら push を選びます。

## Checklist

新しい sampling service を書くときの確認項目です。

- [ ] ブロックし得る処理が service の task 側にあるか
- [ ] 公開 API は非ブロッキングかつ冪等か
- [ ] バッファは service 所有で、submit 後も生存するか
- [ ] revision を持ち、内容変更時に加算しているか
- [ ] 欠測を飛ばさず記録しているか
- [ ] 共有資源の確保が view の寿命に一致しているか
- [ ] build feature switch 対象なら stub があるか
- [ ] composition から optional service として起動しているか（起動失敗でブートループしないか）
