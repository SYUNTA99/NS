#include "NSlib/Object/SubObjects/Collider.h"

#include "NSlib/Object/Actor.h"
#include "NSlib/Object/Transform.h"
#include "NSlib/Physics/JoltCharacter.h"

#include <cmath>

namespace
{
    // 寸法の欄へ書く値。負は 0、有限でなければ書く前の値
    float NonNegativeLength(float value, float current) noexcept
    {
        if (!std::isfinite(value))
        {
            return current;
        }
        if (value < 0.0f)
        {
            return 0.0f;
        }
        return value;
    }
} // namespace

namespace NS::Obj
{
    void Collider::SetCapsuleRadius(float radius) noexcept
    {
        m_radius = NonNegativeLength(radius, m_radius);
    }

    void Collider::SetStandingHalfHeight(float halfHeight) noexcept
    {
        m_standingHalfHeight = NonNegativeLength(halfHeight, m_standingHalfHeight);
    }

    float Collider::CapsuleHalfHeight() const noexcept
    {
        // 移動と裁定はどちらもここから寸法を引くので、球にしている間は両方が同じ球で当たる
        if (m_sphereShape)
        {
            return 0.0f;
        }
        return StandingHalfHeight();
    }

    NS::Phys::Capsule Collider::CapsuleAt(const NS::Vector3& rootPosition) const noexcept
    {
        // 軸は +Y 固定、中心は根。Move が JoltCharacter へ渡す (半径, 半分の高さ) と同じ 2 つの値から組む
        return NS::Phys::Capsule{rootPosition, NS::Vector3::UnitY, CapsuleHalfHeight(), CapsuleRadius()};
    }

    NS::Phys::Capsule Collider::WorldCapsule() const noexcept
    {
        if (Owner() == nullptr)
        {
            return CapsuleAt(NS::Vector3{0.0f, 0.0f, 0.0f});
        }
        return CapsuleAt(RootTransform().Position());
    }

    void Collider::SetSphereShape(bool sphere) noexcept
    {
        m_sphereShape = sphere;
    }

    NS::Phys::PhysicsScene* Collider::GetPhysicsScene() const noexcept
    {
        if (Owner() == nullptr)
        {
            return nullptr;
        }
        return Owner()->GetPhysicsScene();
    }

    ColliderMove Collider::Move(const NS::Vector3& from,
                                const NS::Vector3& velocity,
                                float dt,
                                float maxStepHeight) noexcept
    {
        ColliderMove moved;
        NS::Phys::PhysicsScene* physics = GetPhysicsScene();
        if (physics == nullptr)
        {
            moved.position = from + velocity * dt;
            moved.velocity = velocity;
            return moved;
        }

        const float radius = CapsuleRadius();
        const float halfHeight = CapsuleHalfHeight();
        // AttachScene は新しく組んだ配置物にしか呼ばれない。Scene が変わらないので m_character を作り直さない
        if (m_character == nullptr)
        {
            m_character = std::make_unique<NS::Phys::JoltCharacter>(*physics, radius, halfHeight);
        }
        m_character->Resize(radius, halfHeight);

        m_character->Step(from, velocity, dt, maxStepHeight);

        moved.position = m_character->Position();
        moved.velocity = m_character->Velocity();
        moved.grounded = m_character->IsGrounded();
        moved.inWorld = true;
        return moved;
    }
} // namespace NS::Obj
