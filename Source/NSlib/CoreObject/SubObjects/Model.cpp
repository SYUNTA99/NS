#include "NSlib/CoreObject/SubObjects/Model.h"
#include "NSlib/Core/AABB.h"
#include "NSlib/Core/Types.h"

#include "NSlib/CoreObject/Scene/Scene.h"
#include "NSlib/CoreObject/SceneActor.h"
#include "NSlib/CoreObject/SubObjects/Transform.h"
#include "NSlib/Graphics/D3DMaterial.h"
#include "NSlib/Graphics/D3DMesh.h"
#include "NSlib/Graphics/RenderContext.h"
#include <cmath>

namespace
{
    // 描く時だけの倍率の既定。この値の間は描く行列にも境界にも何も掛けない
    const NS::Vector3 k_NoDrawScale{1.0f, 1.0f, 1.0f};
    // 止めの間の左右の写しの不透明度
    constexpr float k_SideGhostOpacity = 0.45f;

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

    void Model::Register()
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
        scene->RegisterRenderable(&m_ghosts);
    }

    void Model::Unregister()
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
        scene->UnregisterRenderable(&m_ghosts);
    }

    void Model::Snapshot() noexcept
    {
        m_previousLocalRotation = m_localRotation;
        m_previousDrawScale = m_drawScale;
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

    bool Model::SetDrawScale(const Vector3& scale) noexcept
    {
        if (!IsPositiveFiniteScale(scale))
        {
            return false;
        }
        m_drawScale = scale;
        return true;
    }

    bool Model::SnapDrawScale(const Vector3& scale) noexcept
    {
        if (!IsPositiveFiniteScale(scale))
        {
            return false;
        }
        m_drawScale = scale;
        m_previousDrawScale = scale;
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

    bool Model::SetGhostSpread(const Vector3& spread) noexcept
    {
        if (!IsFinite(spread))
        {
            return false;
        }
        m_ghostSpread = spread;
        return true;
    }

    Matrix Model::GhostWorldMatrix(float alpha, float side) const noexcept
    {
        return DrawWorldMatrixWithOffset(alpha, m_ghostSpread * side);
    }

    void Model::CaptureAfterimage() noexcept
    {
        m_afterimageWorld = DrawWorldMatrix(1.0f);
    }

    bool Model::SetAfterimageOpacity(float opacity) noexcept
    {
        if (!std::isfinite(opacity))
        {
            return false;
        }
        m_afterimageOpacity = std::clamp(opacity, 0.0f, 1.0f);
        return true;
    }

    bool Model::SetTremor(const Gfx::TremorCB& tremor) noexcept
    {
        if (!IsFinite(tremor.contactOffset) || !IsFinite(tremor.right) || !IsFinite(tremor.up) ||
            !std::isfinite(tremor.amplitude) || !std::isfinite(tremor.elapsedFrames) ||
            !std::isfinite(tremor.framesPerMeter) || !std::isfinite(tremor.ringFrames))
        {
            return false;
        }
        m_tremor = tremor;
        return true;
    }

    const AABB* Model::DrawnLocalBounds() const noexcept
    {
        if (m_hasLocalBoundsOverride)
        {
            return &m_localBoundsOverride;
        }
        if (const Gfx::D3DMesh* mesh = m_mesh.Get())
        {
            return &mesh->LocalBounds();
        }
        return nullptr;
    }

    Matrix Model::DrawWorldMatrix(float alpha) const noexcept
    {
        return DrawWorldMatrixWithOffset(alpha, m_drawOffset);
    }

    Matrix Model::DrawWorldMatrixWithOffset(float alpha, const Vector3& offset) const noexcept
    {
        const Quaternion local = Quaternion::Slerp(m_previousLocalRotation, m_localRotation, alpha);
        const Matrix localMatrix = WithLocalScale(m_localScale, Matrix::CreateFromQuaternion(local));
        if (m_owner == nullptr)
        {
            return localMatrix;
        }
        // 回転を先に掛ける。根のスケールは根の軸に残り、局所の回転と一緒に回らない
        const Matrix root = m_owner->Root().InterpolatedWorldMatrix(alpha);
        Matrix drawn = localMatrix * root;
        // ずれは倍率の後に世界で足す。倍率の中心 (下端の真ん中) は根から測るので、ずれと一緒に動いて形を変えない
        // ずれが 0 の間は掛けない。0 の平行移動でも掛け算が下の桁を丸めることがある
        Matrix shift = Matrix::Identity;
        const bool shifted = offset.x != 0.0f || offset.y != 0.0f || offset.z != 0.0f;
        if (shifted)
        {
            shift = Matrix::CreateTranslation(offset);
        }
        const Vector3 scale = Vector3::Lerp(m_previousDrawScale, m_drawScale, alpha);
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

    void Model::Collect(const Gfx::RenderContext& context, std::vector<Gfx::DrawItem>& out)
    {
        Gfx::D3DMaterial* material = m_material.Get();
        if (m_owner == nullptr || !m_owner->IsAppeared() || m_mesh.Get() == nullptr || material == nullptr)
        {
            return;
        }

        out.push_back(MakeDrawItem(context, material, DrawWorldMatrix(context.alpha)));
    }

    Gfx::DrawItem Model::MakeDrawItem(const Gfx::RenderContext& context,
                                          Gfx::D3DMaterial* material,
                                          const Matrix& world) const noexcept
    {
        // context.resolvedSettings は project 既定に配置された平行光まで解決済
        const Gfx::RenderSettings& settings = context.resolvedSettings;

        Gfx::DrawItem item{};
        item.mesh = m_mesh.Get();
        item.material = material;
        item.blend = material->Blend();
        item.constants.world = world;
        item.constants.viewProj = context.viewProjection;
        item.constants.lightDir = settings.lightDir;
        item.constants.lightDir.Normalize();
        item.constants.baseColor = m_baseColor.ToVector3(); // 個体色は lighting と別系統
        item.constants.lightColor = settings.lightColor;
        item.constants.ambientColor = settings.ambientColor;
        item.constants.groundColor = settings.groundColor;
        item.constants.exposure = settings.exposure;
        item.constants.groundWaveCenterX = context.groundWave.centerX;
        item.constants.groundWaveCenterZ = context.groundWave.centerZ;
        item.constants.groundWaveRadius = context.groundWave.radius;
        item.constants.groundWaveStrength = context.groundWave.strength;
        item.constants.tremor = m_tremor;
        item.extraVsCb = m_perObjectVsCb;
        item.extraVsData = m_perObjectVsData;
        item.extraVsSize = m_perObjectVsSize;
        item.extraVsSlot = m_perObjectVsSlot;
        return item;
    }

    void Model::Ghosts::Collect(const Gfx::RenderContext& context, std::vector<Gfx::DrawItem>& out)
    {
        Gfx::D3DMaterial* material = m_model.m_ghostMaterial.Get();
        if (m_model.m_owner == nullptr || !m_model.m_owner->IsAppeared() || m_model.m_mesh.Get() == nullptr ||
            material == nullptr)
        {
            return;
        }
        const Vector3& spread = m_model.m_ghostSpread;
        if (spread.x != 0.0f || spread.y != 0.0f || spread.z != 0.0f)
        {
            for (const float side : {1.0f, -1.0f})
            {
                Gfx::DrawItem item = m_model.MakeDrawItem(
                    context, material, m_model.GhostWorldMatrix(context.alpha, side));
                item.constants.opacity = k_SideGhostOpacity;
                out.push_back(item);
            }
        }
        if (m_model.m_afterimageOpacity > 0.0f)
        {
            // 控えた姿のまま残す。震えを足すと止まった姿に見えない
            Gfx::DrawItem item = m_model.MakeDrawItem(context, material, m_model.m_afterimageWorld);
            item.constants.tremor = Gfx::TremorCB{};
            item.constants.opacity = m_model.m_afterimageOpacity;
            out.push_back(item);
        }
    }

    AABB Model::Ghosts::WorldBounds() const noexcept
    {
        // 描く形の箱を、描く時だけのずれと残像の離れの分だけ広げる。どちらの写しも外さない
        AABB out = m_model.WorldBounds();
        const Vector3& spread = m_model.m_ghostSpread;
        const Vector3& offset = m_model.m_drawOffset;
        out.Extents.x += std::abs(spread.x) + std::abs(offset.x);
        out.Extents.y += std::abs(spread.y) + std::abs(offset.y);
        out.Extents.z += std::abs(spread.z) + std::abs(offset.z);
        const AABB* local = m_model.DrawnLocalBounds();
        if (m_model.m_afterimageOpacity > 0.0f && local != nullptr)
        {
            // 明けの写しは根が飛び去った後も当たった所に残るので、控えた姿の箱も包む
            AABB afterimage{};
            local->Transform(afterimage, m_model.m_afterimageWorld);
            AABB merged{};
            AABB::CreateMerged(merged, out, afterimage);
            return merged;
        }
        return out;
    }

    RenderBucket Model::Bucket() const noexcept
    {
        const Gfx::D3DMaterial* material = m_material.Get();
        if (material == nullptr || material->Blend() == Gfx::BlendMode::Opaque)
        {
            return RenderBucket::Opaque;
        }

        return RenderBucket::Transparent;
    }

    Vector3 Model::SortCenter() const noexcept
    {
        if (m_owner == nullptr)
        {
            return {};
        }
        return m_owner->Root().WorldMatrix().Translation();
    }

    int Model::SortPriority() const noexcept
    {
        if (const Gfx::D3DMaterial* material = m_material.Get())
        {
            return material->RenderPriority();
        }
        return 0;
    }

    AABB Model::WorldBounds() const noexcept
    {
        if (m_mesh.Get() == nullptr || m_owner == nullptr)
        {
            return {}; // 描くものが無い。間引かれても Collect が何も積まず結果は変わらない
        }
        AABB out{};
        // m_mesh は確認済みで、描画の境界を取得できる
        DrawnLocalBounds()->Transform(out, WithLocalScale(m_localScale, m_owner->Root().WorldMatrix()));

        // 描く時は前と今の倍率の間を補間するので、成分ごとの大きい方で包む。下端はそのまま
        const Vector3 scale = Vector3::Max(m_previousDrawScale, m_drawScale);
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
