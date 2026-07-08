#pragma once

/// @file LedgeEdges.h
/// @brief 歩ける天面の「踏み外せる縁」を world 線分列で算出する
///
/// @details 固形箱の world OBB 群を入力に、 各箱の axisY 面を天面とみなした縁辺のうち
/// 真上を塞がれず側方へ同じ高さの固形が接していない辺を線分にする。 コヨーテタイムが効く境界の debug 可視化に使う
/// OBB を入力に取ることで配置物の位置・スケール・回転にそのまま追従し、 セルや軸並行を前提しない

#include <vector>

namespace NS::GameCore::Blocks
{
    /// 天面の縁辺 1 本。 a, b は world 端点で y は天面高さ
    /// outward は空側 すなわちコヨーテ猶予が伸びる向きの水平単位ベクトル
    struct LedgeEdge
    {
        NS::Math::Vector3 a{0.0f, 0.0f, 0.0f};
        NS::Math::Vector3 b{0.0f, 0.0f, 0.0f};
        NS::Math::Vector3 outward{0.0f, 0.0f, 0.0f};
    };

    /// 固形箱の world OBB 群から、 歩ける天面の踏み外せる縁辺を world 線分列で返す
    /// 各箱の axisY 面を天面とみなす。 真上を別の箱が覆う天面は対象外で、 側方に同じ高さの固形が接する辺は出ない
    [[nodiscard]] std::vector<LedgeEdge> ComputeTopLedgeEdges(const std::vector<NS::Physics::OBB>& solidBoxes);
} // namespace NS::GameCore::Blocks
