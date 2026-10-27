# 音源パックの仕様（format 1）

音源パックは、車両 1 種類分の音を 1 つのフォルダにまとめたもの。microSD にコピーするだけで追加できる（要件 F-10）。

## 1. フォルダ構成

```
/packs/
  e231/              ← パック 1 つ（フォルダ名は英数字）
    pack.json        ← マニフェスト（必須）
    horn.wav
    door.wav
    run.wav
    ...
  d51/
    pack.json
    ...
```

- 1 フォルダ = 1 パック。フォルダ名は英数字・`-`・`_` だけにする。
- パック内のファイルは、`pack.json` と同じフォルダに置く（サブフォルダは使わない）。

## 2. 音声ファイル

- WAV（リニア PCM、16bit）。
- モノラルまたはステレオ。
- サンプリング周波数は 8,000〜48,000Hz。24,000Hz を推奨する（本体の合成音と同じ）。

## 3. マニフェスト（pack.json）

UTF-8 の JSON。例:

```json
{
  "format": 1,
  "name": "E231系 通勤電車",
  "kind": "電車",
  "running": {
    "type": "wav",
    "loop": "run.wav",
    "idle": "idle.wav",
    "pitch": [0.7, 1.5],
    "volume": [0.3, 1.0]
  },
  "buttons": [
    { "label": "警笛", "file": "horn.wav", "mode": "hold" },
    { "label": "ドア", "file": "door.wav" },
    { "label": "発車ベル", "file": "bell.wav" },
    { "label": "車内放送", "file": "announce.wav" },
    { "label": "駅到着", "files": ["chime.wav", "door.wav"] },
    { "label": "雨", "file": "rain.wav", "mode": "toggle" }
  ],
  "credits": [
    { "files": ["horn.wav", "door.wav"], "author": "作者名", "license": "CC0-1.0", "source": "https://example.com/" }
  ]
}
```

### 3.1 項目

| 項目 | 必須 | 内容 |
|---|---|---|
| `format` | ○ | 仕様の版。いまは `1` |
| `name` | ○ | 画面に出す車両名（63 バイトまで） |
| `kind` | | 種別（電車・SL など。31 バイトまで） |
| `running` | ○ | 走行音（3.2） |
| `buttons` | ○ | 効果音ボタン。1〜6 個（3.3） |
| `credits` | | 音のライセンスとクレジット（3.4） |

### 3.2 走行音（running）

`type` で種類を選ぶ。

| `type` | 内容 | 使う項目 |
|---|---|---|
| `synth` | 本体の合成音を使う | `preset`: `electric`（電車）・`steam`（蒸気機関車）・`diesel`（気動車） |
| `wav` | 音声ファイルをループ再生し、level に合わせて高さと音量を変える | `loop`（必須）、`idle`、`pitch`、`volume` |
| `none` | 走行音なし | − |

- `loop`: 走っているときにループ再生するファイル。
- `idle`: 止まっているときにループ再生するファイル（任意。気動車のアイドリングなど）。
- `pitch`: level 0 と 1 のときの再生速度の倍率。既定は `[1.0, 1.0]`（変えない）。
- `volume`: level 0 と 1 のときの音量（0〜1）。既定は `[1.0, 1.0]`。

### 3.3 効果音ボタン（buttons）

| 項目 | 必須 | 内容 |
|---|---|---|
| `label` | ○ | ボタンの表示名（63 バイトまで。全角 5 文字程度に収めると見やすい） |
| `file` | どちらか一方 | 鳴らすファイル |
| `files` | どちらか一方 | 順番に鳴らすファイルの並び（2〜4 個）。駅到着のような一連の音に使う |
| `mode` | | `oneshot`（既定。押すと最後まで鳴る）・`hold`（押している間だけ鳴る）・`toggle`（押すたびにループ再生のオンとオフが切り替わる） |

### 3.4 クレジット（credits）

| 項目 | 必須 | 内容 |
|---|---|---|
| `files` | ○ | 対象のファイル |
| `author` | ○ | 作者 |
| `license` | ○ | ライセンス（SPDX の識別子。例: `CC0-1.0`、`CC-BY-4.0`） |
| `source` | | 入手元の URL |

- 本体の設定画面のクレジットに表示する。
- 本リポジトリに同梱する初期パックは、`CC0-1.0` のものだけにする（要件 F-12）。

## 4. 確認ツール

PC でパックの中身を確認できる。

```sh
python3 tools/check_pack.py /path/to/packs/e231
```

マニフェストの項目、ファイルの有無、WAV の形式、クレジットの抜けを確認し、問題があれば一覧で出す。

## 5. 実装の状況

- マニフェストと WAV ヘッダの読み込み: `libraries/SoundPack`（PC 上のテストあり: `tests/sound_pack`）。
- Tab5 の microSD からの読み込みと、パックの音での再生: 未実装（microSD のピンを実機で確認してから行う）。
