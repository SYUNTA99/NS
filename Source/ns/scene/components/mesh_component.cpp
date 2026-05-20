#include "ns/scene/components/mesh_component.h"

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

    void MeshComponent::Draw(const RenderContext& context)
    {
        if (!IsActive() || m_mesh == nullptr || m_material == nullptr)
            return;

        FrameCB cb{};
        cb.world = Owner()->Root().InterpolatedWorldMatrix(context.alpha);
        cb.viewProj = context.viewProjection;
        cb.lightDir = m_lightDir;
        cb.lightDir.Normalize();
        cb.baseColor = m_baseColor;

        m_material->SetParams(cb);
        m_material->Bind();
        m_mesh->Draw();
    }
} // namespace ns::scene
