#pragma once

#include "Runtime/Core/AABB.h"
#include "Runtime/Core/Math.h"

namespace NS::Obj
{
    class Collider;
    class GameObject;
} // namespace NS::Obj

namespace NS::Game::Level
{
    //! @brief FindBodyCollider の結果
    struct BodyColliderSearch
    {
        const NS::Obj::Collider* body = nullptr; //!< 体の当たり判定。ぶつかる当たり判定がちょうど 1 つの時だけ入る
        int count = 0;                           //!< ぶつかる当たり判定の数
    };

    //! @brief 配置物の体の当たり判定を探す
    //! @details 体は「トリガー」でも「物理に入れない」でもない当たり判定。並び順では決めない
    //! 2 つ以上ある時はどれで測ったかが読めなくなるので、body を空のまま数だけ返す
    //! 寝ている当たり判定も数える。体当たりの探索は当たりを寝かせた配置物も相手にする
    //! @param[in] object 当たり判定を持つ配置物
    //! @return 体の当たり判定と、ぶつかる当たり判定の数
    //! 依存: NS::Obj::GameObject, NS::Obj::Collider
    [[nodiscard]] BodyColliderSearch FindBodyCollider(const NS::Obj::GameObject& object) noexcept;

    //! @brief 配置物の体の当たり判定から世界の外接箱を取り出す
    //! @details 体は FindBodyCollider で探し、箱か球の時だけ返す。取り出せなければ outBounds を触らない
    //! 当たりが寝ていても形は返す。体当たりの探索は当たりを寝かせた配置物も相手にするので、
    //! 返さないと飛んでいる最中の配置物を拾えなくなる
    //! @param[in] object 当たりを持つ配置物
    //! @param[out] outBounds 世界座標の外接箱
    //! @return 取り出せた場合 true、それ以外の場合は false
    //! 依存: NS::Obj::GameObject, NS::Obj::BoxCollider, NS::Obj::SphereCollider
    [[nodiscard]] bool TryGetColliderBounds(const NS::Obj::GameObject& object, NS::Core::AABB& outBounds) noexcept;
} // namespace NS::Game::Level
