#pragma once

/// @file LedgeEdges.h
/// @brief 歩ける天面の「踏み外せる縁」を world 線分列で算出する
///
/// @details grid 固形セルのうち真上が空いた天面について、 側方の隣が固形でなければ
/// その天面の縁辺を線分にする。 コヨーテタイムが効く境界の debug 可視化に使う
/// 連結判定は見た目でなく固形性で行うので、 隣接が別マテリアルでも固形なら縁は出さない

#include "Framework/Math/Math.h"

#include <vector>

namespace NS::GameCore::Level
{
    struct LevelData;
} // namespace NS::GameCore::Level

namespace NS::GameCore::Blocks
{
    /// 天面の縁辺 1 本。 a, b は world 端点で y は天面高さ
    /// outward は空セル側 すなわちコヨーテ猶予が伸びる向きの水平単位ベクトル
    struct LedgeEdge
    {
        NS::Math::Vector3 a{0.0f, 0.0f, 0.0f};
        NS::Math::Vector3 b{0.0f, 0.0f, 0.0f};
        NS::Math::Vector3 outward{0.0f, 0.0f, 0.0f};
    };

    /// level の grid 固形から、 歩ける天面の踏み外せる縁辺を world 線分列で返す
    /// 真上が固形のセルは天面が塞がれて立てないので対象外。 側方の隣が固形なら共有面に縁は出ない
    [[nodiscard]] std::vector<LedgeEdge> ComputeTopLedgeEdges(const NS::GameCore::Level::LevelData& level);
} // namespace NS::GameCore::Blocks
