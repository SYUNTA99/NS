#pragma once

#include "Runtime/Core/AABB.h"
#include "Runtime/Core/Math.h"

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/BodyID.h>

#include <vector>

namespace NS::Phys
{
    class PhysicsScene;
}

namespace NS::Obj
{
    //! @brief 地形の当たりの窓口。シーンの PhysicsScene を引ける物が持つ
    //! @details Actor・UIActor・シーンと、動く体の部品 Body が持つ。他の部品は持ち主の Actor から引く
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

    //! @brief region に重なる body の世界座標の境界箱を集めて返す
    //! @details 重なりを見るのも返すのも軸並行の境界箱で、shape の形は見ない。答えは PhysicsScene::OverlapBox と同じ
    //! @param[in] user 問う物。シーンの PhysicsScene をここから引く
    //! @param[in] region 調べる軸並行の箱。幅 0 の柱や点も渡せる
    //! @return 重なった body の境界箱の並び。シーンに居ない時と重なりが無い時は空
    [[nodiscard]] std::vector<NS::Core::AABB> OverlapBoxCollision(const IUseCollision& user,
                                                                  const NS::Core::AABB& region);
} // namespace NS::Obj
