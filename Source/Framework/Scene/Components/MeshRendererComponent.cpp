#include "Framework/Scene/Components/MeshRendererComponent.h"

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

        // InputLayout を初回描画時に生成 (冪等)。instanced block は inactive で InstanceBatcher が担う
        m_material->CreateInputLayoutFor(*m_mesh);

        // ctx.resolvedSettings は project 既定 ← scene override まで解決済。ここに個体段 override を載せる
        const NS::Graphics::RenderSettings finalSettings =
            NS::Graphics::Resolve(context.resolvedSettings, m_objectOverride);

        FrameCB cb{};
        cb.world = owner->Root().InterpolatedWorldMatrix(context.alpha);
        cb.viewProj = context.viewProjection;
        cb.lightDir = finalSettings.lightDir;
        cb.lightDir.Normalize();
        cb.baseColor = m_baseColor; // 個体色は lighting と別系統
        cb.lightColor = finalSettings.lightColor;
        cb.ambientColor = finalSettings.ambientColor;

        m_material->SetParams(*context.renderer, cb);
        m_material->Bind(*context.renderer);
        m_mesh->Draw(*context.renderer);
    }
} // namespace NS::Scene
