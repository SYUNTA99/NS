#pragma once

#include "NSlib/Core/AABB.h"
#include "NSlib/Core/Math.h"

namespace NS::Obj
{
    class Actor;
}

namespace NS::Game::Level
{
    //! @brief 配置物の当たりの形から世界の外接箱を取り出す
    //! @details 見るのは箱と球の 2 形状。どちらも無ければ outBounds を触らない
    //! 今の使い手は置物の吹き飛びの帯で、自分の直径を測るのに使う。
    //! WorldAABB は物理への登録に依らず根と欄から作るので、当たりを物理から外した後も形を返す
    //! @param[in] object 当たりを持つ配置物
    //! @param[out] outBounds 世界座標の外接箱
    //! @return 取り出せた場合 true、それ以外の場合は false
    //! 依存: NS::Obj::Actor, NS::Obj::BoxCollision, NS::Obj::SphereCollision
    [[nodiscard]] bool TryGetCollisionBounds(const NS::Obj::Actor& object, NS::AABB& outBounds) noexcept;
} // namespace NS::Game::Level
