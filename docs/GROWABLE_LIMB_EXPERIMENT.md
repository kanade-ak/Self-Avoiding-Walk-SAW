# compact growable limb 実装と整合性監査

日付: 2026-08-30

対象は `README.md`、`docs/` の最適化記録、`src/` の現行・旧・参照・実験実装、
`tests/`、ビルド／テスト／ベンチマークスクリプト、および確定済み計測結果。

## 結論

`src/moto_probe_mph_inplace.cpp` としてメイン実装へ採用した。
現行の minimal-perfect-hash / in-place DP と状態・遷移は同じまま、数値配列を
1 limbから必要な幅まで物理的に拡張する。

16 threadsの初回実測では次の改善となった。

| n | 固定版 | growable版 | 変化 | 高速化 |
|---:|---:|---:|---:|---:|
| 16 | 1.5963063秒（3回中央値） | 1.1129306秒（3回中央値） | **-30.3%** | 1.43倍 |
| 18 | 20.4194325秒（3回中央値） | 12.8603649秒（3回中央値） | **-37.0%** | 1.59倍 |
| 20 | 244.628秒（既存確定値、1回） | 158.5655625秒（1回） | **-35.2%** | 1.54倍 |

同じセッションでfixed版とgrowable版を取り直した再現測定でも改善した。

| n | fixed版 | growable版 | 変化 | 高速化 |
|---:|---:|---:|---:|---:|
| 16 | 1.6223040秒（3回中央値） | 1.1366702秒（3回中央値） | **-29.9%** | 1.43倍 |
| 18 | 19.9064856秒（3回中央値） | 13.4424065秒（3回中央値） | **-32.5%** | 1.48倍 |
| 20 | 227.0820619秒（1回） | 156.4243705秒（1回） | **-31.1%** | 1.45倍 |

n=20の結果は既知値と一致した。

```text
3962892199823037560207299517133362502106339705739463771515237113377010682364035706704472064940398
```

したがって改善は有効で、2セッションを通じて1.43〜1.59倍となった。ただし、事前調査の
約1.9〜2.0倍という値は、安全な実装でそのまま達成できる保証値ではなかった。

## 事前調査との整合性

### 整合していた点

- ボトルネックがメモリ転送量という結論は、今回も要素の平均幅と時間の対応で再現した。
- 既存の `variable_limb` / `active_limb` 実験は要素を48 Bのまま保ち、加算するlimb数だけを
  変える。今回の実装は配列要素そのものを8 B刻みで拡張するため、既存実験の不採用と
  growable案の有望判定は矛盾しない。
- minimal-perfect-hash state数、state visits、既知のpathsは固定版と一致した。
- Motzkin状態数は現在のフロンティア符号化における正確なuniverseである。ただし
  「理論下限」はこの符号化の範囲での主張であり、別の問題定式化全般に対する下限の証明
  ではない。

### 実装時に修正した事前記録

1. `docs/OPTIMIZATION_HEADROOM.md` に記録していた「ピークメモリも16.9 GBから約8.7 GBへ
   半減」という予測は、
   配列全体を最終的に6 limbsへ拡張する設計とは両立しない。途中の使用量と転送量は減るが、
   完走時のピークはfixed版と同水準になる。実測peak privateはn=20の2回で
   16,889,233,408 / 16,889,245,696 bytesだった。
2. 同文書の約1.94倍は実測ビット成長から求めた平均24.7 Bをそのまま時間へ当てはめた推定。
   正しさを測定値に依存させない今回の上界スケジュールでは平均29.810 Bとなり、再配置
   を含む実測は1.45〜1.54倍だった。

### 監査で見つかった既存記録の注意点

1. `docs/BENCHMARK_MPH_INPLACE.md` の「`/analyze`警告なし」は静的limb化より前の記録。
   比較用fixed版単体では `FixedCount<1>` のコンパイル時に空となる上位limbループについて
   C6294が2件出る。メイン版ではfixed版のinclude範囲だけ警告を抑制しており、候補版固有の
   警告はない。解析自体はexit code 0で、機能上の欠陥ではない。
2. `docs/REACHABLE_STATE_PRUNING.md` は修正版について「n=4〜14で一致」と
   「n=13でSIGSEGV」を同時に記載しており、n=13の条件または実験版を区別して書く必要がある。
   到達枝刈りを不採用とする性能結論には影響しない。

## 実装

- `VirtualAlloc(MEM_RESERVE)` で最大幅のアドレス空間だけを予約し、使用するページだけを
  差分commitする。予約済み先頭範囲を再commitすると一時commit chargeが増えるため、
  ページ境界で未commitの末尾だけを対象にした。
- 拡張時は要素を末尾からコピーし、同じ予約領域の中で `K` limbsから `K+1` limbsへ
  安全に再配置する。一時的な新旧2配列は持たない。
- 更新本体を `updateBlock<K>` として実体化し、1 updateにつき1回だけlimb数をdispatchする。
  要素ごとの実行時分岐はない。
- 初期構成数を1とし、1セル更新で各構成から生成される後続は高々2つなので、t updates後の
  全構成数は高々 `2^t`。この上界からupdate 64、128、…の直前に拡張する。既存のn別最大
  limb表も最終上限として使い、`operator+=` のcarry検査を残す。
- 完了までにn=16は3回、n=18は4回、n=20は5回拡張した。

## ベンチマーク詳細

環境: Intel Core i9-12900KS（16 cores / 24 logical processors）、RAM
68,456,280,064 bytes、MSVC 19.44.35228、`/O2 /openmp /DNDEBUG`、16 threads。
監視は `scripts/benchmark/benchmark_mph_inplace_probe.ps1` を使用した。

| n | 実装 | 3回のelapsed（秒） | 中央値 | 平均要素幅 | promotion時間 |
|---:|---|---|---:|---:|---:|
| 16 | fixed | 1.5870283 / 1.5963063 / 1.6131648 | 1.5963063 | 32 B | — |
| 16 | growable | 1.1129306 / 1.1641438 / 1.0684919 | 1.1129306 | 20.794 B | 約0.05秒 |
| 18 | fixed | 20.4194325 / 20.5546524 / 19.0443514 | 20.4194325 | 40 B | — |
| 18 | growable | 12.9559374 / 12.8482630 / 12.8603649 | 12.8603649 | 25.123 B | 約0.60秒 |
| 20 | fixed | 244.628（既存の同日確定値） | — | 48 B | — |
| 20 | growable | 158.5655625 | — | 29.810 B | 6.3075385秒 |

再現測定:

| n | 実装 | 3回のelapsed（秒） | 中央値 | 平均要素幅 | promotion時間（run 1） |
|---:|---|---|---:|---:|---:|
| 16 | fixed | 1.5223745 / 1.6281402 / 1.6223040 | 1.6223040 | 32 B | — |
| 16 | growable | 1.0823742 / 1.1437984 / 1.1366702 | 1.1366702 | 20.794 B | 0.0501790秒 |
| 18 | fixed | 19.2765642 / 19.9196860 / 19.9064856 | 19.9064856 | 40 B | — |
| 18 | growable | 13.7238550 / 13.4424065 / 13.4367108 | 13.4424065 | 25.123 B | 0.6257197秒 |
| 20 | fixed | 227.0820619 | — | 48 B | — |
| 20 | growable | 156.4243705 | — | 29.810 B | 12.8307931秒 |

代表監視値:

| n | fixed peak private | growable peak private | 判定 |
|---:|---:|---:|---|
| 16 | 187,334,656 bytes | 187,338,752 bytes | 同水準 |
| 18 | 1,792,475,136 bytes | 1,792,442,368 bytes | 同水準 |
| 20 | 16,889,225,216 bytes（既存16-thread probe） | 16,889,233,408 bytes | 同水準 |

n=16とn=18の比較は同じセッションで交互に取得した。n=20 fixedは
`benchmarks/results/benchmark_mph_inplace_static_limbs_ab.txt` の同日・同環境・同一16-thread
確定値であり、今回のgrowable runとは交互実行ではない。生の確定値は
`benchmarks/results/benchmark_mph_inplace_growable.csv` に保存した。

再現測定ではn=16/18をfixed→growableの順で各3回交互に、n=20をgrowable→fixedの順で
各1回測定した。n=20のpromotion時間は初回6.308秒、再現測定12.831秒と振れたが、
総時間の短縮は両方で再現した。

## 検証

- `scripts/test/test_probe_mph_inplace_cpp.bat`: n=1〜16の既知値、1→2→3 limbsの
  in-place再配置を確認。成功。
- `scripts/test/test_probe_mph_inplace_fixed_cpp.bat`: 比較用fixed版。成功。
- `scripts/test/test_probe_optimized_v2_cpp.bat`: adaptive / fast v2。成功。
- 新規ソースを `/W4 /permissive- /analyze` で解析。候補版固有の警告なし。
- n=18、n=20の完走値、state数、state visitsが既存値と一致。
- reference port、legacy、variable limb、active limbも再ビルドに成功。reference portには
  既存のC4101/C4244/C4457、reachable版には既存のC4458が出るため、リポジトリ全ソースが
  `/W4` warning-freeという状態ではない（READMEはその主張をしていない）。reachable版は
  文書化済みの実行時欠陥があるためビルド確認だけとし、実行していない。

## 追加ファイル

- `src/moto_probe_mph_inplace.cpp`
- `src/experiments/moto_probe_mph_inplace_fixed.cpp`
- `scripts/build/build_probe_mph_inplace_fixed_experiment.bat`
- `tests/moto_probe_mph_inplace_tests.cpp`
- `tests/moto_probe_mph_inplace_fixed_tests.cpp`
- `scripts/test/test_probe_mph_inplace_fixed_cpp.bat`
- `benchmarks/results/benchmark_mph_inplace_growable.csv`
- `docs/GROWABLE_LIMB_EXPERIMENT.md`
