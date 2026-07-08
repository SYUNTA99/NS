#include "Framework/Scene/Components/ShadowComponent.h"

#include "Framework/Graphics/CommandList.h"
#include "Framework/Graphics/Material.h"
#include "Framework/Graphics/Renderer.h"
#include "Framework/Graphics/StaticMesh.h"
#include "Framework/Scene/ComponentRegistry.h"
#include "Framework/Scene/Components/MeshRendererComponent.h" // FrameCB レイアウト共有で standard.vs と一致
#include "Framework/Scene/GameObject.h"
#include "Framework/Scene/RenderContext.h"
#include "Framework/Scene/SceneBase.h"
#include "Framework/Scene/Transform.h"

namespace NS::Scene
{
    void ShadowComponent::SetResources(NS::Graphics::StaticMesh* mesh, NS::Graphics::Material* material) noexcept
    {
        m_mesh = mesh;
        m_material = material;
    }

    void ShadowComponent::SetCollisionWorld(std::span<const NS::Math::AABB> world)
    {
        m_collisionWorld.assign(world.begin(), world.end());
    }

    void ShadowComponent::OnStart()
    {
        GameObject* owner = Owner();
        if (owner == nullptr)
            return;
        SceneBase* scene = owner->OwningScene();
        if (scene == nullptr)
            return;
        scene->RegisterRenderable(this);
    }

    void ShadowComponent::OnEndPlay()
    {
        GameObject* owner = Owner();
        if (owner == nullptr)
            return;
        SceneBase* scene = owner->OwningScene();
        if (scene == nullptr)
            return;
        scene->UnregisterRenderable(this);
    }

    NS::Math::Vector3 ShadowComponent::SortCenter() const noexcept
    {
        const GameObject* owner = Owner();
        if (owner == nullptr)
            return {};
        const NS::Math::Matrix m = owner->Root().WorldMatrix();
        return NS::Math::Vector3{m._41, m._42, m._43};
    }

    bool ShadowComponent::GroundBelow(const NS::Math::Vector3& origin,
                                      std::span<const NS::Math::AABB> world,
                                      float maxDist,
                                      float& outDist) noexcept
    {
        const NS::Math::Ray ray(origin, NS::Math::Vector3{0.0f, -1.0f, 0.0f});
        float nearest = maxDist;
        bool hit = false;
        for (const auto& box : world)
        {
            float t = 0.0f;
            if (ray.Intersects(box, t) && t >= 0.0f && t <= nearest)
            {
                nearest = t;
                hit = true;
            }
        }
        if (hit)
            outDist = nearest;
        return hit;
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

    void ShadowComponent::Draw(const RenderContext& context)
    {
        const GameObject* owner = Owner();
        if (!IsActive() || m_mesh == nullptr || m_material == nullptr || owner == nullptr ||
            context.renderer == nullptr || m_collisionWorld.empty())
            return;

        const NS::Math::Matrix ownerWorld = owner->Root().InterpolatedWorldMatrix(context.alpha);
        const NS::Math::Vector3 origin{ownerWorld._41, ownerWorld._42, ownerWorld._43};

        float dist = 0.0f;
        if (!GroundBelow(origin, m_collisionWorld, m_maxDrop, dist))
            return; // 真下に地面が無い奈落上なら描かない

        const float alpha = ComputeFade(dist, m_maxDrop) * m_baseAlpha;
        if (alpha <= 0.0f)
            return;

        const float fade = ComputeFade(dist, m_maxDrop);
        const float scale = m_baseDiameter * (0.6f + 0.4f * fade); // 高いほど小さく
        const float groundY = origin.y - dist;

        const NS::Math::Matrix world =
            NS::Math::Matrix::CreateScale(scale, 1.0f, scale) *
            NS::Math::Matrix::CreateTranslation(origin.x, groundY + m_surfaceOffset, origin.z);

        // 描画する者が自分の Pipeline を set する不変条件。Alpha 合成で深度書込OFF の半透明として手前に遮蔽される
        context.renderer->Commands().SetPipeline(context.renderer->CommonPipeline(NS::Graphics::BlendMode::Alpha));

        m_material->CreateInputLayoutFor(*m_mesh);

        FrameCB cb{};
        cb.world = world;
        cb.viewProj = context.viewProjection;
        cb.baseColor = NS::Math::Vector3{alpha, 0.0f, 0.0f}; // x = 高さフェードアルファで shadow.ps が読む

        m_material->SetParams(*context.renderer, cb);
        m_material->Bind(*context.renderer);
        m_mesh->Draw(*context.renderer);
    }

    // 共有 quad / material は scene が SetResources で注入する。未注入の間 Draw は何もしない
    NS_REGISTER_COMPONENT(ShadowComponent)
} // namespace NS::Scene
