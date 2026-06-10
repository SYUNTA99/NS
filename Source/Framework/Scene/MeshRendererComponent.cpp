#include "Framework/Scene/MeshRendererComponent.h"

#include "Framework/Graphics/Material.h"
#include "Framework/Graphics/Mesh.h"
#include "Framework/Scene/GameObject.h"
#include "Framework/Scene/RenderContext.h"
#include "Framework/Scene/SceneBase.h"
#include "Framework/Scene/Transform.h"

namespace NS::Scene
{
    MeshRendererComponent::MeshRendererComponent(NS::Graphics::Mesh* mesh, NS::Graphics::Material* material) noexcept
        : m_mesh(mesh), m_material(material)
    {}

    void MeshRendererComponent::OnStart()
    {
        GameObject* owner = Owner();
        if (owner == nullptr)
            return;
        SceneBase* scene = owner->OwningScene();
        if (scene == nullptr)
            return;
        scene->RegisterRenderable(this);
    }

    void MeshRendererComponent::OnEndPlay()
    {
        GameObject* owner = Owner();
        if (owner == nullptr)
            return;
        SceneBase* scene = owner->OwningScene();
        if (scene == nullptr)
            return;
        scene->UnregisterRenderable(this);
    }

    void MeshRendererComponent::Draw(const RenderContext& context)
    {
        GameObject* owner = Owner();
        if (!IsActive() || m_mesh == nullptr || m_material == nullptr || owner == nullptr ||
            context.renderer == nullptr)
            return;

        // 直接描画される mesh の InputLayout を初回描画時に生成 (冪等)。 instanced block は
        // ここを通らない (component が inactive で InstanceBatcher が自前 layout を持つ)
        m_material->CreateInputLayoutFor(*m_mesh);

        FrameCB cb{};
        cb.world = owner->Root().InterpolatedWorldMatrix(context.alpha);
        cb.viewProj = context.viewProjection;
        // lightDir は解決済設定を SetLightDirection 経由で受け取る前提
        cb.lightDir = m_lightDir;
        cb.lightDir.Normalize();
        cb.baseColor = m_baseColor;
        cb.lightColor = m_lightColor;
        cb.ambientColor = m_ambientColor;

        m_material->SetParams(*context.renderer, cb);
        m_material->Bind(*context.renderer);
        m_mesh->Draw(*context.renderer);
    }
} // namespace NS::Scene
