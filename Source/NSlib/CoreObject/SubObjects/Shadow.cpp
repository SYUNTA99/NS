#include "NSlib/CoreObject/SubObjects/Shadow.h"
#include "NSlib/Core/AABB.h"

#include "NSlib/CoreObject/Asset/Material.h"
#include "NSlib/CoreObject/Asset/Mesh.h"
#include "NSlib/CoreObject/Scene/Scene.h"
#include "NSlib/CoreObject/SceneActor.h"
#include "NSlib/CoreObject/SubObjects/Transform.h"
#include "NSlib/Graphics/D3DMaterial.h"
#include "NSlib/Graphics/RenderContext.h"
#include "NSlib/Graphics/D3DMesh.h"

namespace NS
{
    Shadow::Shadow(SceneActor* owner) : m_owner(owner)
    {
        LoadAssetField(m_mesh, "shadowQuad");
        LoadAssetField(m_material, "shadow");
    }

    void Shadow::Register()
    {
        if (m_owner == nullptr)
        {
            return;
        }
        Scene* scene = m_owner->OwningScene();
        if (scene == nullptr)
        {
            return;
        }
        scene->RegisterRenderable(this);
    }

    void Shadow::Unregister()
    {
        if (m_owner == nullptr)
        {
            return;
        }
        Scene* scene = m_owner->OwningScene();
        if (scene == nullptr)
        {
            return;
        }
        scene->UnregisterRenderable(this);
    }

    Vector3 Shadow::SortCenter() const noexcept
    {
        if (m_owner == nullptr)
        {
            return {};
        }
        return m_owner->Root().WorldMatrix().Translation();
    }

    AABB Shadow::WorldBounds() const noexcept
    {
        if (m_owner == nullptr)
        {
            return {};
        }
        const Matrix world = m_owner->Root().WorldMatrix();
        const Vector3 origin = world.Translation();
        // 影は owner 直下 [-maxDrop, 0] のどこかに baseDiameter 幅で落ちる。その可動域を全部覆う
        const Vector3 center{origin.x, origin.y - m_maxDrop * 0.5f, origin.z};
        const Vector3 extents{m_baseDiameter, m_maxDrop * 0.5f, m_baseDiameter};
        return AABB{center, extents};
    }

    float Shadow::ComputeFade(float dist, float maxDist) noexcept
    {
        if (maxDist <= 0.0f)
        {
            return 0.0f;
        }
        return Clamp(1.0f - dist / maxDist, 0.0f, 1.0f);
    }

    void Shadow::Collect(const Gfx::RenderContext& context, std::vector<Gfx::DrawItem>& out)
    {
        if (m_owner == nullptr || !m_owner->IsAppeared() || m_mesh->D3D() == nullptr || m_material->D3D() == nullptr)
        {
            return;
        }
        const Matrix ownerWorld = m_owner->Root().InterpolatedWorldMatrix(context.alpha);
        const Vector3 origin = ownerWorld.Translation();

        float dist = 0.0f;
        if (!RaycastCollision(m_owner, origin, Vector3{0.0f, -1.0f, 0.0f}, m_maxDrop, dist))
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

        const Matrix world = Matrix::CreateScale(scale, 1.0f, scale) *
                                 Matrix::CreateTranslation(origin.x, groundY + m_surfaceOffset, origin.z);

        Gfx::DrawItem item{};
        item.mesh = m_mesh->D3D();
        item.material = m_material->D3D();
        item.blend = Gfx::BlendMode::Alpha; // 深度は読むだけで書かない。手前の物には隠れる
        item.constants.world = world;
        item.constants.viewProj = context.viewProjection;
        item.constants.baseColor = Vector3{alpha, 0.0f, 0.0f}; // x = 高さフェードアルファで shadow.ps が読む
        out.push_back(item);
    }
} // namespace NS
