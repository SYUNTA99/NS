#pragma once

#include "Runtime/Core/Math.h"
#include "Runtime/Physics/Capsule.h"

namespace NS::Physics
{
    //! center / 回転 quaternion / scale 込み halfExtents から OBB を組む
    //! axis は単位軸を quaternion で回して得る。halfExtents は負 scale 対策で各成分の絶対値を取る
    //! @param[in] center      world 空間での OBB の中心
    //! @param[in] rotation    正規直交軸の保証に必要な単位 quaternion 前提
    //! @param[in] halfExtents scale 込みの半サイズ。各成分の絶対値を取る
    [[nodiscard]] NS::Core::OBB MakeObb(const NS::Core::Vector3& center,
                                        const NS::Core::Quaternion& rotation,
                                        const NS::Core::Vector3& halfExtents) noexcept;

    //! @brief Capsule が motion だけ移動した時の OBB との最初の接触を返す
    //! @details 単一 TRS 由来の OBB は軸が直交を保つので、sphere の始点と motion を local 軸へ射影すれば
    //! 原点中心の軸並行 slab test に持ち込める。床判定は呼出側で contactNormal.y > 0.7 を確認する
    //! @param[in] capsule  移動開始位置の Capsule
    //! @param[in] motion   1 フレーム分の変位ベクトル
    //! @param[in] obb      ターゲット OBB
    //! @param[out] outToi   [0,1] の接触時刻。当たらなければ 1.0
    //! @param[out] outNormal world の OBB 表面外向きの接触法線。当たらなければ零ベクトル
    //! @retresult true = 接触あり / false = 接触なし
    [[nodiscard]] bool SweptCapsuleVsOBB(const NS::Physics::Capsule& capsule,
                                         const NS::Core::Vector3& motion,
                                         const NS::Core::OBB& obb,
                                         float& outToi,
                                         NS::Core::Vector3& outNormal) noexcept;
} // namespace NS::Physics
