# エフェクトの試験素材

`board_wide`、`life_random`、`mover_x`、`stack_by_input`、`stream_life10` は
`Tools/effects/test_defs` から Effekseer 1.80.7 で書き出した素材。
生成と照合は `Tools/@build_effects.cmd nobuild --defs Tools/effects/test_defs --out build/effects_test` で行う。

`impact_core_legacy.efkefc` と `Texture/impact_*.png` は、基点 `b7cc0b35` の
`Assets/Effects/impact.core.efkefc` と依存画像を凍結した比較対象。
現行の衝突光を再生成しても、こちらは更新しない。
`EffectAssetsRender.CoreMaterialKeepsTheHeldLightAndItsRelease` が旧来の毎フレームの拡縮と
現行素材を描画して比較し、素材へ移した曲線と停止後の大きさが保たれているかを確かめる。

`impact_streak/ring/glow/sparks/embers/recoil_legacy`、`release_burst_legacy`、
`launch_trail_legacy`も基点のバイナリを固定した比較対象。更新しない。

`impact.dust_reference`、`land.dust_reference`、`launch.landDust_reference`は、
元の素材に等倍の描画しない親だけを追加した比較対象。
親の追加でSDKの粒の乱数列が1つ進むため、無変更の旧素材との画素一致とは区別する。
旧係数を再生の根に渡す描画と、現行素材へゲームの情報だけを渡す描画を比較し、
発生する輪・塊の数・大きさ・寿命を確かめる。比較対象の木と係数を現行に合わせて更新しない。
