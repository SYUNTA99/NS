#include "Runtime/Physics/JoltCharacter.h"

#include "Runtime/Physics/PhysicsWorld.h"
#include "Runtime/Physics/detail/JoltConversion.h"

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

        JPH::Vec3 CancelInto(JPH::Vec3Arg velocity, JPH::Vec3Arg normal)
        {
            const float into = velocity.Dot(normal);
            if (into >= 0.0f)
            {
                return velocity;
            }
            return velocity - normal * into;
        }

        JPH::Ref<JPH::CharacterVirtual> MakeCharacter(JPH::PhysicsSystem& system, float radius, float halfHeight)
        {
            JPH::CharacterVirtualSettings settings;
            settings.mShape = new JPH::CapsuleShape(halfHeight, radius);
            settings.mMaxSlopeAngle = k_MaxSlopeAngleRadians;
            settings.mSupportingVolume = JPH::Plane{JPH::Vec3::sAxisY(), -halfHeight};
            return new JPH::CharacterVirtual(&settings, JPH::RVec3::sZero(), JPH::Quat::sIdentity(), &system);
        }
    } // namespace

    JoltCharacter::JoltCharacter(PhysicsWorld& world, float radius, float halfHeight)
        : m_world(world), m_radius(radius), m_halfHeight(halfHeight),
          m_character(MakeCharacter(world.m_physicsSystem, radius, halfHeight))
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
        m_character = MakeCharacter(m_world.m_physicsSystem, radius, halfHeight);
    }

    void JoltCharacter::Step(const NS::Core::Vector3& position, const NS::Core::Vector3& velocity, float dt)
    {
        m_character->SetPosition(ToJolt(position));
        m_character->SetLinearVelocity(ToJolt(velocity));
        m_character->Update(dt,
                            JPH::Vec3::sZero(),
                            JPH::BroadPhaseLayerFilter{},
                            JPH::ObjectLayerFilter{},
                            JPH::BodyFilter{},
                            JPH::ShapeFilter{},
                            m_world.m_tempAllocator);

        // 呼出側が毎歩速度を足すので、面へ向かう分を抜かないと押し付けている間に溜まり、離した歩に飛び出す
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
            // 登れる急さの面で打ち消すのは上の接地の法線だけ。継ぎ目の斜めの法線をここで拾うと前進が上向きへ化ける
            if (contact.mContactNormal.GetY() >= k_MaxSlopeNormalY)
            {
                continue;
            }

            correctedVelocity = CancelInto(correctedVelocity, contact.mContactNormal);
        }
        m_character->SetLinearVelocity(correctedVelocity);

        // TODO: 隣り合う地形を 1 つの body へまとめれば角が消えて、この置き直しは要らなくなる
        // 並んだ床の継ぎ目では予測接触に押し上げられる。接地している歩は接点から出た高さへ戻す
        // 上向きの速度が残る歩は触らない。跳んだ直後と登り坂がこれに当たる
        NS::Core::Vector3 correctedPosition = Position();
        if (IsGrounded() && correctedVelocity.GetY() <= 0.0f)
        {
            correctedPosition.y = std::min(correctedPosition.y, RestingHeightOnGround());
            m_character->SetPosition(ToJolt(correctedPosition));
        }
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

    float JoltCharacter::RestingHeightOnGround() const
    {
        // 下端の球は接点から法線方向へ半径ぶん離れた所に中心がある
        const JPH::Vec3 normal = m_character->GetGroundNormal();
        const float sphereCenterY =
            static_cast<float>(m_character->GetGroundPosition().GetY()) + normal.GetY() * m_radius;

        return sphereCenterY + m_halfHeight;
    }

} // namespace NS::Physics
