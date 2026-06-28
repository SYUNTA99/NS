#pragma once

/// @file WedgeGeometry.h
/// @brief 楔形スロープ wedge の衝突三角形生成
///
/// @details Graphics::MakeWedge の描画 mesh と同じ規約の collision 版。 SlopeColliderComponent と
/// PlayMode の両経路がこの関数を共有し、 wedge geometry の定義を 1 箇所に集約する
/// 5 面 8 三角形は斜面 2 + 底面 2 + 裏壁 2 + 左右側面 1+1 で、 全て CCW で cross が外向き法線

#include <array>
#include <cstdint>

#include "Framework/Math/Math.h"
#include "Framework/Physics/SweptTriangle.h"

namespace NS::Physics
{
    /// 中心 center、 半サイズ halfExtents、 傾斜 angleDegrees の wedge を 8 三角形で返す
    /// @param yawRadians ラジアンで連続的な Y 軸まわりの向き。 0 は +Z 側が高い斜面
    [[nodiscard]] std::array<Triangle, 8> BuildWedgeTriangles(const NS::Math::Vector3& center,
                                                              const NS::Math::Vector3& halfExtents,
                                                              float angleDegrees,
                                                              float yawRadians) noexcept;
} // namespace NS::Physics
