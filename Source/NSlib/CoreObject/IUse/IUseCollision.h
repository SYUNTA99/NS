#pragma once

#include "NSlib/Core/AABB.h"
#include "NSlib/Core/Math.h"

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/BodyID.h>

#include <vector>

namespace NS
{
    namespace Phys
    {
        class PhysicsScene;
    }
} // namespace NS

namespace NS
{
    //! 地形への衝突問い合わせ。個体同士の判定はヒットセンサーを使う
    class IUseCollision
    {
    public:
        //! シーンの PhysicsScene。シーンに居ない間は nullptr
        [[nodiscard]] virtual Phys::PhysicsScene* GetPhysicsScene() const noexcept = 0;

    protected:
        ~IUseCollision() = default;
    };

    //! @brief origin から direction へ maxDistance までで一番近い当たりまでの距離を outDistance に返す
    //! @return 当たりがあれば true。シーンに居ない時と当たりが無い時は false で outDistance を変えない
    [[nodiscard]] bool RaycastCollision(const IUseCollision* user,
                                        const Vector3& origin,
                                        const Vector3& direction,
                                        float maxDistance,
                                        float& outDistance);

    [[nodiscard]] bool RaycastCollision(const IUseCollision* user,
                                        const Vector3& origin,
                                        const Vector3& direction,
                                        float maxDistance,
                                        float& outDistance,
                                        Vector3& outNormal,
                                        JPH::BodyID ignoredBody);

    //! 軸並行の境界箱で重なりを調べ、世界座標の境界箱を返す。実形状は判定しない
    //! region は幅 0 の点や柱も受け付ける。シーンなし・重なりなしなら空
    [[nodiscard]] std::vector<AABB> OverlapBoxCollision(const IUseCollision* user, const AABB& region);
} // namespace NS
