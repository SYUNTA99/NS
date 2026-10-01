#pragma once

#include "Runtime/Core/Math.h"

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/BodyID.h>

namespace NS::Phys
{
    class PhysicsScene;
}

namespace NS::Obj
{
    //! @brief 地形の当たりの窓口。シーンの PhysicsScene を引ける物が持つ
    //! @details Actor・UIActor・シーンが持つ。部品は持ち主の Actor から引く
    //! Actor 同士の当たりはヒットセンサーとメッセージで見る。ここは床や壁との当たりを問う口
    class IUseCollision
    {
    public:
        //! シーンの PhysicsScene。シーンに居ない間は nullptr
        [[nodiscard]] virtual NS::Phys::PhysicsScene* GetPhysicsScene() const noexcept = 0;

    protected:
        ~IUseCollision() = default;
    };

    //! @brief origin から direction へ maxDistance までで一番近い当たりまでの距離を outDistance に返す
    //! @return 当たりがあれば true。シーンに居ない時と当たりが無い時は false で outDistance を変えない
    [[nodiscard]] bool RaycastCollision(const IUseCollision& user,
                                        const NS::Core::Vector3& origin,
                                        const NS::Core::Vector3& direction,
                                        float maxDistance,
                                        float& outDistance);

    [[nodiscard]] bool RaycastCollision(const IUseCollision& user,
                                        const NS::Core::Vector3& origin,
                                        const NS::Core::Vector3& direction,
                                        float maxDistance,
                                        float& outDistance,
                                        NS::Core::Vector3& outNormal,
                                        JPH::BodyID ignoredBody);
} // namespace NS::Obj
