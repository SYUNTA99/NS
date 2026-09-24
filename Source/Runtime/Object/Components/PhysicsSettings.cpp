#include "Runtime/Object/Components/PhysicsSettings.h"

#include "Runtime/Object/GameObject.h"
#include "Runtime/Object/Reflection/TypeRegistry.h"
#include "Runtime/Object/Scene/Scene.h"

namespace NS::Obj
{
    PhysicsSettings::PhysicsSettings() noexcept : Component(TickPriority::EarlyUpdate) {}

    void PhysicsSettings::OnStart()
    {
        Apply(m_gravity);
    }

    void PhysicsSettings::OnUpdate()
    {
        Apply(m_gravity);
    }

    void PhysicsSettings::OnEndPlay()
    {
        // 残すと、設定を消したシーンでも消す前の重力のまま回り続ける
        Apply(NS::Phys::DefaultGravity());
    }

    void PhysicsSettings::Apply(const NS::Core::Vector3& gravity) noexcept
    {
        GameObject* owner = Owner();
        if (owner == nullptr || owner->OwningScene() == nullptr)
        {
            return;
        }
        owner->OwningScene()->Physics().SetGravity(gravity);
    }

    NS_CLASS(PhysicsSettings)
} // namespace NS::Obj
