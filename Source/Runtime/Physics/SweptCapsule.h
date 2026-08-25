#pragma once

#include "Runtime/Core/Math.h"
#include "Runtime/Physics/Capsule.h"

namespace NS::Physics
{
    //! @brief Capsule が motion だけ移動した時の Sphere との最初の接触を解析解で端点近似なしに返す
    //! @param[in] capsule   移動開始位置の Capsule
    //! @param[in] motion    1 フレーム分の変位ベクトル
    //! @param[in] sphere    ターゲット球
    //! @param[out] outToi    [0,1] の接触時刻。当たらなければ 1.0
    //! @param[out] outNormal world の球表面外向きの接触法線。当たらなければ零ベクトル
    //! @retresult true = 接触あり / false = 接触なし
    [[nodiscard]] bool SweptCapsuleVsSphere(const NS::Physics::Capsule& capsule,
                                            const NS::Core::Vector3& motion,
                                            const NS::Core::Sphere& sphere,
                                            float& outToi,
                                            NS::Core::Vector3& outNormal) noexcept;

    //! @brief Capsule が motion だけ移動した時の別 Capsule との最初の接触を返す
    //! @details 2 本の軸が平行なら厳密解。相手を半径と長さを足した capsule 1 本へ膨張させ、
    //!          自分の中心から ray を当てる。 平行でなければ自分の軸端点 2 つから ray を当てる近似で、
    //!          相手が短く自分の胴体の中央だけに当たる場合を取りこぼす
    //! @param[in] capsule   移動開始位置の Capsule
    //! @param[in] motion    1 フレーム分の変位ベクトル
    //! @param[in] other     ターゲット Capsule
    //! @param[out] outToi    [0,1] の接触時刻。当たらなければ 1.0
    //! @param[out] outNormal world の相手 capsule 表面外向きの接触法線。当たらなければ零ベクトル
    //! @retresult true = 接触あり / false = 接触なし
    [[nodiscard]] bool SweptCapsuleVsCapsule(const NS::Physics::Capsule& capsule,
                                             const NS::Core::Vector3& motion,
                                             const NS::Physics::Capsule& other,
                                             float& outToi,
                                             NS::Core::Vector3& outNormal) noexcept;
} // namespace NS::Physics
