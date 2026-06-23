#pragma once

/// @file SweptCapsule.h
/// @brief NS::Physics — Capsule vs Sphere / Capsule の TOI 計算 (丸い自由配置物用)
///
/// @details プレイヤー capsule の芯線分を、 相手 (球 / capsule) を半径 R = 両半径和 へ膨らませた
/// capsule とみなし、 相対変位 ray で貫く解析解で TOI を出す。 capsule vs capsule のみ
/// プレイヤー芯の両端点で近似する (既存 SweptOBB と同じ端点近似)。 floor 判定は呼出側で
/// contactNormal.y を確認する (SweptTriangle / SweptOBB と同方針)

#include "Framework/Math/Math.h"
#include "Framework/Physics/Capsule.h"
#include "Framework/Physics/Sphere.h"

namespace NS::Physics
{
    /// @brief Capsule が motion だけ移動した時の Sphere との最初の接触を返す (解析解・端点近似なし)
    /// @param capsule   入力 Capsule (start 位置)
    /// @param motion    1 frame の変位ベクトル
    /// @param sphere    ターゲット球
    /// @param outToi    [0,1] の接触時刻、 no hit なら 1.0
    /// @param outNormal 球表面外向きの接触法線 (world)、 no hit なら zero
    /// @retresult true = 接触あり / false = no hit
    [[nodiscard]] bool SweptCapsuleVsSphere(const Capsule& capsule,
                                            const NS::Math::Vector3& motion,
                                            const Sphere& sphere,
                                            float& outToi,
                                            NS::Math::Vector3& outNormal) noexcept;

    /// @brief Capsule が motion だけ移動した時の別 Capsule との最初の接触を返す
    /// @details プレイヤー芯の両端点を相手の膨張 capsule へ ray で当てる端点近似で、
    ///          芯の中央のみが当たる稀ケースは取りこぼす (既存 SweptOBB と同水準)
    /// @param outNormal 相手 capsule 表面外向きの接触法線 (world)、 no hit なら zero
    /// @retresult true = 接触あり / false = no hit
    [[nodiscard]] bool SweptCapsuleVsCapsule(const Capsule& capsule,
                                             const NS::Math::Vector3& motion,
                                             const Capsule& other,
                                             float& outToi,
                                             NS::Math::Vector3& outNormal) noexcept;
} // namespace NS::Physics
