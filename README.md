# Nゲージ サウンドボックス（M5Stack Tab5）

KATO のパワーパックで普段どおり N ゲージを運転しながら、M5Stack Tab5 で走行音や効果音を鳴らすサウンドボックス。

- パワーパックの入力（AC アダプタ側）の電流を測り、走行状態（停止・発進・走行中・加速・減速）を判定して、走行音を自動で切り替える。
- 画面のボタンで、警笛・ドア・発車ベルなどの効果音を鳴らす。
- 車両の種類（音のセット）を選ぶと、音がすべて切り替わる。実際に走っている車両とは連動しない。
- 列車本体とパワーパックは改造しない。

詳しくは [docs/requirements.md](docs/requirements.md)。

## 必要なもの

| もの | 内容 |
|---|---|
| M5Stack Tab5 | 本体 |
| M5Stack U200（Unit INA226-10A） | 電流センサー。Tab5 の Grove Port A につなぐ |
| DC ジャックの中継（オス・メス） | AC アダプタとパワーパックの間に入れ、U200 に電流を通す |
| KATO パワーパック スタンダードSX | 動作確認に使ったパワーパック |

## リポジトリの構成

| パス | 内容 |
|---|---|
| `firmware/NGaugeSoundBox/` | 本体のファームウェア |
| `libraries/TrainSense/` | 電流から走行状態を判定する（ハードウェアに依存しない） |
| `libraries/SoundSynth/` | 車両ごとの音の合成（ハードウェアに依存しない） |
| `libraries/SoundPack/` | 音源パックのマニフェストと WAV ヘッダの読み込み（ハードウェアに依存しない） |
| `ui/SoundBoxScreen.lgfxsb.json` | 画面のデザイン（LGFXScreenBuilder のプロジェクト） |
| `experiments/` | 実験用のスケッチ（電流の記録、描画の計測など） |
| `tests/` | PC 上のテスト（pytest-embedded と Arduino CLI） |
| `tools/` | 補助ツール（Serial の記録、画面のヘッダの生成、音源パックの確認など） |
| `docs/` | 要件、画面要件、実験計画と結果、音源パックの仕様、調査の記録 |

## ビルドと書き込み

[arduino-cli](https://arduino.github.io/arduino-cli/) を使う。ボードの定義とライブラリのバージョンは、各スケッチの `sketch.yaml` で固定している。

```sh
arduino-cli compile --profile m5stack_tab5 firmware/NGaugeSoundBox
arduino-cli upload  --profile m5stack_tab5 -p /dev/ttyACM0 firmware/NGaugeSoundBox
```

画面には、隣に置いた [LGFXScreenBuilder](https://github.com/tanakamasayuki/LGFXScreenBuilder) のリポジトリ（`../LGFXScreenBuilder`）を使う。まだリリースされていない機能（部品の表示・非表示の上書きと当たり判定）を使っているため。

画面のデザインを変えたら、ヘッダを作り直す。

```sh
node tools/gen_screen.mjs
```

## テスト（PC 上）

[uv](https://docs.astral.sh/uv/) と lang-ship:host（PC 上で Arduino のスケッチを動かす環境）を使う。

```sh
cd tests
cp .env.example .env   # 初回だけ
uv run --env-file .env pytest
```

| テスト | 内容 |
|---|---|
| `tests/smoke` | テスト環境そのものの確認 |
| `tests/train_state` | 記録した電流で走行状態の判定を確かめる |
| `tests/screens` | 全画面を PNG に書き出す（`tests/screens/output/`） |
| `tests/sound_preview` | 合成音を WAV に書き出す（`tests/sound_preview/output/`）。PC で聞いて確かめられる |
| `tests/sound_pack` | 音源パックのマニフェストと WAV ヘッダの読み込み |

## 音源パック

microSD に置く音のセット。仕様は [docs/sound-pack.md](docs/sound-pack.md)。PC で中身を確認できる。

```sh
python3 tools/check_pack.py /path/to/packs/e231
```

本体での microSD からの読み込みは、まだ実装していない。

## ライセンス

未定。
