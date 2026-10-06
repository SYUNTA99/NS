#include "NSlib/Object/Components/Shadow.h"
#include "NSlib/Core/AABB.h"

#include "NSlib/Graphics/Material.h"
#include "NSlib/Graphics/RenderContext.h"
#include "NSlib/Graphics/StaticMesh.h"
#include "NSlib/Object/Actor.h"
#include "NSlib/Object/AssetManager.h"
#include "NSlib/Object/Reflection/TypeRegistry.h"
#include "NSlib/Object/Scene/Scene.h"
#include "NSlib/Object/Transform.h"

namespace NS::Obj
{
    void Shadow::SetResources(NS::Gfx::StaticMesh* mesh, NS::Gfx::Material* material) noexcept
    {
        m_mesh = mesh;
        m_material = material;
    }

    void Shadow::ResolveAssets(AssetManager& assets)
    {
        SetResources(assets.Builtin("shadowQuad"), assets.SharedMaterial("shadow"));
    }

    void Shadow::OnStart()
    {
        Scene* scene = OwningScene();
        if (scene == nullptr)
        {
            return;
        }
        scene->RegisterRenderable(this);
    }

    void Shadow::OnEndPlay()
    {
        Scene* scene = OwningScene();
        if (scene == nullptr)
        {
            return;
        }
        scene->UnregisterRenderable(this);
    }

    NS::Vector3 Shadow::SortCenter() const noexcept
    {
        const Actor* owner = Owner();
        if (owner == nullptr)
        {
            return {};
        }
        const NS::Matrix m = owner->Root().WorldMatrix();
        return NS::Vector3{m._41, m._42, m._43};
    }

    NS::AABB Shadow::WorldBounds() const noexcept
    {
        const Actor* owner = Owner();
        if (owner == nullptr)
        {
            return {};
        }
        const NS::Matrix world = owner->Root().WorldMatrix();
        const NS::Vector3 origin{world._41, world._42, world._43};
        // 影は owner 直下 [-maxDrop, 0] のどこかに baseDiameter 幅で落ちる。その可動域を全部覆う
        const NS::Vector3 center{origin.x, origin.y - m_maxDrop * 0.5f, origin.z};
        const NS::Vector3 extents{m_baseDiameter, m_maxDrop * 0.5f, m_baseDiameter};
        return NS::AABB{center, extents};
    }

    float Shadow::ComputeFade(float dist, float maxDist) noexcept
    {
        if (maxDist <= 0.0f)
        {
            return 0.0f;
        }
        const float f = 1.0f - dist / maxDist;
        if (f < 0.0f)
        {
            return 0.0f;
        }
        if (f > 1.0f)
        {
            return 1.0f;
        }
        return f;
    }

    void Shadow::Collect(const NS::Gfx::RenderContext& context, std::vector<NS::Gfx::DrawItem>& out)
    {
        Actor* owner = Owner();
        if (!IsActive() || m_mesh == nullptr || m_material == nullptr || owner == nullptr)
        {
            return;
        }
        const NS::Matrix ownerWorld = owner->Root().InterpolatedWorldMatrix(context.alpha);
        const NS::Vector3 origin{ownerWorld._41, ownerWorld._42, ownerWorld._43};

        float dist = 0.0f;
        if (!RaycastCollision(*owner, origin, NS::Vector3{0.0f, -1.0f, 0.0f}, m_maxDrop, dist))
        {
            return; // 真下 maxDrop 以内に地面が無ければ描かない
        }

        const float fade = ComputeFade(dist, m_maxDrop);
        const float alpha = fade * m_baseAlpha;
        if (alpha <= 0.0f)
        {
            return;
        }

        const float scale = m_baseDiameter * (0.6f + 0.4f * fade); // 高いほど小さく
        const float groundY = origin.y - dist;

        const NS::Matrix world = NS::Matrix::CreateScale(scale, 1.0f, scale) *
                                 NS::Matrix::CreateTranslation(origin.x, groundY + m_surfaceOffset, origin.z);

        NS::Gfx::DrawItem item{};
        item.mesh = m_mesh;
        item.material = m_material;
        item.blend = NS::Gfx::BlendMode::Alpha; // 深度は読むだけで書かない。手前の物には隠れる
        item.constants.world = world;
        item.constants.viewProj = context.viewProjection;
        item.constants.baseColor = NS::Vector3{alpha, 0.0f, 0.0f}; // x = 高さフェードアルファで shadow.ps が読む
        out.push_back(item);
    }

    NS_CLASS(Shadow)
} // namespace NS::Obj
