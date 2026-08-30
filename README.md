# 自己回避歩道

自己回避経路の動的計画法（DP）を検証するC++実装と、ビルド・テスト・ベンチマーク用スクリプトを管理するリポジトリです。Windows/MSVCを前提にしています。

## ディレクトリ構成

| 場所 | 内容 |
|---|---|
| `src/` | 現行実装（MPH in-place、v2メモリ優先、v2速度優先） |
| `src/experiments/` | 比較用fixed版と実験実装（可変limb、active limb、到達状態枝刈り） |
| `src/reference/` | 参照実装の移植版 |
| `src/legacy/` | 旧世代の実装 |
| `tests/` | C++テストコード |
| `scripts/build/` | ビルドスクリプト |
| `scripts/test/` | テスト・静的解析スクリプト |
| `scripts/benchmark/` | ベンチマークスクリプト |
| `benchmarks/results/` | 確定・比較用の計測結果 |
| `benchmarks/audit_logs/` | ローカル監査ログ（Git管理外） |
| `build/` | 実行ファイルなどの生成物（Git管理外） |
| `docs/` | 実装・ベンチマークの記録 |
| `archive/legacy/` | 初代実装一式（Git管理外） |

## まず実行するコマンド

```bat
scripts\test\test_probe_mph_inplace_cpp.bat
scripts\test\test_probe_optimized_v2_cpp.bat
```

MPH in-place版（要素サイズ実行時成長）をビルドして実行する場合:

```bat
scripts\build\build_probe_mph_inplace_cpp.bat
build\moto_probe_mph_inplace_parallel.exe 18 120 16
```

比較用のfixed版をビルドして実行する場合:

```bat
scripts\build\build_probe_mph_inplace_fixed_experiment.bat
build\moto_probe_mph_inplace_fixed_parallel.exe 18 120 16
```

v2の速度優先版とメモリ優先版を同じ条件で比較する場合:

```bat
scripts\benchmark\benchmark_probe_optimized_v2.bat 20 60
```

MPH in-place版の監視付き計測:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\benchmark\benchmark_mph_inplace_probe.ps1 `
  -N 18 -LimitSeconds 120 `
  -Executable .\build\moto_probe_mph_inplace_parallel.exe `
  -Tag rerun
```

## 不採用とした実験

性能改善を狙って実装したが、効果が確認できず現行実装には取り込んでいないもの。
詳細は各ドキュメントを参照。

| 実験 | 結果 | 記録 |
|---|---|---|
| 位置ごとの到達可能状態だけを処理する | 正しくしても state visits は2.1%減、実時間は6.7倍悪化。不採用 | [docs/REACHABLE_STATE_PRUNING.md](docs/REACHABLE_STATE_PRUNING.md) |
| 可変 limb / active limb | single n=16 で 9.598秒 → 9.217秒 / 8.813秒。OpenMP版では優位性なし | [docs/BENCHMARK_MPH_INPLACE.md](docs/BENCHMARK_MPH_INPLACE.md) |
| PGO | single n=16 で 9.598秒 → 8.575秒。Parallel PGO は逆に遅くなり不採用 | [docs/BENCHMARK_MPH_INPLACE.md](docs/BENCHMARK_MPH_INPLACE.md) |
| 無効ペアのブロックを丸ごと skip | 全訪問の15.7%を削れるが n=16 で -2.0%（悪化）。キャッシュライン被覆率が常に100%のため転送量が減らない | [docs/OPTIMIZATION_HEADROOM.md](docs/OPTIMIZATION_HEADROOM.md) |
| switch のループ外ホイスト | n=16 で +0.2%。分岐予測はボトルネックではない | [docs/OPTIMIZATION_HEADROOM.md](docs/OPTIMIZATION_HEADROOM.md) |

## 採用した最適化

比較用fixed版 `src/experiments/moto_probe_mph_inplace_fixed.cpp` のボトルネックは演算でも
分岐でもなく **メモリ転送量**（時間が要素サイズにほぼ比例する）。まず要素のlimb数を
nごとに最小にした
（`FixedCount<LIMB_COUNT>` + `limbCountForN()`、n=16 は6→4、n=18/19 は6→5）。

| n | 14 | 15 | 16 | 17 | 18 | 19 | 20 |
|---|---:|---:|---:|---:|---:|---:|---:|
| limb 数 | 3 | 3 | 4 | 4 | 5 | 5 | 6 |
| 要素サイズ | 24 B | 24 B | 32 B | 32 B | 40 B | 40 B | 48 B |
| 変化 (16 thr) | -7.1% | **-54.8%** | **-35.4%** | **-32.6%** | **-18.4%** | **-15.7%** | -3.5% |

`paths` は n=0..17 で 6 limbs 固定版と完全一致、n=18/19/20 は過去の実行結果とも一致。
第4引数で limb 数を強制できる（A/B 計測用。省略時は自動）。

```bat
build\moto_probe_mph_inplace_fixed_parallel.exe 18 120 16
build\moto_probe_mph_inplace_fixed_parallel.exe 16 120 16 6   : limb 数を6に固定
```

## 要素サイズの実行時成長版

`src/moto_probe_mph_inplace.cpp` は、数値配列を1 limbで開始し、安全な上界に基づいて
必要な幅まで物理的に拡張する。以下の結果を受けてメイン実装として採用した。
16 threadsでfixed版と交互に再測定した結果は次のとおり。

| n | fixed版 | growable版 | 時間短縮 | 高速化 |
|---:|---:|---:|---:|---:|
| 16 | 1.6223秒（3回中央値） | 1.1367秒（3回中央値） | **29.9%** | 1.43倍 |
| 18 | 19.9065秒（3回中央値） | 13.4424秒（3回中央値） | **32.5%** | 1.48倍 |
| 20 | 227.0821秒（1回） | 156.4244秒（1回） | **31.1%** | 1.45倍 |

全測定で `paths` と state visits はfixed版に一致した。途中の転送量は減るが、完走時には
最終limb数まで拡張するため、ピークメモリはfixed版と同水準になる。

詳細は [growable limb実装と測定](docs/GROWABLE_LIMB_EXPERIMENT.md) および
[最適化余地の調査](docs/OPTIMIZATION_HEADROOM.md)。採用後に集めた次の候補と予備測定は
[growable limbs 採用後の最適化候補](docs/OPTIMIZATION_CANDIDATES.md) にまとめた。

## ドキュメント

- [MPH / in-place DP ベンチマーク](docs/BENCHMARK_MPH_INPLACE.md)
- [自己回避経路 DP 最適化 v2](docs/OPTIMIZATION_V2.md)
- [最適化余地の調査](docs/OPTIMIZATION_HEADROOM.md)
- [growable limbs 採用後の最適化候補（暫定報告）](docs/OPTIMIZATION_CANDIDATES.md)
- [growable limb実装と測定](docs/GROWABLE_LIMB_EXPERIMENT.md)
- [到達状態枝刈りの調査記録](docs/REACHABLE_STATE_PRUNING.md)
- [GGCOUNT由来コードのライセンス](LICENSE_GGCOUNT_MIT.txt)

ビルド生成物は `build/` に、ベンチマークの標準出力・標準エラーとPGO/解析生成物は `benchmarks/audit_logs/` に集約されます。どちらも再生成可能なためGitでは管理しません。
