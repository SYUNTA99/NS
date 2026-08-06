#pragma once

#include "Runtime/Math/Math.h"
#include "Runtime/Physics/Capsule.h"

namespace NS::Physics
{
    /// @brief Capsule が motion だけ移動した時の Sphere との最初の接触を解析解で端点近似なしに返す
    /// @param capsule   移動開始位置の Capsule
    /// @param motion    1 フレーム分の変位ベクトル
    /// @param sphere    ターゲット球
    /// @param outToi    [0,1] の接触時刻。当たらなければ 1.0
    /// @param outNormal world の球表面外向きの接触法線。当たらなければ零ベクトル
    /// @retresult true = 接触あり / false = 接触なし
    [[nodiscard]] bool SweptCapsuleVsSphere(const NS::Physics::Capsule& capsule,
                                            const NS::Math::Vector3& motion,
                                            const NS::Math::Sphere& sphere,
                                            float& outToi,
                                            NS::Math::Vector3& outNormal) noexcept;

    /// @brief Capsule が motion だけ移動した時の別 Capsule との最初の接触を返す
    /// @details プレイヤー capsule の軸端点を相手の膨張 capsule へ ray で当てる端点近似
    ///          軸の中央だけが当たる稀なケースは取りこぼす。SweptOBB と同水準
    /// @param outNormal world の相手 capsule 表面外向きの接触法線。当たらなければ零ベクトル
    /// @retresult true = 接触あり / false = 接触なし
    [[nodiscard]] bool SweptCapsuleVsCapsule(const NS::Physics::Capsule& capsule,
                                             const NS::Math::Vector3& motion,
                                             const NS::Physics::Capsule& other,
                                             float& outToi,
                                             NS::Math::Vector3& outNormal) noexcept;
} // namespace NS::Physics
