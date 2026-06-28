#pragma once

/// @file SweptOBB.h
/// @brief 自由配置物の回転・スケールに対応する NS::Physics::OBB と Capsule vs OBB の TOI 計算
///
/// @details 平行移動・回転・スケールの単一 TRS 由来の OBB は軸が直交を保つため、 sphere の
/// 始点と motion を local 軸へ射影すると原点中心の軸並行 slab test へ帰着できる
/// floor 判定は呼出側で `contactNormal.y > 0.7` を確認する。 SweptTriangle と同方針

#include "Framework/Math/Math.h"
#include "Framework/Physics/Capsule.h"

namespace NS::Physics
{
    /// 有向境界ボックス。 axisX/Y/Z は回転の各列にあたる正規直交基底、 halfExtents は scale 込みの半サイズ
    /// center を中心に各 axis 方向へ halfExtents 伸びる
    struct OBB
    {
        NS::Math::Vector3 center{0.0f, 0.0f, 0.0f};
        NS::Math::Vector3 axisX{1.0f, 0.0f, 0.0f};
        NS::Math::Vector3 axisY{0.0f, 1.0f, 0.0f};
        NS::Math::Vector3 axisZ{0.0f, 0.0f, 1.0f};
        NS::Math::Vector3 halfExtents{0.5f, 0.5f, 0.5f};
    };

    /// center / 回転 quaternion / scale 込み halfExtents から OBB を組む
    /// axis は unit 軸を quaternion で回して得る。 halfExtents は負 scale 対策で各成分の絶対値を取る
    /// @param center      world 空間での OBB の中心
    /// @param rotation    正規直交軸の保証に必要な単位 quaternion 前提
    /// @param halfExtents scale 込みの半サイズ、 各成分の絶対値を取る
    [[nodiscard]] OBB MakeObb(const NS::Math::Vector3& center,
                              const NS::Math::Quaternion& rotation,
                              const NS::Math::Vector3& halfExtents) noexcept;

    /// @brief Capsule が motion だけ移動した時の OBB との最初の接触を返す
    /// @param capsule  start 位置の入力 Capsule
    /// @param motion   1 frame の変位ベクトル
    /// @param obb      ターゲット OBB
    /// @param outToi   [0,1] の接触時刻、 no hit なら 1.0
    /// @param outNormal world の OBB 表面外向きの接触法線、 no hit なら zero
    /// @retresult true = 接触あり / false = no hit
    [[nodiscard]] bool SweptCapsuleVsOBB(const Capsule& capsule,
                                         const NS::Math::Vector3& motion,
                                         const OBB& obb,
                                         float& outToi,
                                         NS::Math::Vector3& outNormal) noexcept;
} // namespace NS::Physics
