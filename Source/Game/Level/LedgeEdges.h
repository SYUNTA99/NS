#pragma once

#include "Runtime/Core/Math.h"

#include <vector>

namespace NS::Game::Level
{
    //! @brief 歩行可能な天面の縁（踏み外し判定用エッジ）を表すデータ構造
    struct LedgeEdge
    {
        NS::Core::Vector3 a{0.0f, 0.0f, 0.0f}; ///< @brief エッジのワールド座標端点1（yは天面高さ）
        NS::Core::Vector3 b{0.0f, 0.0f, 0.0f}; ///< @brief エッジのワールド座標端点2（yは天面高さ）
        NS::Core::Vector3 outward{
            0.0f, 0.0f, 0.0f}; ///< @brief 空中側へ向かう水平単位ベクトル（コヨーテ猶予の伸びる方向）
    };

    //! @brief 固形ブロック群のOBBから、踏み外し可能な天面のエッジ一覧を抽出する
    //! @note 上部が別のブロックで覆われている面や、隣接ブロックと高さが繋がっている辺は抽出対象から除外される
    //! @param solidBoxes 判定対象となる固形ブロックのOBB
    //! @return 踏み外し可能なエッジの配列
    [[nodiscard]] std::vector<LedgeEdge> ComputeTopLedgeEdges(const std::vector<NS::Core::OBB>& solidBoxes);

} // namespace NS::Game::Level