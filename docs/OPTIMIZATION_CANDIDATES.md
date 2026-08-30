# growable limbs 採用後の最適化候補（暫定報告）

2026-08-30 時点。ここでは候補を集め、短い予備測定で優先順位を付けるところまでを行う。
速度向上率の確定や採否判断は次の検証で行う。

## 1. 現在地

- `Research/` は `.gitignore` の `/Research/` で追跡対象外にした。採用コミットにも含まれて
  いない。
- 要素を 1 limb から実行時に拡張する版を `src/moto_probe_mph_inplace.cpp` のメイン実装に
  採用した。固定幅版は比較用として `src/experiments/moto_probe_mph_inplace_fixed.cpp` に残した。
- 採用コミットは `3ebbef0`（`要素サイズ実行時成長版をメインに採用`）。
- n=1〜16 の既知値テスト、fixed 版テスト、v2 テスト、MSVC `/analyze`、n=18/20 の
  完走値と state visits の一致を確認済み。詳細は
  [GROWABLE_LIMB_EXPERIMENT.md](GROWABLE_LIMB_EXPERIMENT.md) を参照。

採用効果の再測定値は次の通り（16 threads）。

| n | fixed 中央値/実測 | growable 中央値/実測 | 変化 |
|---:|---:|---:|---:|
| 16 | 1.6223040 s | 1.1366702 s | -29.9%、1.43倍 |
| 18 | 19.9064856 s | 13.4424065 s | -32.5%、1.48倍 |
| 20 | 227.0820619 s | 156.4243705 s | -31.1%、1.45倍 |

既存調査の通り、現行ループは主としてメモリ転送律速である。したがって本報告では、
命令数だけでなく、走査する状態数、要素幅、配列本数、promotion のコピー量を優先する。

## 2. 今回の簡易検証

環境は Intel Core i9-12900KS（16 cores / 24 logical processors）、RAM 63.8 GiB、
MSVC 19.44.35228、Windows、`/O2 /openmp /DNDEBUG`。すべての完走結果で `paths` は一致した。

### 2.1 スレッド数

n=16 は各2回の平均。短時間ケースなので絶対値より傾向を見る。

| threads | 2回の elapsed | 平均 |
|---:|---:|---:|
| 1 | 6.6511942 / 6.8419126 s | 6.7465534 s |
| 2 | 3.4150909 / 3.4891066 s | 3.4520988 s |
| 4 | 1.7145437 / 1.8017919 s | 1.7581678 s |
| 6 | 1.3522249 / 1.3357822 s | 1.3440036 s |
| 8 | 1.1374066 / 1.1668429 s | 1.1521248 s |
| 10 | 1.0699063 / 1.1540048 s | 1.1119556 s |
| 12 | 1.0462885 / 1.0635642 s | **1.0549264 s** |
| 16 | 1.1280870 / 1.1259448 s | 1.1270159 s |
| 20 | 1.1516477 / 1.1556007 s | 1.1536242 s |
| 24 | 1.2186624 / 1.1837346 s | 1.2011985 s |

n=16 では12 threadsが16 threadsより6.4%速かった。n=18 の各1回だけの予備値は
8 / 10 / 12 / 16 threadsでそれぞれ14.6495 / **13.3471** / 13.6603 / 13.4873秒だった。
最適点は問題サイズとOSの配置で動くため、「常に12」ではなく、スレッド数・affinity・
OpenMP schedule をまとめて再測定する価値がある。

### 2.2 `/GL /LTCG`

通常のparallel版と `/GL /LTCG` 版を n=16、12 threadsでウォームアップ後に交互に5回実行した。

| build | 5回の中央値 |
|---|---:|
| 通常 `/O2 /openmp` | 1.0987543 s |
| `/O2 /GL /openmp /LTCG` | 1.1512347 s |

LTCG版は4.8%遅く、この構成では採用根拠がない。現行ソースが実質1翻訳単位であることとも
整合する。今後再確認するなら、LTCG単体ではなく代表入力で学習したPGOとして扱う。

### 2.3 32-bit limb の机上試算

現在と同じ「1 updateで総数は高々2倍」という安全側の証明を32 bit単位に適用し、
31 updates後から32 updatesごとに拡張すると仮定した。追加のcarry命令とpromotion時間を
含まないため、速度予測ではなく転送量の候補値である。

| n | 32-bit最大limbs | 推定平均要素幅 | 現行64-bit版 | 差 |
|---:|---:|---:|---:|---:|
| 16 | 7 | 18.206 B | 20.794 B | -12.45% |
| 18 | 9 | 22.620 B | 25.123 B | -9.96% |
| 20 | 11 | 27.333 B | 29.810 B | -8.31% |

帯域律速という既存結果から実装候補にはなる。ただしpromotion回数が約2倍になり、
`_addcarry_u32` の命令数も増えるので、実測なしに上表を速度向上率とはみなさない。

### 2.4 main / deferred の別幅化

Git管理外の一時プローブで n=18 の全342 updates後に両配列を別々に走査した。

| 配列 | 全期間の最大bit長 | 必要64-bit limbs |
|---|---:|---:|
| main | 261 bit | 5 |
| deferred | 258 bit | 5 |

64 / 128 / 192 / 256 bitを超えたupdateは、mainが97 / 177 / 257 / 337、deferredが
99 / 181 / 259 / 339だった。別幅化で遅らせられるのは各境界で2〜4 updatesだけで、
理想化した総転送量差も約0.3%にとどまる。**幅だけを別管理する案は低優先度**とする。
一方、deferred は各行末（n=18では18 updatesごと）に全て0へ戻ることも確認できたため、
幅ではなく寿命・密度・依存関係を利用する案は別候補として残す。

### 2.5 序盤だけのsparse処理の上限

既存プローブでは n=18 のmain非ゼロ密度が update 20 / 40 / 60 / 80 / 100 で
1.2% / 24.2% / 65.4% / 91.1% / 99.0%、update 160でほぼ100%となる。
標本間を線形補間し、現行limb幅で「非ゼロ要素だけを費用ゼロで処理できる」と仮定すると、
削れる走査バイト数の理想上限は約 **5.37%** だった。active-list管理や重複排除の費用は
含まない。

全期間をsparse化する案には見込みがないが、最初の数十updatesだけdense走査を避けて
閾値で切り替えるhybrid案には小さな余地がある、という位置付けである。

## 3. 候補の優先順位

### A: 次に小さな実験を作る候補

| 順位 | 候補 | 根拠・期待できる上限 | 主なリスク / 次の確認 |
|---:|---|---|---|
| 1 | **安全なpromotion時期の後ろ倒し** | n=18で現行はupdate 64/128/192/256前に拡張するが、実測のmainは97/177/257/337で初めて境界を超えた。oracle通りなら平均要素幅25.123 B→19.789 B（-21.2%） | 実測値依存は不可。遷移からのより強い上界、定期的な総数scan＋次回までの `2^k` 上界、またはoverflow sidecarを設計し、正しさの証明とscan費用を測る |
| 2 | **32-bit growable limbs** | 安全側スケジュールのままでも推定転送量 -8.3〜-12.5% | promotion回数とcarry命令が増える。n=16/18で実装A/B後、n=20へ進む |
| 3 | **threads / affinity / schedule調整** | 今回 n=16で最大6.4%、n=18で約1%の差。12900KSはP/E core混在 | n=18/20で5回以上。10/12/16 threads、P-core優先、`dynamic/static/guided` とchunkを交互測定 |
| 4 | **promotionコピーの並列化または分割配置** | n=20のpromotionは6.3〜12.8秒、総時間の4〜8%。完全除去の上限は約1.04〜1.09倍 | 現行の後方in-placeコピーには依存がある。別領域へのparallel copyは最後の拡張で一時約30.8 GBとなるが、この機械では容量内。peak memoryとNUMA first-touchも測る |
| 5 | **deferred配列の除去・縮小** | deferredは n=18/20とも全number要素の約26%。既存の意味を壊したcache閉じ込めprobeでは16 threadsで最大31.9%短縮したため、上限は大きい | 値を壊さない依存グラフ解析が必要。位置ごとの非ゼロ密度をまず測り、per-row sparse buffer、遷移のcycle/SCC順、局所一時領域で置換可能か調べる |

順位1の -21.2% は「未来を知っている場合の転送量上限」であり、そのまま採用可能な数値では
ない。定期scan案では、value/deferredの総和 `S` を正確に求めた時点から、k updates後の
任意要素を `2^k S` 以下とする方法が候補になる。追加scan自体が帯域を使うため、scan間隔を
含めてA/Bする。

### B: 条件付き候補

| 候補 | 暫定判断 |
|---|---|
| **序盤active list→dense切替** | 理想上限約5.37%。状態IDの重複排除をbitsetで行っても得になるか、n=16/18だけで先に確認する |
| **deferredの非ゼロbitset / row-local化** | 行末で0に戻ることは確認済み。非ゼロ密度が十分低い位置だけ、dense `Count` 読みをbitset読みに置換できる可能性がある |
| **OpenMP groupの静的重み付き分割** | 現在はbitcount降順＋`schedule(dynamic)`。groupごとのblock/state数を事前計算しLPT分割すればruntime schedulingを消せるが、帯域律速なので数%候補 |
| **large pages / 64 KiB page実験** | TLB miss軽減候補。ただしWindows large pageは権限が必要でreserve+commit同時となり、現行の段階commitと衝突する。out-of-place promotion案と組み合わせる場合だけ試す |
| **growable版でPGO再学習** | 過去のparallel PGOは悪化し、今回LTCGも+4.8%。代表的なn=18/20で学習し直す場合のみ再検証する |
| **最初の一歩を対称性で固定** | 反射で答えを2倍にできても、dense配列の全走査は減らない。active-listまたは位置別状態空間と組み合わせる場合だけ意味がある |

### C: 現時点では低優先度または見送り

- **main/deferredの幅だけを別管理**: n=18の理想転送量差が約0.3%。複雑さに見合わない。
- **既存のreachable block枝刈り**: state visitsは2.1%しか減らず6.7倍遅くなった。
- **無効branch skip、switch hoist、active limbだけの短縮**: 既存実測で効果なしまたは小さい。
- **LTCG単体**: 今回の中央値で4.8%悪化。
- **全期間sparse map**: 終盤はmain状態が100%非ゼロになる。
- **全ZDDの保持**: count-only用途にはメモリ費用が大きすぎる。後述の既報ではn=18相当の
  全ZDD生成に約500 GBを要する。
- **CRTを同一機械で順番に回す**: 必要な総bit数分の転送は残る。分散して1台あたりの
  メモリを下げる目的なら再評価できる。

## 4. ネット調査から得た示唆

### 4.1 問題に近いfrontier / ZDD研究

Minatoの2025年の解説は、この格子の対角頂点間simple path数え上げを直接扱っている。
ZDDは同値なfrontier状態を共有できる一方、全ZDD保持はn=18で約500 GBとなり、count-onlyでは
一層ずつ捨てるbreadth-first方式がn=21まで、grid専用のminimal perfect hash方式がn=26まで
到達したと整理されている。これは現行MPH dense DPを捨てて全ZDDへ戻すより、現在の配列幅・
補助配列・転送回数を削る方向を支持する。

Kawaharaらのfrontier-based searchは、未処理部分に対して同じ意味を持つ部分解を統合する
一般枠組みを示す。現行のMate/Motzkin状態は既にこの統合を強く行っているため、利用すると
しても「位置別にさらに同値な状態があるか」の監査用であり、直ちに置換案とはしない。

### 4.2 pruning

Jensenのfuture-connection表現では、残り接続に必要な最小追加辺数を幅O(W)で計算でき、
状態数の減少が小さくてもpruning CPUを大幅に減らせた。ただし対象は最大長Nを持つSAW/SAP
列挙であり、本件は全長のcorner-to-corner simple pathを数える。長さ上限による
`n_cur + n_add > N` は直接使えない。

転用可能性があるのは、長さではなく「残り領域では接続不能」「境界条件を満たせない」状態を
安く証明できる場合だけである。現行遷移が閉路・孤立を既に排除している可能性が高く、既存の
reachable実験も失敗しているため、優先度は低い。

### 4.3 実装系

- OpenMPの `OMP_PROC_BIND` はthread migrationを抑え、`close` / `spread` 等の配置方針を
  指定できる。今回のthread数依存が再現するなら、P/E core混在の影響を切り分ける材料になる。
- Windows large pageはTLB効率を改善し得るが、`SeLockMemoryPrivilege`、物理メモリの
  reserve+commit同時実行、非pageable等の制約がある。現行のincremental commitとは
  同時に使えない。
- MSVCのLTCGはcross-module inlineやinterprocedural optimizationを行うが、現行は実質1翻訳
  単位で、今回も改善しなかった。代表入力を使うPGOの方がまだ筋が良いが、過去のparallel
  PGO悪化を踏まえて低優先度とする。
- rank-based connectivity DPはtreewidthに対するsingle-exponential algorithmを与えるが、
  現行は平面・非交差接続に特化したMotzkin符号化である。別アルゴリズムの長期調査枠とする。

## 5. 次回の検証順

1. **安全なpromotion後ろ倒しの上限測定**: updateごとの全要素総和bit長をn=16/18で記録し、
   定期scan＋`2^k` 上界で得られる安全なscheduleとscan費用を机上計算する。
2. **32-bit growable試作**: n=1〜16既知値とoverflow検査後、n=16/18を64-bit版と交互測定する。
3. **10/12/16 threads＋affinity＋schedule**: n=18を5回、勝者だけn=20で確認する。
4. **promotion方式**: n=18で別領域parallel copy、勝てた場合だけn=20のpeak memoryを測る。
5. **deferredプロファイル**: 位置ごとの非ゼロ率、読み書き回数、依存グラフのcycle/SCCを採る。
6. 上記が不発なら、hybrid sparse、large page、PGOの順で進める。

この順序は暫定であり、1〜3は独立した実験として比較版を残し、1項目ずつ採否を決める。

## 6. 一次資料

- [Shin-ichi Minato, chapter on BDD/ZDD techniques and grid-path counting (2025)](https://link.springer.com/chapter/10.1007/978-981-96-0668-9_2)
- [Iwan Jensen, “A new transfer-matrix algorithm for exact enumerations: self-avoiding walks on the square lattice” (2013)](https://arxiv.org/html/1309.6709)
- [Kawahara et al., “Frontier-based Search for Enumerating All Constrained Subgraphs with Compressed Representation” (2017)](https://www.jstage.jst.go.jp/article/transfun/E100.A/9/E100.A_1773/_article)
- [Donald Knuth, official program archive: SIMPATH / SIMPATH-REDUCE](https://www-cs-faculty.stanford.edu/~knuth/programs.html)
- [OpenMP 5.1, `OMP_PROC_BIND`](https://www.openmp.org/spec-html/5.1/openmpse61.html)
- [Microsoft, Large-Page Support](https://learn.microsoft.com/en-us/windows/win32/memory/large-page-support)
- [Microsoft, `/LTCG` (Link-time Code Generation)](https://learn.microsoft.com/en-us/cpp/build/reference/ltcg-link-time-code-generation?view=msvc-170)
- [Microsoft, Profile Guided Optimization](https://learn.microsoft.com/en-us/windows/apps/develop/performance/profile-guided-optimization)
- [Bodlaender et al., “Deterministic single exponential time algorithms for connectivity problems parameterized by treewidth”](https://arxiv.org/abs/1211.1505)

## 7. 再現に使った主なコマンド

```powershell
# thread sweep
.\build\moto_probe_mph_inplace_parallel.exe 16 120 <threads>
.\build\moto_probe_mph_inplace_parallel.exe 18 180 <threads>

# 通常のproduction test
.\scripts\test\test_probe_mph_inplace_cpp.bat

# fixed比較版
.\scripts\test\test_probe_mph_inplace_fixed_cpp.bat
```

LTCG版と配列別bit幅probeは今回の候補抽出専用で、成果物・scratchソースはGit管理外に置いた。
