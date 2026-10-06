#pragma once

// エディタの当たりの下見。編集中の場面の写しから別の場面を組み、選んだ条件で 1 回当てて前後を記録する
// ゲームと同じ部品と段を通し、返りを別に計算し直さない。出荷ビルドには載らない

#include "Game/Level/ImpactResolver.h"
#include "NSlib/Core/Math.h"
#include "NSlib/Object/Scene/SceneJson.h"
#include "NSlib/Windows/Gamepad.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace NS::Obj
{
    class AssetManager;
} // namespace NS::Obj

namespace NS::Gfx
{
    class Renderer;
} // namespace NS::Gfx

namespace NS::Editor
{
    //! @brief 下見の当たりの条件
    struct HitPreviewDesc
    {
        //! 当てる相手の配置物の id。0 は自機に一番近い、突進が当たる体と面を持つ配置物 (結果の desc に選んだ id が入る)
        std::uint32_t targetId = 0;
        float faceU = 0.0f;          //!< 当てる面の上の左右の位置。自機から見て右が正。-1..1 が面の端
        float faceV = 0.0f;          //!< 当てる面の上の上下の位置。上が正。-1..1 が面の端
        float charge01 = 1.0f;       //!< 溜め量 0..1。0 は通常突進
        int leadFrames = 10;         //!< 突進を出してから検知するまでのフレーム数の狙い。触れる前の事象を見る間合い
        int framesAfterRebound = 20; //!< 反動が終わってから記録を続けるフレーム数
        int maxFrames = 300;         //!< 記録するフレーム数の上限
    };

    //! @brief 下見の 1 フレームの姿。場面を 1 歩進めた後に読む
    struct HitPreviewFrame
    {
        NS::Vector3 playerPosition{};         //!< 自機の根の位置
        NS::Vector3 shape{1.0f, 1.0f, 1.0f};  //!< 自機の形の倍率
        bool hitStopping = false;             //!< 止めの事象の最中
        bool awaitingRebound = false;         //!< 止めが明けて反動を待っている
        bool rebounding = false;              //!< 自機が反動で弾かれている
        NS::OS::GamepadVibration pad{};       //!< 下見が控えたパッドの振動。手元のパッドへは送らない
        float trauma = 0.0f;                  //!< カメラのトラウマ 0〜1
        NS::Vector3 shakeAngles{};            //!< トラウマの揺れの角度 (度)。横・縦・傾き
        NS::Vector2 shakeOffset{};            //!< 平行移動の揺れのずれ (m)。カメラの右と上
        float sinkPixels = 0.0f;              //!< 沈む揺れの縦のずれ (高さ 1080 の画面の画素、下が負)
        float worldSpeed = 1.0f;              //!< 世界の速さ 0〜1
        std::vector<std::size_t> startedRows; //!< このフレームに始まった事象の、段のタイムラインの行の番号
    };

    //! @brief 下見の結果
    struct HitPreviewResult
    {
        bool hit = false;                       //!< 選んだ相手に当たった場合 true
        std::string error;                      //!< 当たらなかった理由。当たった時は空
        HitPreviewDesc desc{};                  //!< 走らせた条件
        NS::Game::Level::ImpactRecord impact{}; //!< 当たりの記録。当たらなかった時は既定のまま
        int detectionIndex = -1;                //!< 当たりを検知したフレームの frames の添字。当たらなかった時は -1
        std::vector<HitPreviewFrame> frames;    //!< 突進を出した次の 1 歩から並ぶ
        NS::Vector3 playerStart{};              //!< 写しの中で自機の根を置き直した位置
        NS::Vector3 direction{};                //!< 突進の水平の向き
    };

    //! @brief 写しの場面を組む時に渡す物。描かない下見では空でよい
    struct HitPreviewWorld
    {
        NS::Obj::AssetManager* assets = nullptr; //!< 資産の置き場。空なら参照を実体化しない
        NS::Gfx::Renderer* renderer = nullptr;   //!< 描き手。空ならエフェクトの世界を描かない
    };

    //! @brief 編集中の場面の写しで、選んだ条件の当たりを 1 回下見する
    //! @details 自機を、突進の線が相手の面の (faceU, faceV) を通り、leadFrames の後に検知する所へ置き直し、入力を止めて
    //! 突進を直接頼む。向きは編集中の自機の玉から相手の体の中心への水平。置き直しは実際に当てた面の位置と検知のフレームを
    //! 見て最大 6 回まで詰め、合った回を返す。組む・進める・壊す間は入力を中立にする
    //! (NS::OS::ScopedNeutralInput)。合わなければ選んだ位置に一番近く当てた回を返す。届かない位置
    //! (床に乗った玉より下など) は詰め切れず、impact の faceU・faceV に実際に当たった位置が残る。snapshot
    //! は書き換えない
    //! @param[in] snapshot 編集中の場面の写し (Scene::ToJson)
    //! @param[in] desc 当たりの条件
    //! @param[in] world 写しの場面に渡す資産と描き手
    //! @return 下見の結果。自機か相手が居ない、相手に体のセンサーか面が無い、選んだ相手に当たらなかった場合は hit
    //! が偽で error に理由が入る
    [[nodiscard]] HitPreviewResult RunHitPreview(const nlohmann::json& snapshot,
                                                 const HitPreviewDesc& desc,
                                                 const HitPreviewWorld& world = {});

    //! @brief 下見の当たりを、Replay の hits.jsonl と同じ鍵と並びの 1 行の JSON にする
    //! @details f は検知のフレームの frames の添字。Replay と記録のコードは共有しない (Replay
    //! は公開リポジトリの外にある)
    //! @param[in] result RunHitPreview の結果
    //! @return 改行を含まない 1 行。当たらなかった結果は空の文字列
    [[nodiscard]] std::string HitPreviewHitLine(const HitPreviewResult& result);
} // namespace NS::Editor
