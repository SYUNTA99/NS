#pragma once

#include "NSlib/Core/Math.h"

namespace NS::Obj
{
    class ActorBase;

    //! @brief actor の居るシーンの重力の向きを返す
    //! @return シーンに居なければ (0, -1, 0)
    [[nodiscard]] NS::Vector3 GravityDirection(const ActorBase& actor) noexcept;
    //! @brief actor の居るシーンの重力の向きへ、strength × dt だけ velocity を足す
    //! @details strength か dt が有限でない時と、dt が 0 以下の時は何もしない
    //! @param[in] actor 重力の向きを引く物
    //! @param[in,out] velocity 足す先の速度
    //! @param[in] strength 重力の強さ。単位は m/s²
    //! @param[in] dt 進める秒
    void AddGravity(const ActorBase& actor, NS::Vector3& velocity, float strength, float dt) noexcept;
} // namespace NS::Obj
