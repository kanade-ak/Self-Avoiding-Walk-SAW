# 自己回避経路 DP 最適化 v2

> 追補: 状態集合そのものを削減する後続実装は
> `moto_probe_mph_inplace.cpp` / `BENCHMARK_MPH_INPLACE.md`を参照。

既存ファイルを変更せず、速度優先版とメモリ優先版を新規ファイルとして追加した。

## ファイル

### メモリ優先・上限保証版

- `moto_probe_optimized_v2.cpp`
- `build_probe_optimized_v2_cpp.bat`
- 出力: `moto_probe_optimized_v2.exe`

主な変更:

- 状態カウンタを64ビット単位で自動昇格
- n=20の840辺に対する辺部分集合上界を保持できる最大896ビット
- 4K rankページを採用
- 12ビット局所添字と4ビットページ内世代を1つの16ビット値へ格納
- rankページを256ページ単位の再利用プールから確保
- 第1 rank表を高さ4ビット、順位28ビットの32ビット値へ圧縮
- 第2 rank表を32ビットで直接構築し、到達不能な開始高さを除外
- rank universeとMotzkin表の32ビット超過を明示的に検査
- 最大896ビットを超えた場合は例外で停止し、誤った値を返さない

カウンタは実際に必要なlimbだけを持つ。例えばn=20の探索初期は1 limb（64ビット）で動作する。

### 速度優先版

- `moto_probe_optimized_v2_fast.cpp`
- `build_probe_optimized_v2_fast_cpp.bat`
- 出力: `moto_probe_optimized_v2_fast.exe`

主な変更:

- 64K rankページと固定384ビットカウンタは維持
- 第1 rank表を64ビットから32ビットへ圧縮
- 第2 rank表を一時64ビット表なしで直接構築
- 到達不能な第2表の開始高さを除外
- rankページ確保に`make_unique_for_overwrite`を使用し、未使用indicesのゼロ初期化を回避
- 無効だった`reserve_hint`を削除

メモリ優先版よりメモリを使うが、遷移速度を優先する場合に使用する。

## 共通改善

- `std::from_chars`による引数の厳密な検証
- 不正なn、制限時間、余分な引数をエラー化
- Visual Studio Community / Professional / Enterprise / Build Toolsを探索するビルドスクリプト
- `/W4 /permissive- /DNDEBUG`でビルド
- `std::bad_alloc`と演算例外を捕捉
- rankページとエントリの確保バイト数を出力
- deadline確認間隔を16,384状態から4,096状態へ短縮

## テスト

- `moto_probe_optimized_v2_tests.cpp`
- `moto_probe_optimized_v2_fast_tests.cpp`
- `test_probe_optimized_v2_cpp.bat`
- `benchmark_probe_optimized_v2.bat`

実行:

```bat
test_probe_optimized_v2_cpp.bat
```

確認内容:

- n=0〜13の既知経路数
- 小さいuniverseの全rankと、大きいuniverseのランダムrank/unrank往復
- 4K / 64K rankページの最終局所添字
- 600世代のclearとstale entry排除
- 重複状態の加算
- 64ビット境界を越える自動昇格
- 2^450までの正確な保持（旧384ビット上限を越えるテスト）

## 実行方法

```bat
build_probe_optimized_v2_cpp.bat
moto_probe_optimized_v2.exe 20 60

build_probe_optimized_v2_fast_cpp.bat
moto_probe_optimized_v2_fast.exe 20 60
```

引数を省略した場合は`n=18`、制限時間60秒。

両方を同じ引数で順に計測する場合:

```bat
benchmark_probe_optimized_v2.bat 20 60
```

## 単発計測の参考値

環境: Intel Core i9-12900KS / 63.8 GiB RAM。数値は単発であり、厳密な比較には反復計測が必要。

メモリ優先版はn=16も完走し、既知値
`68745445609149931587631563132489232824587945968099457285419306`
と一致した。この時は4 limbs、経過64.424秒だった。

n=15完走:

| 版 | 経過時間 | rankページ + entry確保量 |
|---|---:|---:|
| 速度優先 | 14.785秒 | 約400 MiB |
| メモリ優先 | 19.939秒 | 約244 MiB |

n=20、10秒制限での観測例:

| 版 | 遷移数 | rankページ + entry確保量 |
|---|---:|---:|
| 速度優先 | 515,964,327 | 約8.87 GiB |
| メモリ優先 | 484,350,130 | 約2.59 GiB |

到達位置が異なるため、メモリ量は同一ワークロード比較ではない。メモリ優先版はこの計測でカウンタ1 limbを使用した。

## 選択基準

- n<=16や速度重視: `moto_probe_optimized_v2_fast.exe`
- n=18〜20や64 GiB内での完走可能性を優先: `moto_probe_optimized_v2.exe`

速度優先版は固定384ビットであり、中間カウンタも384ビット以内という前提が残る。メモリ優先版はn<=20について896ビットまで自動昇格するため、この前提を持たない。

CRT複数パスと並列化は今回の2版には混在させていない。まず単一スレッドのメモリ挙動を固定し、必要なら別実装として追加する。
