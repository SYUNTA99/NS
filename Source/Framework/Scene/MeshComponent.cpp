#include "Framework/Scene/MeshComponent.h"

#include "Framework/Graphics/Material.h"
#include "Framework/Graphics/Mesh.h"
#include "Framework/Scene/GameObject.h"
#include "Framework/Scene/RenderContext.h"
#include "Framework/Scene/SceneBase.h"
#include "Framework/Scene/Transform.h"

namespace NS::Scene
{
    MeshComponent::MeshComponent(NS::Scene::GameObject* owner,
                                 NS::Graphics::Mesh* mesh,
                                 NS::Graphics::Material* material) noexcept
        : Component(owner), m_mesh(mesh), m_material(material)
    {}

    void MeshComponent::OnStart()
    {
        GameObject* owner = Owner();
        if (owner == nullptr)
            return;
        SceneBase* scene = owner->OwningScene();
        if (scene == nullptr)
            return;
        scene->RegisterRenderable(this);
    }

    void MeshComponent::OnEndPlay()
    {
        GameObject* owner = Owner();
        if (owner == nullptr)
            return;
        SceneBase* scene = owner->OwningScene();
        if (scene == nullptr)
            return;
        scene->UnregisterRenderable(this);
    }

    void MeshComponent::Draw(const RenderContext& context)
    {
        GameObject* owner = Owner();
        if (!IsActive() || m_mesh == nullptr || m_material == nullptr || owner == nullptr)
            return;

        FrameCB cb{};
        cb.world = owner->Root().InterpolatedWorldMatrix(context.alpha);
        cb.viewProj = context.viewProjection;
        cb.lightDir = m_lightDir;
        if (cb.lightDir.LengthSquared() <= 1e-6f)
            cb.lightDir = NS::Math::Vector3{-0.3f, -1.0f, -0.2f};
        cb.lightDir.Normalize();
        cb.baseColor = m_baseColor;
        cb.lightColor = m_lightColor;
        cb.ambientColor = m_ambientColor;

        m_material->SetParams(cb);
        m_material->Bind();
        m_mesh->Draw();
    }
} // namespace NS::Scene
