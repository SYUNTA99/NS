#include "NSlib/CoreObject/ClassObjs/Shadow.h"
#include "NSlib/Core/AABB.h"

#include "NSlib/CoreObject/Asset/Material.h"
#include "NSlib/CoreObject/Asset/Mesh.h"
#include "NSlib/Core/Assert.h"
#include "NSlib/CoreObject/IGet/IGetCollision.h"
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

    Gfx::RenderBounds Shadow::Bounds(const Matrix& root) const noexcept
    {
        const Vector3 origin = root.Translation();
        // 影は根の直下 [-maxDrop, 0] のどこかに baseDiameter 幅で落ちる。その可動域を全部覆う
        const Vector3 center{origin.x, origin.y - m_maxDrop * 0.5f, origin.z};
        const Vector3 extents{m_baseDiameter, m_maxDrop * 0.5f, m_baseDiameter};
        return Gfx::RenderBounds{
            .bounds = AABB{center, extents}, .sortCenter = origin, .pass = Gfx::RenderPass::Transparent};
    }

    float Shadow::ComputeFade(float dist, float maxDist) noexcept
    {
        if (maxDist <= 0.0f)
        {
            return 0.0f;
        }
        return Clamp(1.0f - dist / maxDist, 0.0f, 1.0f);
    }

    void Shadow::Update(const IGetCollision* collision, const Vector3& origin) noexcept
    {
        NS_ASSERT(Scene, collision != nullptr, "Shadow::Update: 地面を探すシーンが null");
        float dist = 0.0f;
        m_hasGround = RaycastCollision(collision, origin, Vector3{0.0f, -1.0f, 0.0f}, m_maxDrop, dist);
        m_groundY = 0.0f;
        if (m_hasGround)
        {
            m_groundY = origin.y - dist;
        }
    }

    std::optional<Shadow::Plane> Shadow::PlaneAt(const Vector3& origin) const noexcept
    {
        if (!m_hasGround) // 真下 maxDrop 以内に地面が無ければ描かない
        {
            return std::nullopt;
        }
        const float fade = ComputeFade(origin.y - m_groundY, m_maxDrop);
        const float opacity = fade * m_baseAlpha;
        if (opacity <= 0.0f)
        {
            return std::nullopt;
        }
        const float scale = m_baseDiameter * (0.6f + 0.4f * fade); // 高いほど小さく
        return Plane{.world = Matrix::CreateScale(scale, 1.0f, scale) *
                              Matrix::CreateTranslation(origin.x, m_groundY + m_surfaceOffset, origin.z),
                     .opacity = opacity};
    }

    std::optional<Matrix> Shadow::DrawWorldMatrix(const Vector3& origin) const noexcept
    {
        const std::optional<Plane> plane = PlaneAt(origin);
        if (!plane.has_value())
        {
            return std::nullopt;
        }
        return plane->world;
    }

    void Shadow::Collect(const Gfx::RenderContext& context, const Matrix& root, std::vector<Gfx::DrawItem>& out) const
    {
        if (m_mesh->D3D() == nullptr || m_material->D3D() == nullptr)
        {
            return;
        }
        const std::optional<Plane> plane = PlaneAt(root.Translation());
        if (!plane.has_value())
        {
            return;
        }

        Gfx::DrawItem item{};
        item.mesh = m_mesh->D3D();
        item.material = m_material->D3D();
        item.blend = Gfx::BlendMode::Alpha; // 深度は読むだけで書かない。手前の物には隠れる
        item.constants.world = plane->world;
        item.constants.viewProj = context.viewProjection;
        item.constants.baseColor = m_material->BaseColor().ToVector3();
        item.constants.opacity = plane->opacity;
        out.push_back(item);
    }
} // namespace NS
