# 音の素材

効果音 6 本と曲 1 本。どれも 2026-10-26 に ComfyUI で作った。音の名前は、このフォルダからの相対の道から `.wav` を除いた物 (`SoundName`)。

| 名前 | 何の音 | 元の候補 | 形 | 長さ |
|---|---|---|---|---|
| `hit` | 当たり | `sfx_hit_00001` | PCM 16 bit・44.1 kHz・ステレオ | 0.32 秒 |
| `jump` | 跳び | `sfx_jump_00001` | 同上 | 0.21 秒 |
| `land` | 着地 | `sfx_land_00003` | 同上 | 0.35 秒 |
| `goal` | ゴール | `sfx_goal_00003` | 同上 | 4.06 秒 |
| `fall` | 落下 | `sfx_fall_00001` | 同上 | 1.14 秒 |
| `rebound` | 反動 | `sfx_rebound_00001` | 同上 | 0.58 秒 |
| `bgm_course` | コースの曲 | `bgm_course_s20261027_m6db_00001` | MS-ADPCM・48 kHz・ステレオ | 86.0 秒 |

## 選び方と直し方

- 効果音は 1 本につき 4 本の候補から選んだ。音割れ (振幅 0.999 以上の標本) はどの候補にも無い。立ち上がりの短い物を選び、立ち上がりが揃う物は最大の音量の大きい物を選んだ。耳では聞いていない
- 効果音は、前と後ろの -60 dB 以下の無音を ffmpeg の `silenceremove` で切った。反動の候補は 4 本とも先頭に 0.53 秒の無音があったので、それを切った
- 曲は 3 本の候補から、後ろの無音が一番遅く始まる物 (87.39 秒) を選んだ。120 BPM・4 拍子の小節の切れ目の 86.0 秒で切った。ループのつなぎ目が滑らかかは聞いて確かめていない
- 曲の MS-ADPCM は、fmt の `cbSize` が 32・係数が標準の 7 つ・`wSamplesPerBlock` (1012) が `nBlockAlign` (1024) から出る値と同じ・データの長さが `nBlockAlign` の倍数である事を、ファイルを読んで確かめた

## 作ったモデルとプロンプト

### 効果音: Stable Audio 3 Small SFX

- モデル: `Comfy-Org/stable-audio-3` の `stable_audio_3_small_sfx.safetensors`。文の読み取りは `t5gemma_b_b_ul2.safetensors`
- KSampler: steps 8・cfg 1.0・lcm・simple・denoise 1.0。シード 20261026 で 1 回に 4 本 (候補の番号 `_00001`〜`_00004`)
- 否定の文: `music, melody, vocals, speech, noise, hiss, reverb tail`

| 名前 | 秒 | プロンプト |
|---|---|---|
| `hit` | 2.0 | `punchy cartoon impact hit, two rubber balls colliding hard, snappy bouncy thump with a bright pop, short one-shot game sound effect, dry, close` |
| `jump` | 1.5 | `short cartoon jump sound effect, quick springy whoosh going upward, light and bouncy, one-shot game sound effect, dry, close` |
| `land` | 1.5 | `short soft landing thud on the ground, light body impact with a little dust, one-shot game sound effect, dry, close` |
| `goal` | 4.0 | `short bright victory jingle for reaching the goal, cheerful chimes and sparkling fanfare, one-shot game sound effect` |
| `fall` | 3.0 | `cartoon falling sound effect, descending whistle dropping away into the distance, one-shot game sound effect` |
| `rebound` | 2.0 | `springy rebound boing, bouncing off a wall with an elastic snap and a quick whoosh, one-shot game sound effect, dry, close` |

### 曲: ACE-Step 1.5 turbo

- モデル: `Comfy-Org/ace_step_1.5_ComfyUI_files` の `ace_step_1.5_turbo_aio.safetensors`
- tags: `instrumental, upbeat playful video game background music, bouncy synth bass, plucky marimba, light electronic drums, cheerful and energetic, steady groove, seamless loop, no vocals`
- lyrics は `[instrumental]`。bpm 120・拍子 4・C major・90 秒・cfg_scale 2.0・temperature 0.85・top_p 0.9
- KSampler: steps 8・cfg 1.0・euler・simple。シード 20261027
- 音を下げずに作ると音割れしたので、保存の前に −6 dB 下げた

ワークフローの全部と測った表は、NS-design の `ui-assets/sounds-2026-10-26/` にある (`report.md`・`work/`)。

## ライセンス

### Stable Audio 3 Small SFX — Stability AI Community License Agreement

出どころ: https://stability.ai/community-license-agreement (Last Updated: July 5, 2024)

- 年の売り上げが 100 万ドル未満なら無料で使える: "this Agreement preserves free access to the Models for people or organizations generating annual revenue of less than US $1,000,000" (I)
- **商用で使う時は Stability AI への登録が要る。** 年の売り上げが 100 万ドルを超えると許可が切れる: "If You are using or distributing the Stability AI Materials for a Commercial Purpose, You must register with Stability AI. If at any time You or Your Affiliate(s) ... generate more than USD $1,000,000 in annual revenue ... any licenses granted to You under this Agreement shall terminate as of such date." (III)
  - 商用でない例として "personal use (i.e., hobbyist) or evaluation and testing" が挙がっている。Steam で売る時は登録が要る
- 作った音は作った人の物で、モデルの派生物に入らない: "You own any outputs generated from the Models or Derivative Works to the extent permitted by applicable law." (IV.c.(iii))・"but do not include the output of any Model."
- 配る時の表示: 素材かそれを使う製品を配る時は、本文の写しを付け、"This Stability AI Model is licensed under the Stability AI Community License, Copyright © Stability AI Ltd. All Rights Reserved" を残し、"Powered by Stability AI" を目立つ所に出す (IV.a)。作った音だけを配る時にこれが要るかは、本文からははっきりしない。売る前に確かめる
- 作った音で、生成 AI の基盤のモデルを作る・良くする事はしない (IV.b)
- 文の読み取りの T5Gemma は Gemma Terms of Use (https://ai.google.dev/gemma/terms): "Google claims no rights in Outputs you generate using Gemma." (3.3)

### ACE-Step 1.5 — MIT

出どころ: https://raw.githubusercontent.com/ace-step/ACE-Step-1.5/main/LICENSE

- 条件は、モデルの写しを配る時に著作権の表示 "Copyright (c) 2026 ACEStep" と許可の文を付ける事だけ。作った曲の扱いは書かれていない
