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

計測処理は service 自身の task に置きます。

**「ブロックする処理が無いから task は不要」という判断はしないでください。** task を持つ理由は複数あります。

1. ブロックし得る呼び出しがある
2. 前面 app と独立した寿命・周期で動き続ける必要がある（止まると欠測になる）
3. 優先度を前面 app から独立させたい
4. スタックを分離したい
5. 処理時間が短いと保証できない
6. 状態とループの所有者として自然

`esp_timer` の periodic callback は手軽ですが、**共有の timer task で走ります**。そこで待つと Wi-Fi や lwIP を含むシステム全体の timer が遅延します。したがって esp_timer に置いてよいのは「短く、上限が読め、待たない」と**確信できる**処理だけです。

呼び先がバイナリブロブ（ESP-IDF の Wi-Fi API など）で所要時間を確認できない場合は、**確信できない側に倒して task にします**。`wifi_rssi_history` がこの判断の実例です。

English contract: absence of a blocking call is not sufficient reason to drop a task. If you cannot verify how long a call takes, keep it off the shared esp_timer task.

### 2. Public API is non-blocking

app から呼ぶ API は、待たずに返るものだけにします。状態変更を伴うものは、フラグを立てて task に notify する形にします。

```c
bool wifi_rssi_history_get(const int16_t **samples, uint16_t *count, uint16_t *revision);
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

ただし**毎周期記録しないでください**。値が数時間取れない状態はあり得ます（`wifi_rssi_history` なら radio が落ちている間）。1 秒ごとに欠測を積むと、見せたい実測履歴がバッファから押し出されて消えます。「ここで途切れた」という印は**遷移時の 1 個で足ります**。

English contract: a gap must stay visible as a gap -- compressing it silently is a data integrity bug. But record the break once, on transition. Repeating it every interval evicts the real history the buffer exists to hold.

### 6. Do not acquire shared resources on a view's behalf

**診断ビューのために共有資源を起こさないでください。** service は「既にある状態」を観測する側に徹します。

`wifi_rssi_history` はここで一度間違えました。`radio_manager` は誰も lease を持たなくなると radio を落とすため、初期実装ではサンプリングが 33 件で止まりました。そこで「グラフ表示中だけ lease を保持する」ようにしたところ、今度は**グラフを見るためだけに Wi-Fi が起動する**ようになりました。

最終的に lease は撤去しました。判断は次のとおりです。

- 資源を起こすかどうかは**製品の方針**であって、診断ビューが勝手に決めることではない
- 資源を長く保ちたいなら、**ユーザーに見える設定として出す**（設定の「ネットワーク2」page の「Wi-Fi切断」）
- 資源が落ちている間は、取り繕わず**そう表示する**（「Wi-Fi はオフです」）

どうしても view の寿命に資源を紐付ける場合は、app の `leave()` で必ず解放し、解放漏れの保険に最大保持時間を掛けます。`app_shell` の idle 復帰も `leave()` を通ります。ただしその前に、**本当に service 側が起こす必要があるのか**を疑ってください。

English contract: a diagnostic view observes; it does not power things up. Whether a shared resource stays alive is product policy, so surface it as a setting instead of deciding it inside a view. When the resource is down, say so.

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

- [ ] task を持つ理由を確認したか（ブロックの有無だけで判断していないか）
- [ ] 所要時間を確認できない呼び出しを、共有 esp_timer task に置いていないか
- [ ] 公開 API は非ブロッキングかつ冪等か
- [ ] バッファは service 所有で、submit 後も生存するか
- [ ] revision を持ち、内容変更時に加算しているか
- [ ] 欠測を飛ばさず、かつ遷移時の 1 個だけ記録しているか
- [ ] 共有資源を view のために起こしていないか（起こすなら設定として露出しているか）
- [ ] build feature switch 対象なら stub があるか
- [ ] composition から optional service として起動しているか（起動失敗でブートループしないか）
