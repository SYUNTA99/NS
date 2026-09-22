#pragma once

#include "Runtime/Core/Math.h"
#include "Runtime/Physics/Triangle.h"

#include <array>
#include <cstdint>

namespace NS::Phys
{
    //! @brief 中心 center、半サイズ halfExtents、傾斜 angleDegrees の楔形スロープ wedge を 8 三角形で返す
    //! @details Gfx::MakeSlope の描画 mesh と同じ規約で衝突三角形を作る
    //! SlopeCollider がここから衝突三角形を組む
    //! 5 面 8 三角形の内訳は斜面 2 + 底面 2 + 裏壁 2 + 左右側面 1+1。全て CCW で cross が外向き法線
    //! @param[in] center 中心
    //! @param[in] halfExtents 半サイズ
    //! @param[in] angleDegrees 斜面の傾斜(度)。斜面の高さは 2 * halfExtents.y で頭打ちになる
    //! @param[in] yawRadians Y 軸まわりの向き(ラジアン、連続値)。0 は +Z 側が高い斜面
    [[nodiscard]] std::array<Triangle, 8> BuildWedgeTriangles(const NS::Core::Vector3& center,
                                                              const NS::Core::Vector3& halfExtents,
                                                              float angleDegrees,
                                                              float yawRadians) noexcept;
} // namespace NS::Phys
