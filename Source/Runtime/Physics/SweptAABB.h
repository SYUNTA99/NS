#pragma once

#include "Runtime/Math/Math.h"
#include "Runtime/Physics/Capsule.h"

namespace NS::Physics
{
    /// @brief Capsule が motion ベクトルだけ移動した場合の AABB との最初の接触を返す
    /// @param capsule  移動開始位置の Capsule
    /// @param motion   1 フレーム分の変位ベクトル
    /// @param box      ターゲット AABB
    /// @param outToi   [0,1] の接触時刻。当たらなければ 1.0
    /// @param outNormal capsule 表面外向きの接触法線。当たらなければ零ベクトル
    /// @retresult true = 接触あり / false = 接触なし
    [[nodiscard]] bool SweptCapsuleVsAABB(const NS::Physics::Capsule& capsule,
                                          const NS::Math::Vector3& motion,
                                          const NS::Math::AABB& box,
                                          float& outToi,
                                          NS::Math::Vector3& outNormal) noexcept;
} // namespace NS::Physics
