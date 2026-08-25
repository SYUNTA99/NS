#pragma once

#include "Runtime/Core/Math.h"

namespace NS::Object
{
    class GameObject;
}

namespace NS::Game::Level
{
    //! @brief 配置物の当たりから世界の外接箱を取り出す
    //! @details 見るのは箱と球の 2 形状。 どちらも無ければ outBounds を触らない
    //! 当たりが寝ていても形は返す。 体当たりの探索は当たりを寝かせた配置物も相手にするので、
    //! 返さないと飛んでいる最中の配置物を拾えなくなる
    //! @param[in] object 当たりを持つ配置物
    //! @param[out] outBounds 世界座標の外接箱
    //! @return 取り出せた場合 true、それ以外の場合は false
    //! 依存: NS::Object::GameObject, NS::Object::BoxColliderComponent, NS::Object::SphereColliderComponent
    [[nodiscard]] bool TryGetColliderBounds(const NS::Object::GameObject& object, NS::Core::AABB& outBounds) noexcept;
} // namespace NS::Game::Level
