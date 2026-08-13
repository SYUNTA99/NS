#include "Runtime/Object/Components/ShadowComponent.h"

#include "Runtime/Graphics/Material.h"
#include "Runtime/Graphics/RenderContext.h"
#include "Runtime/Graphics/StaticMesh.h"
#include "Runtime/Object/AssetManager.h"
#include "Runtime/Object/Components/BoxColliderComponent.h"
#include "Runtime/Object/Components/CapsuleColliderComponent.h"
#include "Runtime/Object/Components/SlopeColliderComponent.h"
#include "Runtime/Object/Components/SphereColliderComponent.h"
#include "Runtime/Object/GameObject.h"
#include "Runtime/Object/Reflection/TypeRegistry.h"
#include "Runtime/Object/Scene/Scene.h"
#include "Runtime/Object/Transform.h"
#include "Runtime/Object/World.h"

#include <optional>

namespace NS::Object
{
    void ShadowComponent::SetResources(NS::Graphics::StaticMesh* mesh, NS::Graphics::Material* material) noexcept
    {
        m_mesh = mesh;
        m_material = material;
    }

    void ShadowComponent::SetCollisionWorld(std::span<const NS::Core::AABB> world)
    {
        m_collisionWorld.assign(world.begin(), world.end());
    }

    namespace
    {
        // object が持つ最初の collider のワールド AABB。 影の地面探索は形の別を問わず箱で受ける
        std::optional<NS::Core::AABB> ColliderAABB(GameObject& obj) noexcept
        {
            if (auto* box = obj.FindComponent<BoxColliderComponent>())
                return box->WorldAABB();
            if (auto* sphere = obj.FindComponent<SphereColliderComponent>())
                return sphere->WorldAABB();
            if (auto* capsule = obj.FindComponent<CapsuleColliderComponent>())
                return capsule->WorldAABB();
            if (auto* slope = obj.FindComponent<SlopeColliderComponent>())
            {
                const auto tris = slope->WorldTriangles();
                NS::Core::Vector3 lo = tris[0].v0;
                NS::Core::Vector3 hi = tris[0].v0;
                for (const auto& t : tris)
                {
                    for (const NS::Core::Vector3& v : {t.v0, t.v1, t.v2})
                    {
                        lo = NS::Core::Vector3::Min(lo, v);
                        hi = NS::Core::Vector3::Max(hi, v);
                    }
                }
                return NS::Core::AABB{(lo + hi) * 0.5f, (hi - lo) * 0.5f};
            }
            return std::nullopt;
        }
    } // namespace

    void ShadowComponent::RefreshReceivers()
    {
        GameObject* owner = Owner();
        if (owner == nullptr || owner->OwningScene() == nullptr)
            return;
        World* world = &owner->OwningScene()->World();

        m_collisionWorld.clear();
        m_collisionWorld.reserve(world->ObjectCount());
        for (GameObject* obj : *world)
        {
            if (auto aabb = ColliderAABB(*obj))
                m_collisionWorld.push_back(*aabb);
        }
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
        // 組み直しのたび OnStart が呼び直されるので、 受け先はここで集めれば常に今の world と揃う
        RefreshReceivers();
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

    bool ShadowComponent::GroundBelow(const NS::Core::Vector3& origin,
                                      std::span<const NS::Core::AABB> world,
                                      float maxDist,
                                      float& outDist) noexcept
    {
        const NS::Core::Ray ray(origin, NS::Core::Vector3{0.0f, -1.0f, 0.0f});
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

    void ShadowComponent::Collect(const NS::Graphics::RenderContext& context, std::vector<NS::Graphics::DrawItem>& out)
    {
        const GameObject* owner = Owner();
        if (!IsActive() || m_mesh == nullptr || m_material == nullptr || owner == nullptr || m_collisionWorld.empty())
            return;

        const NS::Core::Matrix ownerWorld = owner->Root().InterpolatedWorldMatrix(context.alpha);
        const NS::Core::Vector3 origin{ownerWorld._41, ownerWorld._42, ownerWorld._43};

        // 真下の地面探索とフェード算出
        float dist = 0.0f;
        if (!GroundBelow(origin, m_collisionWorld, m_maxDrop, dist))
            return; // 真下に地面が無い奈落上なら描かない

        const float alpha = ComputeFade(dist, m_maxDrop) * m_baseAlpha;
        if (alpha <= 0.0f)
            return;

        const float fade = ComputeFade(dist, m_maxDrop);
        const float scale = m_baseDiameter * (0.6f + 0.4f * fade); // 高いほど小さく
        const float groundY = origin.y - dist;

        // 影クアッドの world 行列
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
