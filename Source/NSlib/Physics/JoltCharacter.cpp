#include "NSlib/Physics/JoltCharacter.h"

#include "NSlib/Core/Assert.h"
#include "NSlib/Core/Math.h"
#include "NSlib/Physics/PhysicsScene.h"
#include "NSlib/Physics/detail/JoltConversion.h"

#include <Jolt/Physics/Body/Body.h>
#include <Jolt/Physics/Body/BodyFilter.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/NarrowPhaseQuery.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>

#include <algorithm>
#include <cmath>

namespace NS::Phys
{
    namespace
    {
        constexpr float k_MaxSlopeAngleRadians = 0.7853982f;
        // 登れる急さの上限をそのまま法線の Y 成分で表した値
        const float k_MaxSlopeNormalY = std::cos(k_MaxSlopeAngleRadians);
        // Jolt の既定値と同じ。浮きの判定もこの距離で切り、Jolt が触れていると数える距離と揃える
        constexpr float k_CollisionTolerance = 1.0e-3f;

        JPH::Vec3 CancelInto(JPH::Vec3Arg velocity, JPH::Vec3Arg normal)
        {
            const float into = velocity.Dot(normal);
            if (into >= 0.0f)
            {
                return velocity;
            }
            return velocity - normal * into;
        }

        bool CastRayDown(const JPH::PhysicsSystem& system, JPH::RVec3Arg origin, float length, float& outDistance)
        {
            if (!(length > 0.0f))
            {
                return false;
            }

            const JPH::RRayCast ray{origin, JPH::Vec3{0.0f, -length, 0.0f}};
            JPH::RayCastResult hit;
            if (!system.GetNarrowPhaseQuery().CastRay(ray, hit))
            {
                return false;
            }

            outDistance = hit.mFraction * length;
            return true;
        }

        JPH::Ref<JPH::CharacterVirtual> MakeCharacter(JPH::PhysicsSystem& system, float radius, float halfHeight)
        {
            NS_ASSERT(Physics, NS::IsPositiveFinite(radius), "JoltCharacter: 半径が正でない ({})", radius);
            JPH::CharacterVirtualSettings settings;
            // 半長 0 は同じ半径の球。JPH::CapsuleShape は半長 0 を assert で断るので球で作る
            float supportHeight = halfHeight;
            if (halfHeight <= 0.0f)
            {
                settings.mShape = new JPH::SphereShape(radius);
                supportHeight = 0.0f;
            }
            else
            {
                settings.mShape = new JPH::CapsuleShape(halfHeight, radius);
            }
            settings.mMaxSlopeAngle = k_MaxSlopeAngleRadians;
            settings.mCollisionTolerance = k_CollisionTolerance;
            settings.mSupportingVolume = JPH::Plane{JPH::Vec3::sAxisY(), -supportHeight};
            return new JPH::CharacterVirtual(&settings, JPH::RVec3::sZero(), JPH::Quat::sIdentity(), &system);
        }
    } // namespace

    JoltCharacter::JoltCharacter(PhysicsScene& physics, float radius, float halfHeight)
        : m_physics(physics), m_radius(radius), m_halfHeight(halfHeight),
          m_character(MakeCharacter(physics.m_physicsSystem, radius, halfHeight))
    {}

    JoltCharacter::~JoltCharacter() = default;

    void JoltCharacter::Resize(float radius, float halfHeight)
    {
        if (radius == m_radius && halfHeight == m_halfHeight)
        {
            return;
        }

        m_radius = radius;
        m_halfHeight = halfHeight;
        m_character = MakeCharacter(m_physics.m_physicsSystem, radius, halfHeight);
    }

    void JoltCharacter::Step(const NS::Vector3& position, const NS::Vector3& velocity, float dt, float maxStepHeight)
    {
        m_character->SetPosition(ToJolt(position));
        m_character->SetLinearVelocity(ToJolt(velocity));

        const float stepHeight = std::max(0.0f, maxStepHeight);
        // 半径 × (1 − cos 45°) までの角は CharacterVirtual::Update が登れる面として滑り越える
        // WalkStairs には残りの高さだけを渡す。maxStepHeight の 25 cm をそのまま渡すと 45 cm の段まで登った
        const float slideUpHeight = m_radius * (1.0f - k_MaxSlopeNormalY);
        JPH::CharacterVirtual::ExtendedUpdateSettings settings;
        settings.mWalkStairsStepUp = JPH::Vec3{0.0f, std::max(0.0f, stepHeight - slideUpHeight), 0.0f};
        // 足場を離れたフレームには吸い付けない
        // 吸い付けると床の端で角に沿って接地のまま沈み、コヨーテジャンプが 9 cm 低い所から出た
        settings.mStickToFloorStepDown = JPH::Vec3::sZero();
        m_character->ExtendedUpdate(dt,
                                    JPH::Vec3::sZero(),
                                    settings,
                                    JPH::BroadPhaseLayerFilter{},
                                    JPH::ObjectLayerFilter{},
                                    JPH::BodyFilter{},
                                    JPH::ShapeFilter{},
                                    m_physics.m_tempAllocator);

        // 呼出側が毎フレーム速度を足すので、面へ向かう分を抜かないと押し付けている間に溜まり、離したフレームに飛び出す
        JPH::Vec3 correctedVelocity = m_character->GetLinearVelocity();
        if (IsGrounded())
        {
            correctedVelocity = CancelInto(correctedVelocity, m_character->GetGroundNormal());
        }

        for (const JPH::CharacterContact& contact : m_character->GetActiveContacts())
        {
            // 予測だけで終わった接触は当たっていない。ここで速度を削ると触れてもいない壁で止まる
            if (!contact.mHadCollision || contact.mWasDiscarded)
            {
                continue;
            }
            // 登れる斜面は接地の法線で処理済み。ここで水平化して打ち消すと登る速度まで消える。
            if (IsWalkableNormal(contact.mContactNormal.GetY()))
            {
                continue;
            }

            // 急斜面は水平化して段差での跳ね上がりを防ぐ。天井は上向き速度を消すため法線をそのまま使う。
            JPH::Vec3 cancelNormal = contact.mContactNormal;
            if (cancelNormal.GetY() > 0.0f)
            {
                cancelNormal =
                    JPH::Vec3{cancelNormal.GetX(), 0.0f, cancelNormal.GetZ()}.NormalizedOr(JPH::Vec3::sZero());
            }
            correctedVelocity = CancelInto(correctedVelocity, cancelNormal);
        }

        if (IsGrounded())
        {
            const JPH::Vec3 groundNormal = m_character->GetGroundNormal();
            const bool standingOnWalkable = IsWalkableNormal(groundNormal.GetY());

            // 登り坂に必要な上向き速度は保ち、角で跳ね上がった余剰分だけを消す。
            float alongGroundY = 0.0f;
            if (standingOnWalkable)
            {
                // 速度と法線の内積が 0 になる縦の速度
                const float horizontalDot =
                    correctedVelocity.GetX() * groundNormal.GetX() + correctedVelocity.GetZ() * groundNormal.GetZ();
                alongGroundY = -horizontalDot / groundNormal.GetY();
            }
            correctedVelocity.SetY(std::min(correctedVelocity.GetY(), std::max(alongGroundY, 0.0f)));

            // 継ぎ目で浮いた身体を、足場の接点の高さまで戻す。低い床まで探すと端で沈む。
            const JPH::RVec3 bottomSphereCenter = m_character->GetPosition() - JPH::Vec3{0.0f, m_halfHeight, 0.0f};
            const float groundY = static_cast<float>(m_character->GetGroundPosition().GetY());
            const float rayLength = static_cast<float>(bottomSphereCenter.GetY()) - groundY + k_CollisionTolerance;
            float floorDistance = 0.0f;
            const bool floorAtFootHeight =
                CastRayDown(m_physics.m_physicsSystem, bottomSphereCenter, rayLength, floorDistance);
            const bool hoveringOverFloor = floorAtFootHeight && floorDistance - m_radius > k_CollisionTolerance;
            // 前方の角で浮いた分は戻す。離れていく後方の角へ戻すと床の端で沈む。
            const JPH::Vec3 toGround = JPH::Vec3(m_character->GetGroundPosition() - bottomSphereCenter);
            const bool groundAhead = toGround.GetX() * velocity.x + toGround.GetZ() * velocity.z > 0.0f;
            const bool hoveringOverGroundAhead = groundAhead && toGround.Length() - m_radius > k_CollisionTolerance;
            // 足場の法線が登れない急さのフレームは戻さない。吸い付けると Jolt が角だけを足場に数え直して接地から外れる
            // 25 cm の段で 1 フレーム接地を失った
            if (standingOnWalkable && (hoveringOverFloor || hoveringOverGroundAhead))
            {
                m_character->StickToFloor(JPH::Vec3{0.0f, -rayLength, 0.0f},
                                          JPH::BroadPhaseLayerFilter{},
                                          JPH::ObjectLayerFilter{},
                                          JPH::BodyFilter{},
                                          JPH::ShapeFilter{},
                                          m_physics.m_tempAllocator);
            }
        }
        m_character->SetLinearVelocity(correctedVelocity);
    }

    NS::Vector3 JoltCharacter::Position() const
    {
        return FromJolt(m_character->GetPosition());
    }

    NS::Vector3 JoltCharacter::Velocity() const
    {
        return FromJolt(m_character->GetLinearVelocity());
    }

    bool JoltCharacter::IsGrounded() const
    {
        return m_character->GetGroundState() == JPH::CharacterBase::EGroundState::OnGround;
    }

    bool IsWalkableNormal(float normalY) noexcept
    {
        return normalY >= k_MaxSlopeNormalY;
    }

} // namespace NS::Phys
