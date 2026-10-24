#include "NSlib/CoreObject/ClassObjs/Shadow.h"
#include "NSlib/Core/AABB.h"

#include "NSlib/CoreObject/Asset/Material.h"
#include "NSlib/CoreObject/Asset/Mesh.h"
#include "NSlib/Core/Assert.h"
#include "NSlib/CoreObject/IUse/IUseCollision.h"
#include "NSlib/Gfx/D3DMaterial.h"
#include "NSlib/Gfx/D3DMesh.h"
#include "NSlib/Gfx/Render/RenderContext.h"

namespace NS
{
    Shadow::Shadow()
    {
        LoadAssetField(m_mesh, "shadowQuad");
        LoadAssetField(m_material, "Assets/Materials/shadow.mat");
    }

    Vector3 Shadow::SortCenter(const RenderSource& source) const noexcept
    {
        return source.root.Translation();
    }

    AABB Shadow::WorldBounds(const RenderSource& source) const noexcept
    {
        const Vector3 origin = source.root.Translation();
        // 影は根の直下 [-maxDrop, 0] のどこかに baseDiameter 幅で落ちる。その可動域を全部覆う
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

    void Shadow::Collect(const Gfx::RenderContext& context,
                         const RenderSource& source,
                         std::vector<Gfx::DrawItem>& out)
    {
        NS_ASSERT(Graphics, m_collision != nullptr, "Shadow::Collect: 地面を探すシーンを InitDraw で受けていない");
        if (m_collision == nullptr || m_mesh->D3D() == nullptr || m_material->D3D() == nullptr)
        {
            return;
        }
        const Vector3 origin = source.root.Translation();

        float dist = 0.0f;
        if (!RaycastCollision(m_collision, origin, Vector3{0.0f, -1.0f, 0.0f}, m_maxDrop, dist))
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
