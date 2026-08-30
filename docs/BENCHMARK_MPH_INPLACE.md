# Minimal-perfect-hash / in-place DP ベンチマーク

## 結論

フロンティア状態の持ち方そのものを変更することで、従来のsparse ranked map版より大幅に高速化できた。

この文書の固定幅ベンチマーク実装は現在
`src/experiments/moto_probe_mph_inplace_fixed.cpp` に保存されている。メインの
`src/moto_probe_mph_inplace.cpp` は、同じ状態・遷移を使うgrowable limb版である。

- `START位置 × balanced Motzkin語`を全てrank universeへ含めない
- 始点成分を「未対応端点を1個持つMotzkin状態」として直接列挙
- 有効状態だけにminimal perfect hashを割り当てる
- 2つのsparse mapを作り直さず、main配列と小さいdeferred配列をin-place更新
- 独立な左側状態グループをOpenMPで並列更新

n=20の理論rank空間は、従来版の3,136,046,299から次へ縮小した。

- main states: 258,215,664
- deferred states: 91,695,540

main側の状態番号空間は約91.8%減少している。計算量は依然としてフロンティア幅に対して指数的だが、列挙する状態集合と状態更新コストを大きく減らせた。

## 新規ファイル

- `src/moto_probe_mph_inplace.cpp`
- `scripts/build/build_probe_mph_inplace_cpp.bat`
- `tests/moto_probe_mph_inplace_tests.cpp`
- `scripts/test/test_probe_mph_inplace_cpp.bat`
- `scripts/benchmark/benchmark_mph_inplace_probe.ps1`
- `scripts/test/analyze_probe_mph_inplace.bat`
- `LICENSE_GGCOUNT_MIT.txt`

アルゴリズムのMateCodec / in-place更新はERATO MINATO ProjectのMITライセンス実装を基に、単純経路専用化、64ビットlimb、MSVC/OpenMP、期限処理、入力検証、統計出力を追加した。

## ビルド

```bat
scripts\build\build_probe_mph_inplace_cpp.bat
```

生成物:

- `build\moto_probe_mph_inplace.exe`: 1スレッド
- `build\moto_probe_mph_inplace_parallel.exe`: OpenMP版

OpenMP版は既定で最大16スレッド。3番目の引数で変更できる。

```bat
build\moto_probe_mph_inplace_parallel.exe 18 120 16
```

## テスト

```bat
scripts\test\test_probe_mph_inplace_cpp.bat
scripts\test\analyze_probe_mph_inplace.bat
```

確認済み:

- n=0〜16の既知値
- n=20のminimal perfect hash state数
- single / parallelのn=0〜13一致
- `/W4 /permissive-`
- MSVC `/analyze`警告なし
- n=17〜20の完走値が既知値と一致

## 実測

環境:

- Intel Core i9-12900KS
- RAM 63.8 GiB
- Visual Studio 2022 MSVC 14.44

### 既存v2速度優先版との直接比較

| n | 実装 | 状態 | 時間 | Peak private memory |
|---:|---|---|---:|---:|
| 16 | `build\moto_probe_optimized_v2_fast.exe` | COMPLETED | 46.257秒 | 1,166,671,872 bytes |
| 16 | MPH in-place parallel | COMPLETED | 2.378秒 | 278,450,176 bytes |
| 18 | `build\moto_probe_optimized_v2_fast.exe` | TIME_LIMIT | 60.383秒 | 8,818,769,920 bytes |
| 18 | MPH in-place parallel | COMPLETED | 23.224秒 | 2,147,684,352 bytes |

n=16では約19.5倍高速で、Peak private memoryは約76%減少した。
従来版の遷移記録数2,952,216,763に対し、新実装のdense state visitsは
1,136,852,016だった。単位は完全には同一ではないが、rank計算・sparse map探索を
各遷移で行わないことに加え、更新対象の総量自体も減っている。

### 高n完走結果

| n | threads | 時間 | Peak private memory | 結果 |
|---:|---:|---:|---:|---|
| 17 | 24 | 7.156秒 | 771,145,728 bytes | COMPLETED |
| 18 | 24 | 23.025秒 | 2,148,036,608 bytes | COMPLETED |
| 19 | 24 | 70.298秒 | 6,012,014,592 bytes | COMPLETED |
| 20 | 24 | 224.451秒 | 16,889,573,376 bytes | COMPLETED |

n=20結果:

```text
3962892199823037560207299517133362502106339705739463771515237113377010682364035706704472064940398
```

60秒時点でもn=20はrow=5, col=14、114 updatesまで進んだ。従来sparse版より少ないメモリで先まで進む。

## スレッド数調査

n=16:

| threads | 時間 |
|---:|---:|
| 4 | 2.640秒 |
| 8 | 2.319秒 |
| 12 | 2.218秒 |
| 16 | 2.246秒 |
| 20 | 2.352秒 |
| 24 | 2.392秒 |

n=18では16 threadsが22.398秒で最良、12 threadsが22.487秒、24 threadsが22.938秒だった。サイズにより最適値が異なるため、既定値は高n寄りの16とした。

## 残した検証・実験ファイル

依頼に従い、一時的な確認コード・失敗を含む実験も削除していない。

- `src/reference/moto_probe_mph_reference_port.cpp`
- `scripts/build/build_probe_mph_reference_port.bat`
- `scripts/benchmark/benchmark_mph_reference_probe.ps1`
- `src/experiments/moto_probe_mph_inplace_variable_limb_experiment.cpp`
- `scripts/build/build_probe_mph_inplace_variable_limb_experiment.bat`
- `src/experiments/moto_probe_mph_inplace_active_limb_experiment.cpp`
- `scripts/build/build_probe_mph_inplace_active_limb_experiment.bat`
- `scripts/build/build_probe_mph_inplace_pgo_experiment.bat`
- `src/experiments/moto_probe_mph_inplace_reachable.cpp` — 位置ごとの到達状態枝刈り。
  効果なしと判定し不採用。調査の詳細は `docs/REACHABLE_STATE_PRUNING.md`。
  この実装には未修正の欠陥が2つある（n>=5 で過小計上、n=15 で SIGSEGV）。

一時生成物はルートには置かず、`benchmarks/audit_logs/` へ集約している。

- `benchmarks/audit_logs/stdout/` - 各実行の標準出力
- `benchmarks/audit_logs/stderr/` - 各実行の標準エラー出力
- `benchmarks/audit_logs/build_artifacts/` - `.obj` / `.pgc` / `.pgd` / 静的解析 XML
- `benchmarks/audit_logs/README.md` - 40件の実行一覧（n、threads、status、経過時間）

実験結果:

- 可変active limbはsingle n=16を9.598秒から8.813秒へ改善した
- OpenMP版ではlimb昇格処理やメモリ帯域が支配的で、固定6 limbsより明確には速くならなかった
- PGOはsingle n=16を9.598秒から8.575秒へ改善した
- Parallel PGOは通常ビルドより遅くなったため、標準成果物には採用していない

## ベンチマーク再実行

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\benchmark\benchmark_mph_inplace_probe.ps1 `
  -N 18 -LimitSeconds 120 `
  -Executable .\build\moto_probe_mph_inplace_parallel.exe `
  -Tag rerun
```

stdout / stderrは監査用に削除せず、`benchmarks/audit_logs/stdout/` と
`benchmarks/audit_logs/stderr/` に保存される。`benchmarks/audit_logs/` は
Git管理外のため、証跡をバージョン管理したい場合は `.gitignore` の
`benchmarks/audit_logs/` 行を削除する。

## リポジトリ運用

このディレクトリはGitリポジトリである。管理対象はソース、ビルド/テスト/
ベンチマークスクリプト、ドキュメント、および確定した計測結果
（`benchmarks/results/benchmark_mph_inplace_results.csv` など）のみ。

`.gitignore` で除外しているもの:

- ビルド成果物（`.exe` / `.obj` / `.pdb` など）
- PGOプロファイル（`.pgc` / `.pgd`）
- probeスクリプトが書く一時ログと、その集約先 `benchmarks/audit_logs/`
- `archive/legacy/` - 初代実装一式（ディスクには残すが版管理しない）
- `archive/scratch/` - 使い捨ての調査用コードとその生成物
- `.workbuddy-ai/` - エージェント作業領域

`archive/` を版管理に戻したい場合は `.gitignore` の `archive/` 行を削除する。

`.gitattributes` で `.bat` / `.cmd` / `.ps1` はCRLF固定、その他はLFに正規化
している。Windowsのバッチファイルは改行がLFだと `goto` のラベル処理が
壊れることがあるため、この設定を維持する。
