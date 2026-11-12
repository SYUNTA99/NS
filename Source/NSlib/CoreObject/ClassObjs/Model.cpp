#include "NSlib/CoreObject/ClassObjs/Model.h"
#include "NSlib/Core/AABB.h"
#include "NSlib/Core/Types.h"

#include "NSlib/CoreObject/Asset/Material.h"
#include "NSlib/CoreObject/Asset/Mesh.h"
#include "NSlib/Core/Assert.h"
#include "NSlib/Gfx/D3DMaterial.h"
#include "NSlib/Gfx/D3DMesh.h"
#include "NSlib/Gfx/Render/RenderContext.h"
#include <algorithm>
#include <cmath>

namespace
{
    // 描く時だけの倍率の既定。この値の間は描く行列にも境界にも何も掛けない
    const NS::Vector3 k_NoDrawScale{1.0f, 1.0f, 1.0f};

    // SetNeverCulled の後に返す境界の半分の長さ。どのコースよりも十分に広い
    constexpr float k_NeverCulledExtent = 1000000.0f;

    // 0 以下と非数・無限大の成分を弾き、全部が有限の正の時だけ真を返す
    [[nodiscard]] bool IsPositiveFiniteScale(const NS::Vector3& scale) noexcept
    {
        return NS::IsPositiveFinite(scale.x) && NS::IsPositiveFinite(scale.y) && NS::IsPositiveFinite(scale.z);
    }

    // 形の原点で局所の倍率を掛けてから matrix を掛ける。倍率が 1 の間は、掛け算が下の桁を丸めないように掛けない
    [[nodiscard]] NS::Matrix WithLocalScale(const NS::Vector3& localScale, const NS::Matrix& matrix) noexcept
    {
        if (localScale == k_NoDrawScale)
        {
            return matrix;
        }
        return NS::Matrix::CreateScale(localScale) * matrix;
    }
} // namespace

namespace NS
{
    Model::Model()
    {
        LoadAssetField(m_mesh, "");
        LoadAssetField(m_material, "");
    }

    void Model::SetMeshRef(std::string_view ref)
    {
        LoadAssetField(m_mesh, ref);
    }

    void Model::SetMaterialRef(std::string_view ref)
    {
        LoadAssetField(m_material, ref);
    }

    const Mesh* Model::GetMesh() const noexcept
    {
        if (m_shownMesh != nullptr)
        {
            return m_shownMesh;
        }
        return m_mesh;
    }

    void Model::SetPerObjectVsConstant(const Gfx::D3DBuffer* cb,
                                       const void* cpuData,
                                       std::size_t cpuDataSize,
                                       uint slot) noexcept
    {
        m_perObjectVsCb = cb;
        m_perObjectVsData = cpuData;
        m_perObjectVsSize = cpuDataSize;
        m_perObjectVsSlot = slot;
    }

    void Model::Snapshot() noexcept
    {
        m_localRotation.Snapshot();
        m_shownDrawScale.Snapshot();
    }

    bool Model::SetDrawScale(const Vector3& scale) noexcept
    {
        if (!IsPositiveFiniteScale(scale))
        {
            return false;
        }
        m_drawScale = scale;
        m_shownDrawScale.Set(m_drawScale * m_drawShapeScale);
        return true;
    }

    bool Model::SnapDrawScale(const Vector3& scale) noexcept
    {
        if (!SetDrawScale(scale))
        {
            return false;
        }
        m_shownDrawScale.Teleport(GetDrawScale());
        return true;
    }

    bool Model::SetDrawOffset(const Vector3& offset) noexcept
    {
        if (!IsFinite(offset))
        {
            return false;
        }
        m_drawOffset = offset;
        return true;
    }

    bool Model::SetDrawShapeScale(const Vector3& scale) noexcept
    {
        if (!IsPositiveFiniteScale(scale))
        {
            return false;
        }
        m_drawShapeScale = scale;
        m_shownDrawScale.Set(m_drawScale * m_drawShapeScale);
        return true;
    }

    bool Model::SetLocalScale(const Vector3& scale) noexcept
    {
        if (!IsPositiveFiniteScale(scale))
        {
            return false;
        }
        m_localScale = scale;
        return true;
    }

    const AABB* Model::DrawnLocalBounds() const noexcept
    {
        if (m_hasLocalBoundsOverride)
        {
            return &m_localBoundsOverride;
        }
        return GetMesh()->LocalBounds();
    }

    Matrix Model::DrawWorldMatrix(const Matrix& root, float alpha) const noexcept
    {
        return DrawWorldMatrixWithOffset(root, alpha, m_drawOffset);
    }

    Matrix Model::DrawWorldMatrixWithOffset(const Matrix& root, float alpha, const Vector3& offset) const noexcept
    {
        const Quaternion local = m_localRotation.Lerp(alpha);
        const Matrix localMatrix = WithLocalScale(m_localScale, Matrix::CreateFromQuaternion(local));
        // 回転を先に掛ける。根のスケールは根の軸に残り、局所の回転と一緒に回らない
        Matrix drawn = localMatrix * root;
        // ずれは倍率の後に世界で足す。倍率の中心 (下端の真ん中) は根から測るので、ずれと一緒に動いて形を変えない
        // ずれが 0 の間は掛けない。0 の平行移動でも掛け算が下の桁を丸めることがある
        Matrix shift = Matrix::Identity;
        const bool shifted = offset.x != 0.0f || offset.y != 0.0f || offset.z != 0.0f;
        if (shifted)
        {
            shift = Matrix::CreateTranslation(offset);
        }
        const Vector3 scale = m_shownDrawScale.Lerp(alpha);
        // 倍率が 1 でも、下端の真ん中へ移して戻す足し引きが下の桁を丸めることがある。倍率の無い間は掛けずに返す
        if (scale == k_NoDrawScale)
        {
            if (shifted)
            {
                drawn = drawn * shift;
            }
            return drawn;
        }

        // 中心は描く形の下端の真ん中。局所の回転は含めない。回した箱を包む箱は玉の下端より下へ出るので、
        // 中心が床より下に来て、潰すと玉が床へめり込む
        Vector3 pivot = root.Translation();
        if (const AABB* localBounds = DrawnLocalBounds())
        {
            AABB worldBounds{};
            localBounds->Transform(worldBounds, WithLocalScale(m_localScale, root));
            pivot =
                Vector3{worldBounds.Center.x, worldBounds.Center.y - worldBounds.Extents.y, worldBounds.Center.z};
        }
        // 倍率は世界の軸で掛ける。局所の回転と根の回転に依らず世界の縦に潰れる
        Matrix scaled = drawn * Matrix::CreateTranslation(-pivot) * Matrix::CreateScale(scale) *
                            Matrix::CreateTranslation(pivot);
        if (shifted)
        {
            scaled = scaled * shift;
        }
        return scaled;
    }

    void Model::Collect(const Gfx::RenderContext& context, const Matrix& root, std::vector<Gfx::DrawItem>& out) const
    {
        if (!CanDraw())
        {
            return;
        }

        out.push_back(MakeDrawItem(DrawWorldMatrix(root, context.alpha)));
    }

    void Model::SetCopies(std::span<const ModelDraw> copies, int priority)
    {
        m_copies.assign(copies.begin(), copies.end());
        m_copyPriority = priority;
    }

    Gfx::RenderBounds Model::CopiesBounds(const Matrix& root) const noexcept
    {
        NS_ASSERT(Graphics, DrawsCopies(), "Model::CopiesBounds: 写しを描かない Model で呼んでいる");
        return Gfx::RenderBounds{.bounds = DrawsWorldBounds(root, m_copies),
                                 .sortCenter = root.Translation(),
                                 .sortPriority = m_copyPriority,
                                 .pass = Gfx::RenderPass::Transparent};
    }

    void Model::CollectCopies(const Gfx::RenderContext& context,
                              const Matrix& root,
                              std::vector<Gfx::DrawItem>& out) const
    {
        NS_ASSERT(Graphics, DrawsCopies(), "Model::CollectCopies: 写しを描かない Model で呼んでいる");
        CollectDraws(context, root, m_copies, out);
    }

    Color Model::DrawColor() const noexcept
    {
        if (m_colorOverride)
        {
            return *m_colorOverride;
        }
        return m_material->DrawColor();
    }

    void Model::CollectDraws(const Gfx::RenderContext& context,
                             const Matrix& root,
                             std::span<const ModelDraw> copies,
                             std::vector<Gfx::DrawItem>& out) const
    {
        if (!CanDraw())
        {
            return;
        }
        for (const ModelDraw& draw : copies)
        {
            Matrix world = Matrix::Identity;
            if (draw.world.has_value())
            {
                world = *draw.world;
            }
            else if (draw.offset.has_value())
            {
                world = DrawWorldMatrixWithOffset(root, context.alpha, *draw.offset);
            }
            else
            {
                world = DrawWorldMatrix(root, context.alpha);
            }
            Gfx::DrawItem item = MakeDrawItem(world);
            item.blend = Gfx::BlendMode::Alpha;
            item.constants.opacity = draw.opacity;
            if (!draw.sceneValues)
            {
                item.constants.sceneValuesWeight = 0.0f;
            }
            if (!draw.objectValues)
            {
                item.constants.objectValues = {};
            }
            out.push_back(item);
        }
    }

    AABB Model::DrawsWorldBounds(const Matrix& root, std::span<const ModelDraw> copies) const noexcept
    {
        const AABB base = WorldBounds(root);
        AABB out = base;
        for (const ModelDraw& draw : copies)
        {
            AABB bounds = base;
            if (draw.world.has_value())
            {
                const AABB* local = DrawnLocalBounds();
                if (local == nullptr)
                {
                    continue;
                }
                local->Transform(bounds, *draw.world);
            }
            else if (draw.offset.has_value())
            {
                const Vector3& offset = *draw.offset;
                const Vector3& sourceOffset = GetDrawOffset();
                bounds.Extents.x += std::abs(offset.x) + std::abs(sourceOffset.x);
                bounds.Extents.y += std::abs(offset.y) + std::abs(sourceOffset.y);
                bounds.Extents.z += std::abs(offset.z) + std::abs(sourceOffset.z);
            }
            AABB merged{};
            AABB::CreateMerged(merged, out, bounds);
            out = merged;
        }
        return out;
    }

    bool Model::CanDraw() const noexcept { return GetMesh()->D3D() != nullptr && m_material->D3D() != nullptr; }

    Gfx::DrawItem Model::MakeDrawItem(const Matrix& world) const noexcept
    {
        // 光・カメラ・シーンの値はビューごとの定数で SceneRenderer が送るので、ここでは物の値だけを書く
        Gfx::DrawItem item{};
        item.mesh = GetMesh()->D3D();
        item.material = m_material->D3D();
        item.blend = item.material->Blend();
        item.constants.world = world;
        item.constants.baseColor = DrawColor().ToVector3();
        item.constants.objectValues = m_drawValues;
        item.extraVsCb = m_perObjectVsCb;
        item.extraVsData = m_perObjectVsData;
        item.extraVsSize = m_perObjectVsSize;
        item.extraVsSlot = m_perObjectVsSlot;
        return item;
    }

    Gfx::RenderBounds Model::Bounds(const Matrix& root) const noexcept
    {
        Gfx::RenderBounds out{.bounds = WorldBounds(root), .sortCenter = root.Translation()};
        const Gfx::D3DMaterial* material = m_material->D3D();
        if (material != nullptr)
        {
            out.sortPriority = material->RenderPriority();
        }
        if (m_drawsAfterSky)
        {
            out.pass = Gfx::RenderPass::AfterSky;
        }
        else if (material != nullptr && material->Blend() != Gfx::BlendMode::Opaque)
        {
            out.pass = Gfx::RenderPass::Transparent;
        }
        return out;
    }

    AABB Model::WorldBounds(const Matrix& root) const noexcept
    {
        if (m_neverCulled)
        {
            AABB everywhere{};
            everywhere.Extents = {k_NeverCulledExtent, k_NeverCulledExtent, k_NeverCulledExtent};
            return everywhere;
        }
        if (GetMesh()->D3D() == nullptr)
        {
            return {}; // 描くものが無い。間引かれても Collect が何も積まず結果は変わらない
        }
        AABB out{};
        // m_mesh は確認済みで、描画の境界を取得できる
        DrawnLocalBounds()->Transform(out, WithLocalScale(m_localScale, root));

        // 描く時は前と今の倍率の間を補間するので、成分ごとの大きい方で包む。下端はそのまま
        const Vector3 scale = Vector3::Max(m_shownDrawScale.Previous(), GetDrawScale());
        if (scale != k_NoDrawScale)
        {
            const float bottom = out.Center.y - out.Extents.y;
            out.Extents.x *= scale.x;
            out.Extents.y *= scale.y;
            out.Extents.z *= scale.z;
            out.Center.y = bottom + out.Extents.y;
        }
        out.Center.x += m_drawOffset.x;
        out.Center.y += m_drawOffset.y;
        out.Center.z += m_drawOffset.z;
        return out;
    }
} // namespace NS
