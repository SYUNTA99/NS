#include "Game/Level/HitZones.h"

#include "Game/Level/ColliderBounds.h"
#include "Runtime/Core/Logger.h"
#include "Runtime/Core/OBB.h"
#include "Runtime/Core/Sphere.h"
#include "Runtime/Object/Components/BoxCollider.h"
#include "Runtime/Object/Components/CapsuleCollider.h"
#include "Runtime/Object/Components/SphereCollider.h"
#include "Runtime/Object/GameObject.h"
#include "Runtime/Object/ObjectList.h"
#include "Runtime/Object/Reflection/TypeRegistry.h"
#include "Runtime/Object/Scene/Scene.h"
#include "Runtime/Object/Transform.h"

#include <algorithm>
#include <cmath>
#include <string>

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

        // 線からの水平の符号付きの距離。線に直交する水平の軸で測る
        [[nodiscard]] float SignedLateral(const NS::Core::Vector3& point,
                                          const NS::Core::Vector3& origin,
                                          float normalX,
                                          float normalZ) noexcept
        {
            return (point.x - origin.x) * normalX + (point.z - origin.z) * normalZ;
        }

        // 段の形に使える当たり判定か。物理に入れない球・カプセル・箱だけ
        // 物理に入るものは体や押し戻しにも効くので、段の形にすると物理が変わる
        [[nodiscard]] bool IsUsableTierShape(const NS::Obj::Collider& shape) noexcept
        {
            if (!shape.IsExcludedFromPhysics())
            {
                return false;
            }
            return NS::Obj::ComponentCast<NS::Obj::SphereCollider>(&shape) != nullptr ||
                   NS::Obj::ComponentCast<NS::Obj::CapsuleCollider>(&shape) != nullptr ||
                   NS::Obj::ComponentCast<NS::Obj::BoxCollider>(&shape) != nullptr;
        }

        // 上から見て線が形を通るか。縁ちょうどは通らない
        [[nodiscard]] bool LinePassesShape(const NS::Obj::Collider& shape,
                                           const NS::Core::Vector3& origin,
                                           float normalX,
                                           float normalZ) noexcept
        {
            if (const NS::Obj::SphereCollider* sphere = NS::Obj::ComponentCast<NS::Obj::SphereCollider>(&shape))
            {
                const NS::Core::Sphere world = sphere->WorldSphere();
                return std::abs(SignedLateral(world.center, origin, normalX, normalZ)) < world.radius;
            }
            if (const NS::Obj::CapsuleCollider* capsule = NS::Obj::ComponentCast<NS::Obj::CapsuleCollider>(&shape))
            {
                // 軸の線分の両端が線の両側にあれば、線分は線と交わり距離 0
                const NS::Phys::Capsule world = capsule->WorldCapsule();
                const float first =
                    SignedLateral(world.center - world.axis * world.halfHeight, origin, normalX, normalZ);
                const float second =
                    SignedLateral(world.center + world.axis * world.halfHeight, origin, normalX, normalZ);
                float distance = std::min(std::abs(first), std::abs(second));
                if ((first < 0.0f && second > 0.0f) || (first > 0.0f && second < 0.0f))
                {
                    distance = 0.0f;
                }
                return distance < world.radius;
            }
            if (const NS::Obj::BoxCollider* box = NS::Obj::ComponentCast<NS::Obj::BoxCollider>(&shape))
            {
                // 8 つの角のうち、線の片側だけに寄っていなければ線が箱を通る
                const NS::Core::OBB world = box->WorldOBB();
                bool negative = false;
                bool positive = false;
                for (int corner = 0; corner < 8; ++corner)
                {
                    float signX = 1.0f;
                    if ((corner & 1) != 0)
                    {
                        signX = -1.0f;
                    }
                    float signY = 1.0f;
                    if ((corner & 2) != 0)
                    {
                        signY = -1.0f;
                    }
                    float signZ = 1.0f;
                    if ((corner & 4) != 0)
                    {
                        signZ = -1.0f;
                    }
                    const NS::Core::Vector3 point = world.center + world.axisX * (world.halfExtentX * signX) +
                                                    world.axisY * (world.halfExtentY * signY) +
                                                    world.axisZ * (world.halfExtentZ * signZ);
                    const float side = SignedLateral(point, origin, normalX, normalZ);
                    if (side < 0.0f)
                    {
                        negative = true;
                    }
                    if (side > 0.0f)
                    {
                        positive = true;
                    }
                }
                return negative && positive;
            }
            return false;
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
        // 名指しした形があればその形、無ければ体の中心まわりの範囲 (m) を線が通るかで見る
        const float scale = HorizontalScale();
        bool passesCenter = lateral < m_centerRadius * scale;
        if (const NS::Obj::Collider* shape = FindTierShape(HitTier::Center))
        {
            passesCenter = LinePassesShape(*shape, origin, normalX, normalZ);
        }
        bool passesNear = m_tierCount == 3 && lateral < m_nearRadius * scale;
        if (const NS::Obj::Collider* shape = FindTierShape(HitTier::Near))
        {
            passesNear = LinePassesShape(*shape, origin, normalX, normalZ);
        }

        HitTier tier = HitTier::Wide;
        if (passesCenter)
        {
            tier = HitTier::Center;
        }
        else if (passesNear)
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
        // 名指しした段は形そのものを描くので円にしない
        const float scale = HorizontalScale();
        out.center = center;
        out.centerRadius = 0.0f;
        if (FindTierShape(HitTier::Center) == nullptr)
        {
            out.centerRadius = m_centerRadius * scale;
        }
        out.nearRadius = 0.0f;
        if (m_tierCount == 3 && FindTierShape(HitTier::Near) == nullptr)
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
        // 形と範囲 (m) は大きさを比べられない。どちらかを名指ししていれば崩れと言わない
        if (m_tierCount != 3)
        {
            return false;
        }
        if (FindTierShape(HitTier::Center) != nullptr || FindTierShape(HitTier::Near) != nullptr)
        {
            return false;
        }
        return m_centerRadius >= m_nearRadius;
    }

    void HitZones::SetCenterShape(const NS::Obj::ComponentRef<NS::Obj::Collider>& shape) noexcept
    {
        m_centerShape = shape;
        m_warnedCenterShape = false;
    }

    void HitZones::SetNearShape(const NS::Obj::ComponentRef<NS::Obj::Collider>& shape) noexcept
    {
        m_nearShape = shape;
        m_warnedNearShape = false;
    }

    const NS::Obj::Collider* HitZones::FindTierShape(HitTier tier) const noexcept
    {
        if (tier == HitTier::Center)
        {
            return ResolveTierShape(m_centerShape, "真ん中", m_warnedCenterShape);
        }
        if (tier == HitTier::Near && m_tierCount == 3)
        {
            return ResolveTierShape(m_nearShape, "惜しい", m_warnedNearShape);
        }
        return nullptr;
    }

    const NS::Obj::Collider* HitZones::ResolveTierShape(const NS::Obj::ComponentRef<NS::Obj::Collider>& ref,
                                                        const char* tierName,
                                                        bool& warned) const noexcept
    {
        if (!ref.IsSet())
        {
            return nullptr;
        }
        const NS::Obj::GameObject* owner = Owner();
        const NS::Obj::Collider* shape = nullptr;
        if (owner != nullptr && owner->OwningScene() != nullptr)
        {
            shape = owner->OwningScene()->Objects().FindComponent(ref);
        }
        if (shape != nullptr && IsUsableTierShape(*shape))
        {
            return shape;
        }
        if (!warned)
        {
            warned = true;
            std::string ownerName;
            if (owner != nullptr)
            {
                ownerName = owner->Name();
            }
            NS_LOG_WARN(
                Game,
                "HitZones: '{}' の{}の形が見つからないか、物理に入れない球・カプセル・箱でないので、{}の範囲 (m) "
                "で段を決める",
                ownerName,
                tierName,
                tierName);
        }
        return nullptr;
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
