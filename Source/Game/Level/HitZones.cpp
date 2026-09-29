#include "Game/Level/HitZones.h"

#include "Game/Level/ColliderBounds.h"
#include "Game/Level/HitZoneArea.h"
#include "Runtime/Core/Logger.h"
#include "Runtime/Core/OBB.h"
#include "Runtime/Core/Sphere.h"
#include "Runtime/Object/Components/BoxCollider.h"
#include "Runtime/Object/Components/SphereCollider.h"
#include "Runtime/Object/GameObject.h"
#include "Runtime/Object/Reflection/TypeRegistry.h"
#include "Runtime/Object/Transform.h"

#include <algorithm>
#include <cmath>

namespace NS::Game::Level
{
    namespace
    {
        // 自機の玉が相手の表面に触れる向き。相手の中心から見た単位ベクトル
        // 玉の中心が中心から touchRadius の球に入った所で触れる。直線がその球に入る点の向きで、origin がもう球の中でも
        // 入った点へ戻って測る。予測は遠くから、当たりは触れてから呼ぶので、今の位置で測ると箱の相手で両者が割れる
        // 直線が届かない時と、球が origin の後ろにある時は、直線に一番近い点の向き
        [[nodiscard]] NS::Core::Vector3 FindTouchDirection(const NS::Core::Vector3& center,
                                                           float touchRadius,
                                                           const NS::Core::Vector3& origin,
                                                           const NS::Core::Vector3& direction) noexcept
        {
            const NS::Core::Vector3 fromCenter = origin - center;
            const float along = fromCenter.Dot(direction);
            const float outside = fromCenter.Dot(fromCenter) - touchRadius * touchRadius;
            const float discriminant = along * along - outside;
            NS::Core::Vector3 point = origin + direction * -along;
            if (outside <= 0.0f || (discriminant >= 0.0f && along < 0.0f))
            {
                point = origin + direction * (-along - std::sqrt(discriminant));
            }
            NS::Core::Vector3 touch = point - center;
            // 玉の中心が相手の中心に重なると向きが無い。来た側から触れたことにする
            if (touch.LengthSquared() < NS::Core::k_Epsilon * NS::Core::k_Epsilon)
            {
                touch = -direction;
            }
            touch.Normalize();
            return touch;
        }
    } // namespace

    bool HitZones::Judge(const NS::Core::Vector3& origin,
                         const NS::Core::Vector3& direction,
                         float playerRadius,
                         HitZoneJudgement& out) const noexcept
    {
        const float horizontal = std::sqrt(direction.x * direction.x + direction.z * direction.z);
        if (!std::isfinite(horizontal) || !(horizontal > NS::Core::k_Epsilon))
        {
            return false;
        }
        const float dirX = direction.x / horizontal;
        const float dirZ = direction.z / horizontal;
        // 線に直交する水平の軸。左手系で上から見て、進む向きの右
        const float rightX = dirZ;
        const float rightZ = -dirX;

        NS::Core::Vector3 center;
        float halfWidth = 0.0f;
        float halfHeight = 0.0f;
        float surfaceRadius = 0.0f;
        if (!TryGetBody(center, rightX, rightZ, halfWidth, halfHeight, surfaceRadius))
        {
            return false;
        }

        const float toX = center.x - origin.x;
        const float toZ = center.z - origin.z;
        const float along = toX * dirX + toZ * dirZ;
        // 線が相手の中心から右へずれている長さ
        const float right = -(toX * rightX + toZ * rightZ);
        const float reach = halfWidth + playerRadius;
        const float reachUp = halfHeight + playerRadius;
        // 半幅と半径の和が 0 以下では割れない。真ん中扱いへ倒す
        float u = 0.0f;
        if (reach > 0.0f)
        {
            u = right / reach;
        }
        float v = 0.0f;
        if (reachUp > 0.0f)
        {
            v = (origin.y - center.y) / reachUp;
        }

        // 気持ちいいの色を外れの色より先に見る。同じ段の色が重なる所は、部品の並びで先の色
        const HitZoneArea* centerArea = nullptr;
        const HitZoneArea* wideArea = nullptr;
        for (const NS::Obj::Component* component : Owner()->Components())
        {
            const HitZoneArea* area = NS::Obj::ComponentCast<HitZoneArea>(component);
            if (area == nullptr || !area->Contains(u, v))
            {
                continue;
            }
            if (area->IsCenter() && centerArea == nullptr)
            {
                centerArea = area;
            }
            if (!area->IsCenter() && wideArea == nullptr)
            {
                wideArea = area;
            }
        }
        out.tier = HitTier::Wide;
        out.powerScale = m_remainderPowerScale;
        if (centerArea != nullptr)
        {
            out.tier = HitTier::Center;
            out.powerScale = centerArea->PowerScale();
        }
        else if (wideArea != nullptr)
        {
            out.powerScale = wideArea->PowerScale();
        }
        out.u = u;
        out.v = v;
        out.ratio = std::abs(u);
        out.offset01 = NS::Core::Clamp(out.ratio, 0.0f, 1.0f);
        out.along = along;
        out.linePoint = NS::Core::Vector3{origin.x + dirX * along, center.y, origin.z + dirZ * along};
        const NS::Core::Vector3 lineDirection{dirX, 0.0f, dirZ};
        out.surfacePoint =
            center + FindTouchDirection(center, surfaceRadius + playerRadius, origin, lineDirection) * surfaceRadius;
        return true;
    }

    void HitZones::SetRemainderPowerScale(float scale) noexcept
    {
        if (!std::isfinite(scale))
        {
            m_remainderPowerScale = 0.0f;
            return;
        }
        m_remainderPowerScale = std::max(scale, 0.0f);
    }

    bool HitZones::TryGetBody(NS::Core::Vector3& outCenter,
                              float normalX,
                              float normalZ,
                              float& outHalfWidth,
                              float& outHalfHeight,
                              float& outSurfaceRadius) const noexcept
    {
        const NS::Obj::GameObject* owner = Owner();
        if (owner == nullptr)
        {
            return false;
        }
        const BodyColliderSearch search = FindBodyCollider(*owner);
        if (search.count > 1 && !m_warnedAmbiguousBody)
        {
            m_warnedAmbiguousBody = true;
            NS_LOG_WARN(Game,
                        "HitZones: '{}' にぶつかる当たり判定が {} 個あり、どれで段を測るか決まらないので判定しない",
                        owner->Name(),
                        search.count);
        }

        if (const NS::Obj::SphereCollider* sphere = NS::Obj::ComponentCast<NS::Obj::SphereCollider>(search.body))
        {
            const NS::Core::Sphere world = sphere->WorldSphere();
            outCenter = world.center;
            outHalfWidth = world.radius;
            outHalfHeight = world.radius;
            outSurfaceRadius = world.radius;
            return true;
        }
        if (const NS::Obj::BoxCollider* box = NS::Obj::ComponentCast<NS::Obj::BoxCollider>(search.body))
        {
            // 向きの付いた箱の 3 軸を、線に直交する水平の軸と縦の軸へ投影した長さの和が半幅と半分の高さ
            const NS::Core::OBB world = box->WorldOBB();
            outCenter = world.center;
            outHalfWidth = std::abs(world.axisX.x * normalX + world.axisX.z * normalZ) * world.halfExtentX +
                           std::abs(world.axisY.x * normalX + world.axisY.z * normalZ) * world.halfExtentY +
                           std::abs(world.axisZ.x * normalX + world.axisZ.z * normalZ) * world.halfExtentZ;
            outHalfHeight = std::abs(world.axisX.y) * world.halfExtentX + std::abs(world.axisY.y) * world.halfExtentY +
                            std::abs(world.axisZ.y) * world.halfExtentZ;
            outSurfaceRadius = std::sqrt(world.halfExtentX * world.halfExtentX + world.halfExtentY * world.halfExtentY +
                                         world.halfExtentZ * world.halfExtentZ);
            return true;
        }
        return false;
    }

    NS_CLASS(HitZones)
} // namespace NS::Game::Level
