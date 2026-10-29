#pragma once

// エディタの当たりの下見。編集中の場面の写しから別の場面を組み、選んだ条件で 1 回当てて前後を記録する
// ゲームと同じ部品と段を通し、返りを別に計算し直さない。出荷ビルドには載らない

#include "Game/Level/ImpactResolver.h"
#include "NSlib/Core/Math.h"
#include "NSlib/CoreObject/Scene/SceneJson.h"
#include "NSlib/Core/Windows/Gamepad.h"

#include "NSlib/Core/Types.h"
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace NS
{
    class Scene;
} // namespace NS

namespace NS
{
    namespace Gfx
    {
        class D3DDevice;
    }
} // namespace NS

namespace NS
{
    namespace Editor
    {
        //! 下見の当たりの条件
        struct HitPreviewDesc
        {
            //! 当てる相手のアクターの id。0 は自機に一番近い、突進が当たる体と面を持つアクター (結果の desc に選んだ id
            //! が入る)
            uint32 targetId = 0;
            float faceU = 0.0f;          // 当てる面の上の左右の位置。自機から見て右が正。-1..1 が面の端
            float faceV = 0.0f;          // 当てる面の上の上下の位置。上が正。-1..1 が面の端
            float charge01 = 1.0f;       // 溜め量 0..1。0 は通常突進
            int leadFrames = 10;         // 突進を出してから検知するまでのフレーム数の狙い。触れる前の事象を見る間合い
            int framesAfterRebound = 20; // 反動が終わってから記録を続けるフレーム数。当たりの束が終わるまでは続ける
            int maxFrames = 300;         // 記録するフレーム数の上限
        };

        //! 下見の 1 フレームの姿。場面を 1 歩進めた後に読む
        struct HitPreviewFrame
        {
            Vector3 playerPosition{};         // 自機の根の位置
            Vector3 shape{1.0f, 1.0f, 1.0f};  // 自機の描く倍率 (Model::GetDrawScale)
            bool hitStopping = false;             // 自機の止めの残りがある
            bool rebounding = false;              // 自機が反動で弾かれている
            OS::GamepadVibration pad{};       // 下見が控えたパッドの振動。手元のパッドへは送らない
            float trauma = 0.0f;                  // カメラのトラウマ 0〜1
            Vector3 shakeAngles{};            // トラウマの揺れの角度 (度)。横・縦・傾き
            float sinkPixels = 0.0f;              // 沈む揺れの縦のずれ (高さ 1080 の画面の画素、下が負)
            float worldSpeed = 1.0f;              // 世界の速さ 0〜1
            std::string action;                     // 自機の走っている動作の名前。走っていなければ空
            int actionFrame = -1;                   // 走っている動作の最後に進めたフレーム。状態に入った歩が 0
            std::vector<std::size_t> startedEvents; // このフレームに始まった、動作の events の並びでの番号
        };

        //! 下見の結果
        struct HitPreviewResult
        {
            bool hit = false;                    // 選んだ相手に当たった場合 true
            std::string error;                   // 当たらなかった理由。当たった時は空
            HitPreviewDesc desc{};               // 走らせた条件
            GL::Level::ImpactRecord impact{};    // 当たりの記録。当たらなかった時は既定のまま
            int detectionIndex = -1;             // 当たりを検知したフレームの frames の添字。当たらなかった時は -1
            std::vector<HitPreviewFrame> frames; // 突進を出した次の 1 歩から並ぶ
            Vector3 playerStart{};           // 写しの中で自機の根を置き直した位置
            Vector3 direction{};             // 突進の水平の向き
        };

        //! 写しの場面を組む時に渡す物。描かない下見では空でよい
        struct HitPreviewWorld
        {
            Gfx::D3DDevice* device = nullptr; // 描く装置。空ならエフェクトの世界を描かない
        };

        //! snapshot を変えずに別シーンで当たりを再現する。実行中は入力を中立にし、実機へ振動を送らない
        //! 面の位置と検知時刻を最大 6 回調整する。合わなければ最も近い結果を返す
        //! hit が false なら error に理由が入り、成功時の impact には実際の命中位置が残る
        [[nodiscard]] HitPreviewResult RunHitPreview(const nlohmann::json& snapshot,
                                                     const HitPreviewDesc& desc,
                                                     const HitPreviewWorld& world = {});

        //! 下見結果と同じ開始状態のシーンを作る。記録の先頭はこの状態を 1 歩進めた姿
        //! 当たらなかった結果では nullptr
        [[nodiscard]] std::unique_ptr<Scene> MakeHitPreviewScene(const nlohmann::json& snapshot,
                                                                     const HitPreviewResult& result,
                                                                     const HitPreviewWorld& world = {});

        //! hits.jsonl と同じ形式の 1 行を返す。改行は含めず、未命中では空文字列
        [[nodiscard]] std::string HitPreviewHitLine(const HitPreviewResult& result);
    } // namespace Editor
} // namespace NS
