#include "Runtime/Physics/JoltCharacter.h"

#include "Runtime/Physics/PhysicsScene.h"
#include "Runtime/Physics/detail/JoltConversion.h"

#include <Jolt/Physics/Body/Body.h>
#include <Jolt/Physics/Body/BodyFilter.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/NarrowPhaseQuery.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>

#include <algorithm>
#include <cmath>

namespace NS::Physics
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

        // Jolt のキャラクタの掃引は sensor を素通りする。床を探すレイも揃える
        // 揃えないと、ゴールやハザードの判定の箱の中で箱の内側を床と取り違える
        class IgnoreSensorsBodyFilter final : public JPH::BodyFilter
        {
        public:
            bool ShouldCollideLocked(const JPH::Body& body) const override { return !body.IsSensor(); }
        };

        bool CastRayDown(const JPH::PhysicsSystem& system, JPH::RVec3Arg origin, float length, float& outDistance)
        {
            if (!(length > 0.0f))
            {
                return false;
            }

            const JPH::RRayCast ray{origin, JPH::Vec3{0.0f, -length, 0.0f}};
            JPH::RayCastResult hit;
            if (!system.GetNarrowPhaseQuery().CastRay(ray, hit, JPH::BroadPhaseLayerFilter{}, JPH::ObjectLayerFilter{}, IgnoreSensorsBodyFilter{}))
            {
                return false;
            }

            outDistance = hit.mFraction * length;
            return true;
        }

        JPH::Ref<JPH::CharacterVirtual> MakeCharacter(JPH::PhysicsSystem& system, float radius, float halfHeight)
        {
            JPH::CharacterVirtualSettings settings;
            settings.mShape = new JPH::CapsuleShape(halfHeight, radius);
            settings.mMaxSlopeAngle = k_MaxSlopeAngleRadians;
            settings.mCollisionTolerance = k_CollisionTolerance;
            settings.mSupportingVolume = JPH::Plane{JPH::Vec3::sAxisY(), -halfHeight};
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

    void JoltCharacter::Step(const NS::Core::Vector3& position,
                             const NS::Core::Vector3& velocity,
                             float dt,
                             float maxStepHeight)
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
            // 登れる急さの面で打ち消すのは、先に済ませた接地の法線だけ
            // 縦の成分を 0 にした向きで抜くと坂を上る前進まで消える
            // 30° の坂を上り続けるはずの 1 秒で 55 cm 下がった
            if (contact.mContactNormal.GetY() >= k_MaxSlopeNormalY)
            {
                continue;
            }

            // 上を向いた急な面では法線の縦の成分を 0 にした向きで抜く
            // 斜めの法線のまま打ち消すと前進の一部が上向きの速度に変わり、登れない 27 cm の段を越えた
            // 速さ 20 m/s では 0.6 m 跳ねた
            // 下を向いた面は法線のまま抜く。縦の成分を 0 にすると天井に当たった上向きの速度が残る
            // 天井に 25 フレーム張り付いた
            JPH::Vec3 cancelNormal = contact.mContactNormal;
            if (cancelNormal.GetY() > 0.0f)
            {
                cancelNormal = JPH::Vec3{cancelNormal.GetX(), 0.0f, cancelNormal.GetZ()}.NormalizedOr(JPH::Vec3::sZero());
            }
            correctedVelocity = CancelInto(correctedVelocity, cancelNormal);
        }

        if (IsGrounded())
        {
            const JPH::Vec3 groundNormal = m_character->GetGroundNormal();
            const bool standingOnWalkable = groundNormal.GetY() >= k_MaxSlopeNormalY;

            // 上向きの速度は足場の面に沿って進む分まで残す
            // 角を滑り上がった速度は次のフレームへ持ち越さない。持ち越すと 25 cm の段で 9 cm 跳ね、27 cm の段を越えた
            // 0 で切ると登り坂の速さまで削れ、登り坂の基準の軌跡が 8 m ずれた
            float alongGroundY = 0.0f;
            if (standingOnWalkable)
            {
                // 速度と法線の内積が 0 になる縦の速度
                const float horizontalDot = correctedVelocity.GetX() * groundNormal.GetX() + correctedVelocity.GetZ() * groundNormal.GetZ();
                alongGroundY = -horizontalDot / groundNormal.GetY();
            }
            correctedVelocity.SetY(std::min(correctedVelocity.GetY(), std::max(alongGroundY, 0.0f)));

            // 並んだ床の継ぎ目で角に押し上げられた分を、同じフレームのうちに床へ戻す
            // 戻さないと速さ 20 m/s で 8 cm 浮き、継ぎ目を走る間に接地を 43 フレーム失った
            // 探すのは足場の接点の高さまで。それより低い床へ吸い付くと、床の端で角に沿って 20 cm 沈んだ
            const JPH::RVec3 bottomSphereCenter = m_character->GetPosition() - JPH::Vec3{0.0f, m_halfHeight, 0.0f};
            const float groundY = static_cast<float>(m_character->GetGroundPosition().GetY());
            const float rayLength = static_cast<float>(bottomSphereCenter.GetY()) - groundY + k_CollisionTolerance;
            float floorDistance = 0.0f;
            const bool floorAtFootHeight = CastRayDown(m_physics.m_physicsSystem, bottomSphereCenter, rayLength, floorDistance);
            const bool hoveringOverFloor = floorAtFootHeight && floorDistance - m_radius > k_CollisionTolerance;
            // 進む先の角へは真下に床が無くても戻す。1 フレームで角の接平面に沿って進むと角から浮く
            // 戻さないと、速さ 20 m/s で 19 cm の段を登ったフレームに段の上面より 1 cm 浮いた
            // 離れていく後ろの角へ戻すと、速さ 20 m/s で床の端を走り抜ける時に 31 cm 沈んだ
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

    NS::Core::Vector3 JoltCharacter::Position() const
    {
        return FromJolt(m_character->GetPosition());
    }

    NS::Core::Vector3 JoltCharacter::Velocity() const
    {
        return FromJolt(m_character->GetLinearVelocity());
    }

    bool JoltCharacter::IsGrounded() const
    {
        return m_character->GetGroundState() == JPH::CharacterBase::EGroundState::OnGround;
    }

} // namespace NS::Physics
