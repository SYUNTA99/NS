# エフェクトを文字の定義から作る道

編集ソフト (Effekseer 1.80.7) の画面を開かずに、XML の定義から `.efkefc` を作る。作った物がゲームと同じ実行側 (`Source/ThirdParty/Effekseer`、1.80.7) で読めるか、中身が定義どおりかを確かめ、フレームごとの PNG で見る。

## 置き方 (初めの 1 回)

| 要る物 | 置き方 |
|---|---|
| Effekseer 1.80.7 の Windows 版 | 公式の配布物を展開し、`Effekseer.exe` と `bin/` が並ぶ `Tool` フォルダを環境変数 `NS_EFFEKSEER_TOOL` に入れる (例: `setx NS_EFFEKSEER_TOOL "<展開した場所>\Effekseer1.80.7Win\Tool"`)。道はリポジトリに書かない |
| Python 3 と Pillow | `pip install pillow`。一覧の画像と素材の描画に使う |
| .NET 9 の SDK | 照合の道具 `efkxml` を組む |
| Visual Studio 2022 | `efkprobe` を組む (他のビルドと同じ) |

`NS_EFFEKSEER_TOOL` が無い・中に `Effekseer.exe` か `bin/EffekseerCore.dll` が無い時は、`efkbuild.py` がその旨を出して止まる。道具の場所を読むのは `efkbuild.py` の `find_tool` だけで、`efkxml` には組む時 (`-p:EffekseerToolBin=`) と走らせる時 (`--tool-bin`) に渡す。

## 使い方

```
Tools\@build_effects.cmd                        # efkprobe を組み、defs/*.efkproj を全部通す
Tools\@build_effects.cmd spark_min --frames 40  # 1 本だけ
Tools\@build_effects.cmd nobuild --install      # 組まずに通し、全部通ったら Assets\Effects へ置く
python Tools/effects/defs/make_textures.py      # 素材の図形を描き直す (defs/Texture/)
```

- 失敗が 1 本でもあれば終了コード 1。`--install` は全部通った時だけ写す
- 作業の出力は `build/effects/` (注釈を外した写し・`.efkefc`・`Texture/`・`frames/<名前>/frame_NNN.png`・`frames/<名前>_sheet.png`・組んだ `efkxml`)
- ゲームへ渡るのは `--install` で写す `Assets/Effects/<名前>.efkefc` と `Assets/Effects/Texture/` だけ。定義を `Assets/` の下に置かないのは、`@package_release.cmd` が `Assets` を丸ごと配るため
- `efkprobe` は Debug で組む (premake の `EffectProbe`、`Tools/effects/premake.lua`)。出荷の構成では建たない
- 置いた絵がゲームで読めるかは試し `ShippedEffects.EveryEffectPreloads` が縛る (`Assets/Effects/` の全 `.efkefc` を `EffectScene::Preload` し、素材の欠けで落ちる。1 本も無ければ「確かめられなかった」で落ちる)

## 置いてある物

| 物 | 役目 |
|---|---|
| `defs/<名前>.efkproj` | 定義。編集ソフトの XML の形そのもの (後述の書き方) |
| `defs/Texture/` | 定義が読む素材。`defs/` の下のフォルダは全部、書き出し先と `Assets/Effects/` の同じ場所へ写る |
| `defs/make_textures.py` | 白い閃光の丸 `flash.png` と火花の筋 `spark.png` を描く。同じ入力から同じ画素を描く |
| `efkbuild.py` | 本体。下の 1〜7 を 1 本ずつ回す |
| `efkxml/` | .NET 9 のコンソール。編集ソフトの中核 `EffekseerCore.dll` を画面なしで呼ぶ (`dump`・`check`) |
| `efkprobe/` | C++ のコンソール。実行側で読んで節の木を出し、画面外に描いて PNG を書く |
| `premake.lua` | `efkprobe` を ConsoleApp にしてゲームと同じ `effekseer` の lib に繋ぐ |

## efkbuild.py が 1 本ごとにする事

1. **写す。** 定義から XML の注釈を外した写しを `build/effects/<名前>.efkproj` に書き、`defs/` の下のフォルダを同じ所へ写す
2. **書き出す。** `Effekseer.exe -cui -in <写し> -o build/effects/<名前>.efkefc`
3. **利用者の跡を探す。** 書き出した `.efkefc` の塊 (INFO・EDIT を解いた中身・BIN_) に、この機械の利用者の名前・利用者のフォルダの道があれば失敗。INFO と EDIT はドライブから始まる道 (`C:/` など) も失敗にする
4. **照合する。** `efkxml check <写し> <.efkefc>`
   - (a) 定義に書いた葉 (値を持つ要素) が、編集ソフトに読まれた後も同じ値で残っているか。残らない葉は綴りの誤りか既定と同じ値で、どちらも失敗 (定義には既定から動かす値だけを書く)
   - (b) 定義を開いて書き出した XML と、`.efkefc` を開いて書き出した XML が一字一句同じか
   - 例外は `IsLoop` だけ。編集ソフトの再生を繰り返すかのフラグで、開くと常に True になり、実行側に渡らない
5. **実行側で読む。** `efkprobe <.efkefc> build/effects/frames/<名前> --frames N`
   - `Effect::Create` が通るか。依存 (テクスチャ・モデル・マテリアル・カーブ) を `EffectScene` と同じく数え、1 つでも読めなければ失敗
   - 節の木を実行側が読んだ値で出す: 種類・最大生成数・寿命・生成間隔・生成の遅れ・合成・色テクスチャ。`CalculateTerm` の期間
   - `Play` の後 `Update(1)` を k 回呼んで描いた絵を `frame_k.png` に書く。各フレームの生存・インスタンス数 (Root を含む)・描いた画素 (背景から色の成分の 1 つでも 16 以上離れた画素) を出す
   - Manager と Renderer の組み方は `EffectScene` のコンストラクタと同じ (左手系・最大 8000・描画 5 種・読み手 4 種)。描画先は `R8G8B8A8_UNORM`。カメラは原点を `--distance` (既定 6) の手前から見る、画角 60 度、背景は暗い灰 (`--bg`)
   - `--start-frame N` は `Manager::Play` の開始フレームに N を渡す (ゲームの `EffectScene::Play` は渡さない。比べる時だけ使う)
6. **並べる。** PNG を 8 列に並べた一覧 `frames/<名前>_sheet.png`
7. **置く (`--install` の時だけ)。** 全部通った後、`.efkefc` と `defs/` の下のフォルダを `Assets/Effects/` へ写す

## 定義の書き方

- 形は編集ソフトの `.efkproj` の XML そのもの。**既定から動かす値だけを書く**。公式サンプルを `efkxml dump` で開けば書き方の手本になる
- 末尾に `<ToolVersion>1.80.7</ToolVersion>`・`<Version>3</Version>`・`<StartFrame>`・`<EndFrame>`・`<IsLoop>` を書く。`Version` が無いと古い形として読み替えられる。後ろの 3 つは実行側に渡らないが、**無いと編集ソフトの読み込みが落ちる**
- 理由は注釈 (`<!-- -->`) で定義に書いてよい。編集ソフトは注釈で落ちるので、`efkbuild.py` が外してから渡す
- 数の意味 (EffekseerCore.dll の列挙から引いた物のうち使う物)
  - 描画 `DrawingValues/Type`: 0 無し・2 板 (既定)・3 リボン・4 輪・5 モデル・6 軌跡
  - 合成 `RendererCommonValues/AlphaBlend`: 0 不透明・1 半透明 (既定)・2 加算・3 減算・4 乗算
  - 板の向き `DrawingValues/Sprite/Billboard`: 0 カメラへ向く (既定)・1 縦軸固定・2 固定・3 回るカメラ向き・4 進む向きに縦を合わせる
  - 大きさ `ScalingValues/Type`: 0 固定・1 PVA・2 イージング・3 1 軸 PVA・4 1 軸イージング・5 カーブ・6 1 軸カーブ
  - 位置 `LocationValues/Type`: 0 固定・1 PVA・2 イージング・3 カーブ・4 NURBS・5 視点へずらす
  - 色 `DrawingValues/ColorAll/Type`: 0 固定・1 ランダム・2 イージング・3 カーブ・4 グラデーション
  - 生成位置 `GenerationLocationValues/Type`: 0 点・1 球・2 モデル・3 円・4 線
  - 場の力 `LocationAbsValues/LocalForceFieldN/Type`: 8 重力 など
  - イージングの形 `…/Type`: 1 線形・11 EaseOutQuadratic・21 EaseOutCubic ほか
- 既定の値 (確かめた物): 寿命 100、最大生成数 1、生成間隔 1、生成の開始 0、描画は板、合成は半透明、カーブの時間軸 `Timeline` は寿命に対する割合
- 生成の欄は `CommonValues/Generation/` の下: 間隔 `GenerationTime`、開始 `GenerationTimeOffset` (どちらも `Center`・`Max`・`Min`)
- グラデーションは `ColorMarkers/Key` (`ColorR`・`ColorG`・`ColorB` 0〜1、`Position` 0〜1、`Intensity`) と `AlphaMarkers/Key` (`Position`・`Alpha`)。カーブは `Keys/S/Key0..` (`Frame`・`Value`・取っ手 `LeftX`/`LeftY`/`RightX`/`RightY`・`InterpolationType` 0 ベジエ 1 線形)

## 絵の作りの決まり (efkprobe で数えた)

efkprobe の「フレーム 1」は Play → Update(1) → 描く。ゲームでは固定ステップの終わりに `Scene::OnUpdate` が `UpdateEffects` で 1 フレーム進めるので、ステップの中で `Play` した層も同じ手順で最初の絵になる見込み (推測。層を出す口を作る所で確かめる)。

- **1 フレーム目の絵は生まれた瞬間の値で描かれる。** 大きさの始点が 0 の `hit_flash_min` はフレーム 1 の描いた画素が 0 で、見えるのはフレーム 2 から。出るフレームに写したい層は、大きさ・不透明度の始点を 0 にしない
- **一度に出す粒は、生成間隔 0 に生成の開始 -1 を足す。** 間隔 0 だけでは (実行側は 1e-05 と読む) フレーム 1 に 1 粒、残りはフレーム 2。開始を 0 より前に置くと、実行側は 0 より前の分を最初の更新でまとめて生む (`InstanceGroup::GenerateInstancesIfRequired`)

| 火花 20 粒の出し方 | フレーム 1 のインスタンス数 (Root を含む) | フレーム 1 の描いた画素 | フレーム 2 |
|---|---|---|---|
| 間隔 0 だけ (初めの `spark_min`) | 2 (1 粒) | 359 | 21・1103 |
| 間隔 0 + 開始 -1 (今の `spark_min`) | 21 | 882 | 21・2880 |
| 1 粒の節を 20 個 | 21 | 882 | 21・2880 |
| `Play` の直後に `Manager::UpdateHandle(h, 1)` を 1 回 | 2 (1 粒) | 359 | 変わらない |
| `Play` の開始フレーム 1 (`--start-frame 1`) | 21 | 1103 | 21・2964 |

- `UpdateHandle` を 1 回呼んでも変わらないのは、実行側が `Play` では節を作らず、最初の更新の `Preupdate` で作るから (その更新は作るだけで進まない)。開始フレーム 1 は最初の絵が 1 フレーム進んだ姿になり、生まれた瞬間の絵は描かれない (閃光はフレーム 1 で 820 画素、寿命の終わりも 1 フレーム早まる)
- 生成の開始 -1 は閃光には効かない (`hit_flash_min` に足してもフレーム 1 は 0 画素)。数を揃えるのは開始、見える形は始点の値、と分けて持つ

## 落とし穴

- 定義に注釈があると編集ソフトの読み込みが落ちる (NullReferenceException)。`StartFrame`・`EndFrame`・`IsLoop` が無くても落ちる。**どちらも終了コード 0 のまま何も書かない**ので、書き出した物の時刻で成否を見ている
- 編集ソフトは書き出す時、素材の道を「書き出した `.efkefc` から見た相対」に書き換える。だから素材を `.efkefc` の隣に同じ並びで写してから書き出す
- 綴りを誤った要素は黙って捨てられ、既定の値に戻る (`AlphaBlend` を `AlphaBlnd` と書くと合成が半透明になった)。照合 4 (a) で捕まえる
- テクスチャが無いまま書き出すと、照合は通り、efkprobe が「読めなかった: テクスチャ …」で落とす。素材の欠けを捕まえるのは 5 の段と試し `ShippedEffects`
- 歪み (背景を渡していない) と GPU の粒の節 (`SetGpuParticleSystem` を呼んでいない) はゲームで何も言わずに出ない。使わない

## 確かめ用の 2 本 (道の確かめ用。手触りの値ではない)

| 名前 | 中身 | efkprobe で読んだ値 |
|---|---|---|
| `hit_flash_min` | 白い閃光の丸 1 枚。加算。大きさ 0 → 2 を EaseOutCubic で、色は白の不透明 → 透明、寿命 8 | 節 2 (Root + 板)。最大生成数 1、寿命 8、合成 Add、テクスチャ `Texture/flash.png`。フレーム 1 は 0 画素、2〜8 に写り、10 で Root だけ、11 で消える |
| `spark_min` | 火花 20 粒。球の上に全方向の回転で置き、各粒の +Y へ 0.1〜0.3 で飛ばす。親の座標の重力 -0.01。大きさは寿命の割合のカーブで 1 → 0、色はグラデーション。寿命 12〜24。板は進む向きに縦を合わせた縦長。生成間隔 0・開始 -1 | 節 2。最大生成数 20、寿命 12〜24、合成 Add、テクスチャ `Texture/spark.png`。フレーム 1 から 20 粒 |

## 壊して確かめた事

「場所」の列が scratchpad の物は、この道を scratchpad で作った時に確かめ、ツリーへ置いてからは走らせ直していない。

| 壊し方 | 落ちた所 | 場所 |
|---|---|---|
| `AlphaBlend` を `AlphaBlnd` と綴り誤り | 照合 4 (a)「定義の葉が読まれた後に無い」。書き出しは通り、開いた XML は一致する | ツリー |
| 素材の道を絶対の道にする | 編集ソフトが `../../..` の相対に書き換え、照合 4 (a)「定義の葉の値が変わった」 | ツリー |
| 節の名前を利用者のフォルダ名にする | 3「EDIT の塊 (utf-8) に利用者の名前か道」 | ツリー |
| `NS_EFFEKSEER_TOOL` を消す・`Effekseer.exe` の無いフォルダにする | 始める前に「環境変数 NS_EFFEKSEER_TOOL が無い」・「Effekseer.exe が無い」 | ツリー |
| `Assets/Effects/Texture/spark.png` を外す | 試し `ShippedEffects.EveryEffectPreloads`「spark_min.efkefc を読めないか、参照する素材が欠けている」 | ツリー |
| 素材の道を無いファイルへ | efkprobe「読めなかった: テクスチャ Texture/nothere.png」、終了コード 1 | scratchpad |
| 閃光の定義を火花の `.efkefc` と照合 | 照合 4 (b)「開いた XML が食い違う」 | scratchpad |
| 既定と同じ値 (`Timeline` 1) を書いた | 照合 4 (a) | scratchpad |
| 注釈つきの定義を直に渡す・`StartFrame` などを外す | 編集ソフトが書き出さず、2 で失敗 | scratchpad |

## 見ていない物

- 作った `.efkefc` を編集ソフトの画面で開いた姿 (中核の DLL で開いたのは確かめた)
- ゲームの中での見え方と Replay の撮影 (efkprobe の絵との突き合わせは S6 の T6-8)
- モデル・マテリアル (`.efkmat`)・カーブ (`.efkcurve`) を読む節
- efkprobe の WARP (GPU が無い時) の道
