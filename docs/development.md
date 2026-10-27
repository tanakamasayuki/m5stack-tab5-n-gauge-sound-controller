# 開発のルール

このリポジトリで作業するときのルール。人でも AI でも同じルールに従う。概要とビルド・テストの手順は [README.md](../README.md)。

## 1. 進め方

- **ブランチ:** `main` で直接作業する。作業用のブランチは作らない。
- **コミット:** 区切りのよいところ（文書の更新、機能 1 つ、実験 1 回分など）で、確認を取らずにコミットしてよい。
  - メッセージは日本語で、何をしたかを 1 行で書く。
  - AI の署名や `Co-Authored-By` などの行は付けない。
- **ルールの置き場所:** ルールはこの文書にまとめる。AI のツール専用のファイル（CLAUDE.md、AGENTS.md など）は作らない。
- **プッシュ:** 利用者から指示があったときだけ行う。
- **要件:** 決めたこと・変えたことは `docs/` の該当する文書に反映する（要件は `docs/requirements.md`、画面は `docs/screen-requirements.md`、実験は `docs/experiments.md`）。
- **報告:** 実機で確かめていないことを「動いた」と書かない。PC 上で確かめたこと、ビルドが通っただけのこと、実機で確かめたことを分けて書く。

## 2. 書き方

- 文書、コメント、コミットメッセージは日本語で書く。周りのコードの書き方（コメントの量、名前の付け方、括弧の位置、2 字下げ）に合わせる。
- 文書は平易な日本語で書く。表と箇条書きを使い、決まったことと未決のことを分ける。

## 3. 構成とバージョン

- バージョンは各スケッチの `sketch.yaml` で固定する。新しく作るときも、次にそろえる。

| 対象 | 内容 |
|---|---|
| Tab5 の FQBN | `esp32:esp32:m5stack_tab5:USBMode=hwcdc,CDCOnBoot=cdc`（Serial を USB で出すために必要） |
| ESP32 コア | `esp32:esp32 (3.3.12)` |
| M5Unified / M5GFX | `0.2.26` / `0.2.33` |
| LGFXVirtualCanvas | `1.4.0` |
| ArduinoJson | `7.4.2` |
| PC 上のテスト | `lang-ship:host (1.7.1)`。画面を描くテストは `lang-ship:host:host:mode=lgfx` と LovyanGFX `1.2.26` |
| LGFXScreenBuilder | `dir: ../../../LGFXScreenBuilder` のように、隣のリポジトリを参照する（未リリースの機能を使うため） |

- ローカルのライブラリは `libraries/<名前>/` に置き、`sketch.yaml` の `dir:` で参照する。
- **ハードウェアに依存しない処理（判定、合成、読み込み）は、ライブラリに分けて PC 上でテストする。** 実機に依存する部分（スピーカー、I2C、画面）は `firmware/` 側に薄く置く。
- PC 上のテストは `tests/<機能>/` に、`<機能>.ino`、`sketch.yaml`、`test_<機能>.py` を置く（pytest-embedded）。実行は `cd tests && uv run --env-file .env pytest`。
- 画面のデザインの正本は `ui/SoundBoxScreen.lgfxsb.json`。ヘッダ（`SoundBoxScreen.h`）は `node tools/gen_screen.mjs` で作り直す。ヘッダを手で直さない。
- 実験の記録（`logs/`）はコミットしない。テストに使う記録は、`tools/raw_to_windows.py` などで変換して `tests/<機能>/input/` に置く。
- 音源は、CC0 のもの（または自作して CC0 にしたもの）だけをリポジトリに入れる。

## 4. つまずきやすい点

- **Arduino の自動プロトタイプ:** `.ino` では、関数の前方宣言がファイルの先頭に自動で作られる。関数の引数や戻り値に使う構造体は、最初の関数より前に定義する。テンプレート関数は、計測などで必要ならマクロで書く。
- **Tab5 の画面の向き:** パネル本来の向きは縦（720x1280）。横向きで使うので、`M5.Display.setRotation(1)` を `screen.begin()` より前に呼ぶ。
- **Tab5 の描画の速さ:** LGFXVirtualCanvas のタイルの分け方で、描画時間が大きく変わる（`docs/rendering-performance.md`）。画面全体の描き直しは約 0.2 秒かかるので、変化があったときだけ描き直し、頻繁に変わる部分（メーターなど）はスプライトで部分的に描く。
- **U200（INA226）:** I2C アドレス `0x41`、シャント抵抗 5mΩ。Tab5 の Port A（`M5.Ex_I2C`、SDA=53・SCL=54）につなぐ。Grove の 5V は `M5.Power.setExtOutput(true)` で有効にする。
- **パワーパックの電流:** KATO スタンダードSX の入力電流には、約 146Hz の PWM が乗る。10ms ごとの平均値でも揺れるので、判定は TrainSense の平滑化を通す。
- **PC での時間の計測:** WSL2 上では、ms 単位の時間が回によって数十倍ばらつくことがある。関数単位で、何度も測った最小値を使う。シーン全体の時間は実機で測る。

## 5. 主な文書

| 文書 | 内容 |
|---|---|
| `docs/development.md` | 開発のルール（この文書） |
| `docs/requirements.md` | 要件定義書 |
| `docs/screen-requirements.md` | 画面要件 |
| `docs/experiments.md` | 実験の計画と結果（電流の記録、状態の判定） |
| `docs/sound-pack.md` | 音源パックの仕様 |
| `docs/rendering-performance.md` | 描画が重い原因の調査 |
