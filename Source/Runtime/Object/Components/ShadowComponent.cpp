#include "Runtime/Object/Components/ShadowComponent.h"

#include "Runtime/Graphics/Material.h"
#include "Runtime/Graphics/RenderContext.h"
#include "Runtime/Graphics/StaticMesh.h"
#include "Runtime/Object/AssetManager.h"
#include "Runtime/Object/GameObject.h"
#include "Runtime/Object/Reflection/TypeRegistry.h"
#include "Runtime/Object/Scene/Scene.h"
#include "Runtime/Object/Transform.h"
#include "Runtime/Physics/PhysicsWorld.h"

namespace NS::Object
{
    void ShadowComponent::SetResources(NS::Graphics::StaticMesh* mesh, NS::Graphics::Material* material) noexcept
    {
        m_mesh = mesh;
        m_material = material;
    }

    void ShadowComponent::ResolveAssets(AssetManager& assets)
    {
        SetResources(assets.Builtin("shadowQuad"), assets.SharedMaterial("shadow"));
    }

    void ShadowComponent::OnStart()
    {
        GameObject* owner = Owner();
        if (owner == nullptr)
            return;
        Scene* scene = owner->OwningScene();
        if (scene == nullptr)
            return;
        scene->RegisterRenderable(this);
    }

    void ShadowComponent::OnEndPlay()
    {
        GameObject* owner = Owner();
        if (owner == nullptr)
            return;
        Scene* scene = owner->OwningScene();
        if (scene == nullptr)
            return;
        scene->UnregisterRenderable(this);
    }

    NS::Core::Vector3 ShadowComponent::SortCenter() const noexcept
    {
        const GameObject* owner = Owner();
        if (owner == nullptr)
            return {};
        const NS::Core::Matrix m = owner->Root().WorldMatrix();
        return NS::Core::Vector3{m._41, m._42, m._43};
    }

    NS::Core::AABB ShadowComponent::WorldBounds() const noexcept
    {
        const GameObject* owner = Owner();
        if (owner == nullptr)
            return {};
        const NS::Core::Matrix world = owner->Root().WorldMatrix();
        const NS::Core::Vector3 origin{world._41, world._42, world._43};
        // 影は owner 直下 [-maxDrop, 0] のどこかに baseDiameter 幅で落ちる。その可動域を全部覆う
        const NS::Core::Vector3 center{origin.x, origin.y - m_maxDrop * 0.5f, origin.z};
        const NS::Core::Vector3 extents{m_baseDiameter, m_maxDrop * 0.5f, m_baseDiameter};
        return NS::Core::AABB{center, extents};
    }

    float ShadowComponent::ComputeFade(float dist, float maxDist) noexcept
    {
        if (maxDist <= 0.0f)
            return 0.0f;
        const float f = 1.0f - dist / maxDist;
        if (f < 0.0f)
            return 0.0f;
        if (f > 1.0f)
            return 1.0f;
        return f;
    }

    void ShadowComponent::Collect(const NS::Graphics::RenderContext& context, std::vector<NS::Graphics::DrawItem>& out)
    {
        GameObject* owner = Owner();
        if (!IsActive() || m_mesh == nullptr || m_material == nullptr || owner == nullptr)
            return;
        Scene* scene = owner->OwningScene();
        if (scene == nullptr)
            return;

        const NS::Core::Matrix ownerWorld = owner->Root().InterpolatedWorldMatrix(context.alpha);
        const NS::Core::Vector3 origin{ownerWorld._41, ownerWorld._42, ownerWorld._43};

        float dist = 0.0f;
        if (!scene->Physics().RaycastDown(origin, m_maxDrop, dist))
            return; // 真下に地面が無い奈落上なら描かない

        const float fade = ComputeFade(dist, m_maxDrop);
        const float alpha = fade * m_baseAlpha;
        if (alpha <= 0.0f)
            return;

        const float scale = m_baseDiameter * (0.6f + 0.4f * fade); // 高いほど小さく
        const float groundY = origin.y - dist;

        const NS::Core::Matrix world =
            NS::Core::Matrix::CreateScale(scale, 1.0f, scale) *
            NS::Core::Matrix::CreateTranslation(origin.x, groundY + m_surfaceOffset, origin.z);

        NS::Graphics::DrawItem item{};
        item.mesh = m_mesh;
        item.material = m_material;
        item.blend = NS::Graphics::BlendMode::Alpha; // 深度書込OFF の半透明として手前に遮蔽される
        item.constants.world = world;
        item.constants.viewProj = context.viewProjection;
        item.constants.baseColor = NS::Core::Vector3{alpha, 0.0f, 0.0f}; // x = 高さフェードアルファで shadow.ps が読む
        out.push_back(item);
    }

    NS_CLASS(ShadowComponent)
} // namespace NS::Object
