# 自己回避歩道

自己回避経路の動的計画法（DP）を検証するC++実装と、ビルド・テスト・ベンチマーク用スクリプトを管理するリポジトリです。Windows/MSVCを前提にしています。

## ディレクトリ構成

| 場所 | 内容 |
|---|---|
| `src/` | 現行実装（MPH in-place、v2メモリ優先、v2速度優先） |
| `src/experiments/` | 実験実装（可変limb・active limb・到達状態枝刈り、いずれも不採用） |
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

MPH in-place版をビルドして実行する場合:

```bat
scripts\build\build_probe_mph_inplace_cpp.bat
build\moto_probe_mph_inplace_parallel.exe 18 120 16
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

## ドキュメント

- [MPH / in-place DP ベンチマーク](docs/BENCHMARK_MPH_INPLACE.md)
- [自己回避経路 DP 最適化 v2](docs/OPTIMIZATION_V2.md)
- [到達状態枝刈りの調査記録](docs/REACHABLE_STATE_PRUNING.md)
- [GGCOUNT由来コードのライセンス](LICENSE_GGCOUNT_MIT.txt)

ビルド生成物は `build/` に、ベンチマークの標準出力・標準エラーとPGO/解析生成物は `benchmarks/audit_logs/` に集約されます。どちらも再生成可能なためGitでは管理しません。
