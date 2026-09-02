#pragma once

#include "Runtime/Core/Math.h"
#include "Runtime/Physics/Capsule.h"

namespace NS::Physics
{
    //! @brief Capsule が motion ベクトルだけ移動した場合の AABB との最初の接触を返す
    //! @param[in] capsule  移動開始位置の Capsule
    //! @param[in] motion   1 フレーム分の変位ベクトル
    //! @param[in] box      ターゲット AABB
    //! @param[out] outToi   [0,1] の接触時刻。当たらなければ 1.0
    //! @param[out] outNormal capsule 表面外向きの接触法線。当たらなければ零ベクトル
    //! @retresult true = 接触あり / false = 接触なし
    [[nodiscard]] bool SweptCapsuleVsAABB(const NS::Physics::Capsule& capsule,
                                          const NS::Core::Vector3& motion,
                                          const NS::Core::AABB& box,
                                          float& outToi,
                                          NS::Core::Vector3& outNormal) noexcept;
} // namespace NS::Physics
