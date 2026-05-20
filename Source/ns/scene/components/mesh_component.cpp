#include "ns/scene/components/mesh_component.h"

#include "ns/app/scene.h"
#include "ns/graphics/material.h"
#include "ns/graphics/mesh.h"
#include "ns/scene/game_object.h"
#include "ns/scene/render_context.h"
#include "ns/scene/transform.h"

namespace ns::scene
{
    MeshComponent::MeshComponent(ns::graphics::Mesh* mesh, ns::graphics::Material* material) noexcept
        : m_mesh(mesh), m_material(material)
    {}

    void MeshComponent::OnStart()
    {
        GameObject* owner = Owner();
        if (owner == nullptr)
            return;
        ns::app::Scene* scene = owner->OwningScene();
        if (scene == nullptr)
            return;
        scene->RegisterRenderable(this);
    }

    void MeshComponent::OnEndPlay()
    {
        GameObject* owner = Owner();
        if (owner == nullptr)
            return;
        ns::app::Scene* scene = owner->OwningScene();
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
            cb.lightDir = ns::core::Vector3{-0.3f, -1.0f, -0.2f};
        cb.lightDir.Normalize();
        cb.baseColor = m_baseColor;

        m_material->SetParams(cb);
        m_material->Bind();
        m_mesh->Draw();
    }
} // namespace ns::scene
