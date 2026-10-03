#pragma once

// エディタの当たりの下見。編集中の場面の写しから別の場面を組み、選んだ条件で 1 回当てて前後を記録する
// ゲームと同じ部品と段を通し、返りを別に計算し直さない。出荷ビルドには載らない

#include "Game/Level/ImpactResolver.h"
#include "Runtime/Core/Math.h"
#include "Runtime/Object/Scene/SceneJson.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace NS::Obj
{
    class AssetManager;
    class Scene;
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
        std::uint32_t targetId = 0;  //!< 当てる相手の配置物の id
        float faceU = 0.0f;          //!< 当てる面の上の左右の位置。自機から見て右が正。-1..1 が面の端
        float faceV = 0.0f;          //!< 当てる面の上の上下の位置。上が正。-1..1 が面の端
        float charge01 = 1.0f;       //!< 溜め量 0..1。0 はタップの飛び込み
        int leadFrames = 10;         //!< 突進を出してから検知するまでのフレーム数の狙い。触れる前の事象を見る間合い
        int framesAfterRebound = 20; //!< 反動が終わってから記録を続けるフレーム数
        int maxFrames = 300;         //!< 記録するフレーム数の上限
    };

    //! @brief 下見の 1 フレームの姿。場面を 1 歩進めた後に読む
    struct HitPreviewFrame
    {
        NS::Core::Vector3 playerPosition{};        //!< 自機の根の位置
        NS::Core::Vector3 shape{1.0f, 1.0f, 1.0f}; //!< 自機の形の倍率
        bool hitStopping = false;                  //!< 止めの事象の最中
        bool awaitingRebound = false;              //!< 止めが明けて反動を待っている
        bool rebounding = false;                   //!< 自機が反動で弾かれている
        std::vector<std::size_t> startedRows;      //!< このフレームに始まった事象の、段のタイムラインの行の番号
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
        NS::Core::Vector3 playerStart{};        //!< 写しの中で自機の根を置き直した位置
        NS::Core::Vector3 direction{};          //!< 突進の水平の向き
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
    //! 見て最大 6 回まで詰め、合った回を返す。合わなければ選んだ位置に一番近く当てた回を返す。届かない位置
    //! (床に乗った玉より下など) は詰め切れず、impact の faceU・faceV に実際に当たった位置が残る。snapshot は書き換えない
    //! @param[in] snapshot 編集中の場面の写し (Scene::ToJson)
    //! @param[in] desc 当たりの条件
    //! @param[in] world 写しの場面に渡す資産と描き手
    //! @return 下見の結果。自機か相手が居ない、相手に体のセンサーか面が無い、選んだ相手に当たらなかった場合は hit
    //! が偽で error に理由が入る
    [[nodiscard]] HitPreviewResult RunHitPreview(const nlohmann::json& snapshot,
                                                 const HitPreviewDesc& desc,
                                                 const HitPreviewWorld& world = {});

    //! @brief 下見の選んだフレームの場面を、写しから組み直して進める
    //! @details 描くために 1 枚ぶんの場面を作る。RunHitPreview と同じ置き直しと頼みから frameIndex + 1 歩進める
    //! @param[in] snapshot RunHitPreview に渡した写し
    //! @param[in] result RunHitPreview の結果
    //! @param[in] frameIndex result.frames の添字。範囲の外は端へ寄せる
    //! @param[in] world 写しの場面に渡す資産と描き手
    //! @return 進めた場面。自機が居ない場合は nullptr
    [[nodiscard]] std::unique_ptr<NS::Obj::Scene> BuildHitPreviewSceneAt(const nlohmann::json& snapshot,
                                                                         const HitPreviewResult& result,
                                                                         int frameIndex,
                                                                         const HitPreviewWorld& world = {});
} // namespace NS::Editor
