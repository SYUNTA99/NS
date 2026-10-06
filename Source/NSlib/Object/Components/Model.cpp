#include "NSlib/Object/Components/Model.h"
#include "NSlib/Core/AABB.h"

#include "NSlib/Graphics/Material.h"
#include "NSlib/Graphics/Mesh.h"
#include "NSlib/Graphics/RenderContext.h"
#include "NSlib/Graphics/StaticMesh.h"
#include "NSlib/Object/Actor.h"
#include "NSlib/Object/AssetManager.h"
#include "NSlib/Object/Reflection/TypeRegistry.h"
#include "NSlib/Object/Scene/Scene.h"
#include "NSlib/Object/Transform.h"
#include <cmath>

namespace
{
    // 描く時だけの倍率の既定。この値の間は描く行列にも境界にも何も掛けない
    const NS::Vector3 k_NoDrawScale{1.0f, 1.0f, 1.0f};

    // 0 以下と非数・無限大の成分を弾き、全部が有限の正の時だけ真を返す
    [[nodiscard]] bool IsPositiveFiniteScale(const NS::Vector3& scale) noexcept
    {
        return NS::IsPositiveFinite(scale.x) && NS::IsPositiveFinite(scale.y) && NS::IsPositiveFinite(scale.z);
    }
} // namespace

namespace NS::Obj
{
    void Model::SetPerObjectVsConstant(const NS::Gfx::Buffer* cb,
                                       const void* cpuData,
                                       std::size_t cpuDataSize,
                                       unsigned slot) noexcept
    {
        m_perObjectVsCb = cb;
        m_perObjectVsData = cpuData;
        m_perObjectVsSize = cpuDataSize;
        m_perObjectVsSlot = slot;
    }

    void Model::ResolveAssets(AssetManager& assets)
    {
        // 共有 material 名 (player / water / shadow) を先に引き、外れたら .mat 相対パスとして読む
        // 空・トラバーサル・読込失敗は既定の共有 player material にして、描けない状態を作らない
        NS::Gfx::Material* material = assets.SharedMaterial(m_materialRef);
        if (material == nullptr)
        {
            material = assets.SharedMaterial("player");
            if (!m_materialRef.empty())
            {
                if (const std::optional<std::string> resolved = ResolveContentPath(m_materialRef))
                {
                    if (const LoadedMaterial loaded = assets.LoadMaterial(*resolved); loaded.material != nullptr)
                    {
                        material = loaded.material;
                    }
                }
            }
        }
        SetMaterial(material);

        // mesh は解決不可なら cube をフォールバックとする
        NS::Gfx::Mesh* resolved = ResolveMeshFromRef(assets, m_meshRef);
        if (resolved == nullptr)
        {
            resolved = assets.Builtin("cube");
        }

        SetMesh(resolved);
        m_ghostMaterial = assets.SharedMaterial("water");
    }

    void Model::OnStart()
    {
        Scene* scene = OwningScene();
        if (scene == nullptr)
        {
            return;
        }
        scene->RegisterRenderable(this);
        scene->RegisterRenderable(&m_ghosts);
    }

    void Model::OnEndPlay()
    {
        Scene* scene = OwningScene();
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

    bool Model::SetDrawScale(const NS::Vector3& scale) noexcept
    {
        if (!IsPositiveFiniteScale(scale))
        {
            return false;
        }
        m_drawScale = scale;
        return true;
    }

    bool Model::SnapDrawScale(const NS::Vector3& scale) noexcept
    {
        if (!IsPositiveFiniteScale(scale))
        {
            return false;
        }
        m_drawScale = scale;
        m_previousDrawScale = scale;
        return true;
    }

    bool Model::SetDrawOffset(const NS::Vector3& offset) noexcept
    {
        if (!NS::IsFinite(offset))
        {
            return false;
        }
        m_drawOffset = offset;
        return true;
    }

    bool Model::SetGhostSpread(const NS::Vector3& spread) noexcept
    {
        if (!NS::IsFinite(spread))
        {
            return false;
        }
        m_ghostSpread = spread;
        return true;
    }

    NS::Matrix Model::GhostWorldMatrix(float alpha, float side) const noexcept
    {
        return DrawWorldMatrixWithOffset(alpha, m_ghostSpread * side);
    }

    bool Model::SetTremor(const NS::Gfx::TremorCB& tremor) noexcept
    {
        if (!NS::IsFinite(tremor.contactOffset) || !NS::IsFinite(tremor.right) || !NS::IsFinite(tremor.up) ||
            !std::isfinite(tremor.amplitude) || !std::isfinite(tremor.elapsedFrames) ||
            !std::isfinite(tremor.framesPerMeter) || !std::isfinite(tremor.ringFrames))
        {
            return false;
        }
        m_tremor = tremor;
        return true;
    }

    const NS::AABB* Model::DrawnLocalBounds() const noexcept
    {
        if (m_hasLocalBoundsOverride)
        {
            return &m_localBoundsOverride;
        }
        if (m_mesh != nullptr)
        {
            return &m_mesh->LocalBounds();
        }
        return nullptr;
    }

    NS::Matrix Model::DrawWorldMatrix(float alpha) const noexcept
    {
        return DrawWorldMatrixWithOffset(alpha, m_drawOffset);
    }

    NS::Matrix Model::DrawWorldMatrixWithOffset(float alpha, const NS::Vector3& offset) const noexcept
    {
        const NS::Quaternion local = NS::Quaternion::Slerp(m_previousLocalRotation, m_localRotation, alpha);
        const NS::Matrix localMatrix = NS::Matrix::CreateFromQuaternion(local);
        const Actor* owner = Owner();
        if (owner == nullptr)
        {
            return localMatrix;
        }
        // 回転を先に掛ける。根のスケールは根の軸に残り、局所の回転と一緒に回らない
        const NS::Matrix root = owner->Root().InterpolatedWorldMatrix(alpha);
        NS::Matrix drawn = localMatrix * root;
        // ずれは倍率の後に世界で足す。倍率の中心 (下端の真ん中) は根から測るので、ずれと一緒に動いて形を変えない
        // ずれが 0 の間は掛けない。0 の平行移動でも掛け算が下の桁を丸めることがある
        NS::Matrix shift = NS::Matrix::Identity;
        const bool shifted = offset.x != 0.0f || offset.y != 0.0f || offset.z != 0.0f;
        if (shifted)
        {
            shift = NS::Matrix::CreateTranslation(offset);
        }
        const NS::Vector3 scale = NS::Vector3::Lerp(m_previousDrawScale, m_drawScale, alpha);
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
        NS::Vector3 pivot{root._41, root._42, root._43};
        if (const NS::AABB* localBounds = DrawnLocalBounds())
        {
            NS::AABB worldBounds{};
            localBounds->Transform(worldBounds, root);
            pivot =
                NS::Vector3{worldBounds.Center.x, worldBounds.Center.y - worldBounds.Extents.y, worldBounds.Center.z};
        }
        // 倍率は世界の軸で掛ける。局所の回転と根の回転に依らず世界の縦に潰れる
        NS::Matrix scaled = drawn * NS::Matrix::CreateTranslation(-pivot) * NS::Matrix::CreateScale(scale) *
                            NS::Matrix::CreateTranslation(pivot);
        if (shifted)
        {
            scaled = scaled * shift;
        }
        return scaled;
    }

    void Model::Collect(const NS::Gfx::RenderContext& context, std::vector<NS::Gfx::DrawItem>& out)
    {
        Actor* owner = Owner();
        if (!IsActive() || m_mesh == nullptr || m_material == nullptr || owner == nullptr)
        {
            return;
        }

        out.push_back(MakeDrawItem(context, m_material, DrawWorldMatrix(context.alpha)));
    }

    NS::Gfx::DrawItem Model::MakeDrawItem(const NS::Gfx::RenderContext& context,
                                          NS::Gfx::Material* material,
                                          const NS::Matrix& world) const noexcept
    {
        // context.resolvedSettings は project 既定に配置された平行光まで解決済
        const NS::Gfx::RenderSettings& settings = context.resolvedSettings;

        NS::Gfx::DrawItem item{};
        item.mesh = m_mesh;
        item.material = material;
        item.blend = material->Blend();
        item.constants.world = world;
        item.constants.viewProj = context.viewProjection;
        item.constants.lightDir = settings.lightDir;
        item.constants.lightDir.Normalize();
        item.constants.baseColor = m_baseColor; // 個体色は lighting と別系統
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

    void Model::Ghosts::Collect(const NS::Gfx::RenderContext& context, std::vector<NS::Gfx::DrawItem>& out)
    {
        const NS::Vector3& spread = m_model.m_ghostSpread;
        if (spread.x == 0.0f && spread.y == 0.0f && spread.z == 0.0f)
        {
            return;
        }
        if (!m_model.IsActive() || m_model.m_mesh == nullptr || m_model.m_ghostMaterial == nullptr ||
            m_model.Owner() == nullptr)
        {
            return;
        }
        for (const float side : {1.0f, -1.0f})
        {
            out.push_back(
                m_model.MakeDrawItem(context, m_model.m_ghostMaterial, m_model.GhostWorldMatrix(context.alpha, side)));
        }
    }

    NS::AABB Model::Ghosts::WorldBounds() const noexcept
    {
        // 描く形の箱を、描く時だけのずれと残像の離れの分だけ広げる。どちらの写しも外さない
        NS::AABB out = m_model.WorldBounds();
        const NS::Vector3& spread = m_model.m_ghostSpread;
        const NS::Vector3& offset = m_model.m_drawOffset;
        out.Extents.x += std::abs(spread.x) + std::abs(offset.x);
        out.Extents.y += std::abs(spread.y) + std::abs(offset.y);
        out.Extents.z += std::abs(spread.z) + std::abs(offset.z);
        return out;
    }

    RenderBucket Model::Bucket() const noexcept
    {
        if (m_material == nullptr || m_material->Blend() == NS::Gfx::BlendMode::Opaque)
        {
            return RenderBucket::Opaque;
        }

        return RenderBucket::Transparent;
    }

    NS::Vector3 Model::SortCenter() const noexcept
    {
        const Actor* owner = Owner();
        if (owner == nullptr)
        {
            return {};
        }
        const NS::Matrix world = owner->Root().WorldMatrix();
        return NS::Vector3{world._41, world._42, world._43};
    }

    int Model::SortPriority() const noexcept
    {
        if (m_material != nullptr)
        {
            return m_material->RenderPriority();
        }
        return 0;
    }

    NS::AABB Model::WorldBounds() const noexcept
    {
        const Actor* owner = Owner();
        if (m_mesh == nullptr || owner == nullptr)
        {
            return {}; // 描くものが無い。間引かれても Collect が何も積まず結果は変わらない
        }
        NS::AABB out{};
        // m_mesh は確かめ済みなので、描く境界は null にならない
        DrawnLocalBounds()->Transform(out, owner->Root().WorldMatrix());

        // 描く時は前と今の倍率の間を補間するので、成分ごとの大きい方で包む。下端はそのまま
        const NS::Vector3 scale = NS::Vector3::Max(m_previousDrawScale, m_drawScale);
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

    // mesh / material は空で構築し、読み込み時にリフレクションと ResolveAssets が差し込む
    NS_CLASS(Model)
} // namespace NS::Obj
