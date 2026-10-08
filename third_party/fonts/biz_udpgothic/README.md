# BIZ UDPGothic

UI の日本語フォント (`components/support/cyd_ui_fonts`) の元になる書体です。

- 提供元: https://github.com/googlefonts/morisawa-biz-ud-gothic
- 取得したコミット: `18934af56b9c003ca58c54bffbf226848cb11032`
- ライセンス: SIL Open Font License 1.1 (`OFL.txt`)

| File | SHA-256 |
|---|---|
| `BIZUDPGothic-Regular.ttf` | `258d7156c165f2ff774b6efee637c22c3b950de0d8a10e501137061bc8085d01` |
| `BIZUDPGothic-Bold.ttf` | `30eba52fc837e8b62c97d4b82e6706583149fb7294e3712dd71a655eaea80a90` |
| `OFL.txt` | `e753d7155d53c747d037a445e584c8ecfca6dd79846db610417e282a736b28bc` |

TTF はリポジトリに含めません (各 4.6MB)。フォントを作り直すときは `scripts/ui_fonts/fetch_fonts.sh` で取得してください。上の SHA-256 を照合します。

English supplement: The TTFs are git-ignored on purpose. The generated subset tables under `components/support/cyd_ui_fonts/generated/` are a Modified Version under OFL 1.1 and must stay distributed together with `OFL.txt`. The OFL for this family declares no Reserved Font Name.
