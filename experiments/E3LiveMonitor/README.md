# E3LiveMonitor

TrainSense（走行状態判定）を Tab5 上で動かし、判定結果をその場で画面に出すスケッチ。実験 E3 用（[docs/experiments.md](../../docs/experiments.md)）。

- 画面の上半分：状態（停止・発進・走行中・減速）と level のバー
- 画面の下半分：平滑化した電流、学習した基準値、傾き、marker
- 画面をタップすると marker が 1 増える

## ビルドと書き込み

```sh
arduino-cli compile --profile m5stack_tab5 experiments/E3LiveMonitor
arduino-cli upload  --profile m5stack_tab5 -p /dev/ttyACM0 experiments/E3LiveMonitor
```

## 記録

```sh
uv run tools/capture_serial.py --port /dev/ttyACM0 --out logs/e3_run1.csv
```

`t_ms,mA,state,level,marker` を 10ms ごとに出す。`t_ms,mA` の 2 列は、そのまま tests/train_state の入力に使える。

電流の読み取りと判定は専用のタスク（コア 0）で回すので、画面の描き換えでは止まらない。1 区切り（10ms）に 20 サンプル未満しかないときは、次の区切りに持ち越す。画面の sps は、1 秒あたりのサンプル数。
