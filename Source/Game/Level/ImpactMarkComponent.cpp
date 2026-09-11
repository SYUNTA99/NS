#include "Game/Level/ImpactMarkComponent.h"

#include "Runtime/Core/Clock.h"
#include "Runtime/Object/Components/MeshRendererComponent.h"
#include "Runtime/Object/GameObject.h"
#include "Runtime/Object/Reflection/TypeRegistry.h"
#include "Runtime/Object/Scene/Scene.h"
#include "Runtime/Object/Transform.h"

#include <memory>
#include <utility>

namespace NS::Game::Level
{
    NS::Object::GameObject* ImpactMarkComponent::SpawnAt(NS::Object::Scene* scene, const NS::Core::Vector3& position)
    {
        if (scene == nullptr)
            return nullptr;

        // 組み立ててから渡す。SpawnTransient の資産の引き当ては渡した時に持っている Component にしか効かない
        auto owned = std::make_unique<NS::Object::GameObject>();
        owned->Root().SetPosition(position);
        auto* mesh = owned->AddComponent<NS::Object::MeshRendererComponent>();
        mesh->SetMeshRef("shadowQuad");
        mesh->SetMaterialRef("shadow");
        owned->AddComponent<ImpactMarkComponent>();
        return scene->SpawnTransient(std::move(owned));
    }

    void ImpactMarkComponent::OnStart()
    {
        RootTransform().SetScale(NS::Core::Vector3{m_diameter, 1.0f, m_diameter});
    }

    void ImpactMarkComponent::OnUpdate()
    {
        if (m_age >= m_lifeSeconds)
            return;
        m_age += NS::Core::FrameTimer::FixedDelta();
        float t = 1.0f;
        if (m_lifeSeconds > 0.0f)
            t = NS::Core::Clamp(m_age / m_lifeSeconds, 0.0f, 1.0f);
        const float size = m_diameter * (1.0f - t);
        RootTransform().SetScale(NS::Core::Vector3{size, 1.0f, size});
        if (t >= 1.0f)
        {
            // 消える時も配置物は残す。更新の最中に消すと World::UpdateObjects が集めた並びに解放済みの位置が残る
            if (auto* mesh = Owner()->FindComponent<NS::Object::MeshRendererComponent>())
                mesh->SetActive(false);
            SetActive(false);
        }
    }

    // TODO: 消えた跡の配置物は残り続ける。長く遊ぶと積み上がるので、帯の外で回収する仕組みが要る
    NS_CLASS(ImpactMarkComponent)
} // namespace NS::Game::Level
