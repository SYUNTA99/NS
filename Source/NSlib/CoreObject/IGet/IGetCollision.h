#pragma once

#include "NSlib/Core/AABB.h"
#include "NSlib/Core/Math.h"

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/BodyID.h>

#include <vector>

namespace NS
{
    class Collision;

    namespace Phys
    {
        class PhysicsScene;
    }
} // namespace NS

namespace NS
{
    //! 地形への衝突問い合わせ。個体同士の判定はヒットセンサーを使う
    class IGetCollision
    {
    public:
        //! シーンの PhysicsScene。シーンに居ない間は nullptr
        [[nodiscard]] virtual Phys::PhysicsScene* GetPhysicsScene() const noexcept = 0;

    protected:
        ~IGetCollision() = default;
    };

    //! @brief origin から direction へ maxDistance までで一番近い当たりまでの距離を outDistance に返す
    //! @return 当たりがあれば true。シーンに居ない時と当たりが無い時は false で outDistance を変えない
    [[nodiscard]] bool RaycastCollision(const IGetCollision* user,
                                        const Vector3& origin,
                                        const Vector3& direction,
                                        float maxDistance,
                                        float& outDistance);

    [[nodiscard]] bool RaycastCollision(const IGetCollision* user,
                                        const Vector3& origin,
                                        const Vector3& direction,
                                        float maxDistance,
                                        float& outDistance,
                                        Vector3& outNormal,
                                        JPH::BodyID ignoredBody);

    //! 軸並行の境界箱で重なりを調べ、世界座標の境界箱を返す。実形状は判定しない
    //! region は幅 0 の点や柱も受け付ける。シーンなし・重なりなしなら空
    [[nodiscard]] std::vector<AABB> OverlapBoxCollision(const IGetCollision* user, const AABB& region);

    //! @brief 当たりの body を 1 回だけ作り、物理シーンの外に置く。出る時に EnableCollision が入れる
    //! @details 形が何も入れない形状なら作らない。user がシーンに居なければ何もしない
    //! @param[in] world 持ち主の root の世界行列
    void InitCollision(const IGetCollision* user, Collision* collision, const Matrix& world);
    //! @brief 外してある当たりの body を物理シーンへ戻し、world へ置き直す。body がまだ無ければ作る
    //! @param[in] world 持ち主の root の世界行列
    void EnableCollision(const IGetCollision* user, Collision* collision, const Matrix& world);
    //! 当たりの body を壊さずに物理シーンから外す
    void DisableCollision(const IGetCollision* user, Collision* collision);
    //! 当たりの body を壊す。壊した後と、作っていない当たりでは何もしない
    void EndCollision(const IGetCollision* user, Collision* collision);
} // namespace NS
