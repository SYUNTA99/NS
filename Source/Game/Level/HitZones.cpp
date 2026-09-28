#include "Game/Level/HitZones.h"

#include "Game/Level/ColliderBounds.h"
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
        // 負と非数は 0。無限は範囲として意味を持たないので同じく 0
        [[nodiscard]] float SanitizeRadius(float radius) noexcept
        {
            if (!std::isfinite(radius) || radius < 0.0f)
            {
                return 0.0f;
            }
            return radius;
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
        // 線に直交する水平の軸。横ずれも届く幅もこの軸で測る
        const float normalX = -dirZ;
        const float normalZ = dirX;

        NS::Core::Vector3 center;
        float halfWidth = 0.0f;
        if (!TryGetBody(center, normalX, normalZ, halfWidth))
        {
            return false;
        }

        const float toX = center.x - origin.x;
        const float toZ = center.z - origin.z;
        const float along = toX * dirX + toZ * dirZ;
        const float lateral = std::abs(toX * normalX + toZ * normalZ);
        const float reach = halfWidth + playerRadius;
        // 半幅と半径の和が 0 以下では割れない。中心扱いへ倒す
        float ratio = 0.0f;
        if (reach > 0.0f)
        {
            ratio = lateral / reach;
        }

        // 縁ちょうどは外側の段。上から順に見て最初に満たした段
        const float scale = HorizontalScale();
        HitTier tier = HitTier::Wide;
        if (lateral < m_centerRadius * scale)
        {
            tier = HitTier::Center;
        }
        else if (m_tierCount == 3 && lateral < m_nearRadius * scale)
        {
            tier = HitTier::Near;
        }

        out.tier = tier;
        out.ratio = ratio;
        out.offset01 = NS::Core::Clamp(ratio, 0.0f, 1.0f);
        out.along = along;
        out.linePoint = NS::Core::Vector3{origin.x + dirX * along, center.y, origin.z + dirZ * along};
        return true;
    }

    bool HitZones::TryGetRings(ZoneRings& out) const noexcept
    {
        NS::Core::Vector3 center;
        float halfWidth = 0.0f;
        // 円は向きを持たないので、半幅はどの軸で測っても使わない
        if (!TryGetBody(center, 1.0f, 0.0f, halfWidth))
        {
            return false;
        }
        const float scale = HorizontalScale();
        out.center = center;
        out.centerRadius = m_centerRadius * scale;
        out.nearRadius = 0.0f;
        if (m_tierCount == 3)
        {
            out.nearRadius = m_nearRadius * scale;
        }
        out.orderBroken = IsOrderBroken();
        return true;
    }

    void HitZones::SetTierCount(int count) noexcept
    {
        m_tierCount = std::clamp(count, 2, 3);
    }

    void HitZones::SetCenterRadius(float radius) noexcept
    {
        m_centerRadius = SanitizeRadius(radius);
    }

    void HitZones::SetNearRadius(float radius) noexcept
    {
        m_nearRadius = SanitizeRadius(radius);
    }

    bool HitZones::IsOrderBroken() const noexcept
    {
        return m_tierCount == 3 && m_centerRadius >= m_nearRadius;
    }

    bool HitZones::TryGetBody(NS::Core::Vector3& outCenter,
                              float normalX,
                              float normalZ,
                              float& outHalfWidth) const noexcept
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
            return true;
        }
        if (const NS::Obj::BoxCollider* box = NS::Obj::ComponentCast<NS::Obj::BoxCollider>(search.body))
        {
            // 向きの付いた箱の 3 軸を、線に直交する水平の軸へ投影した長さの和が半幅
            const NS::Core::OBB world = box->WorldOBB();
            outCenter = world.center;
            outHalfWidth = std::abs(world.axisX.x * normalX + world.axisX.z * normalZ) * world.halfExtentX +
                           std::abs(world.axisY.x * normalX + world.axisY.z * normalZ) * world.halfExtentY +
                           std::abs(world.axisZ.x * normalX + world.axisZ.z * normalZ) * world.halfExtentZ;
            return true;
        }
        return false;
    }

    float HitZones::HorizontalScale() const noexcept
    {
        const NS::Obj::GameObject* owner = Owner();
        if (owner == nullptr)
        {
            return 1.0f;
        }
        const NS::Core::Vector3 scale = NS::Core::DecomposeAffine(owner->Root().WorldMatrix()).scale;
        return std::max(std::abs(scale.x), std::abs(scale.z));
    }

    NS_CLASS(HitZones)
} // namespace NS::Game::Level
