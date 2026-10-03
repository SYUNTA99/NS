#include "Runtime/Object/Components/Model.h"
#include "Runtime/Core/AABB.h"

#include "Runtime/Graphics/Material.h"
#include "Runtime/Graphics/Mesh.h"
#include "Runtime/Graphics/RenderContext.h"
#include "Runtime/Graphics/StaticMesh.h"
#include "Runtime/Object/Actor.h"
#include "Runtime/Object/AssetManager.h"
#include "Runtime/Object/Reflection/TypeRegistry.h"
#include "Runtime/Object/Scene/Scene.h"
#include "Runtime/Object/Transform.h"
#include <cmath>

namespace
{
    // 描く時だけの倍率の既定。この値の間は描く行列にも境界にも何も掛けない
    const NS::Core::Vector3 k_NoDrawScale{1.0f, 1.0f, 1.0f};

    // 0 以下と非数・無限大の成分を弾き、全部が有限の正の時だけ真を返す
    [[nodiscard]] bool IsPositiveFiniteScale(const NS::Core::Vector3& scale) noexcept
    {
        return std::isfinite(scale.x) && std::isfinite(scale.y) && std::isfinite(scale.z) && scale.x > 0.0f &&
               scale.y > 0.0f && scale.z > 0.0f;
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
    }

    void Model::OnStart()
    {
        Actor* owner = Owner();
        if (owner == nullptr)
        {
            return;
        }
        Scene* scene = owner->OwningScene();
        if (scene == nullptr)
        {
            return;
        }
        scene->RegisterRenderable(this);
    }

    void Model::OnEndPlay()
    {
        Actor* owner = Owner();
        if (owner == nullptr)
        {
            return;
        }

        Scene* scene = owner->OwningScene();
        if (scene == nullptr)
        {
            return;
        }
        scene->UnregisterRenderable(this);
    }

    void Model::Snapshot() noexcept
    {
        m_previousLocalRotation = m_localRotation;
        m_previousDrawScale = m_drawScale;
    }

    bool Model::SetDrawScale(const NS::Core::Vector3& scale) noexcept
    {
        if (!IsPositiveFiniteScale(scale))
        {
            return false;
        }
        m_drawScale = scale;
        return true;
    }

    bool Model::SnapDrawScale(const NS::Core::Vector3& scale) noexcept
    {
        if (!IsPositiveFiniteScale(scale))
        {
            return false;
        }
        m_drawScale = scale;
        m_previousDrawScale = scale;
        return true;
    }

    bool Model::SetDrawOffset(const NS::Core::Vector3& offset) noexcept
    {
        if (!(std::isfinite(offset.x) && std::isfinite(offset.y) && std::isfinite(offset.z)))
        {
            return false;
        }
        m_drawOffset = offset;
        return true;
    }

    const NS::Core::AABB* Model::DrawnLocalBounds() const noexcept
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

    NS::Core::Matrix Model::DrawWorldMatrix(float alpha) const noexcept
    {
        const NS::Core::Quaternion local = NS::Core::Quaternion::Slerp(m_previousLocalRotation, m_localRotation, alpha);
        const NS::Core::Matrix localMatrix = NS::Core::Matrix::CreateFromQuaternion(local);
        const Actor* owner = Owner();
        if (owner == nullptr)
        {
            return localMatrix;
        }
        // 回転を先に掛ける。根のスケールは根の軸に残り、局所の回転と一緒に回らない
        const NS::Core::Matrix root = owner->Root().InterpolatedWorldMatrix(alpha);
        NS::Core::Matrix drawn = localMatrix * root;
        // ずれは倍率の後に世界で足す。倍率の中心 (下端の真ん中) は根から測るので、ずれと一緒に動いて形を変えない
        // ずれが 0 の間は掛けない。0 の平行移動でも掛け算が下の桁を丸めることがある
        NS::Core::Matrix shift = NS::Core::Matrix::Identity;
        const bool shifted = m_drawOffset.x != 0.0f || m_drawOffset.y != 0.0f || m_drawOffset.z != 0.0f;
        if (shifted)
        {
            shift = NS::Core::Matrix::CreateTranslation(m_drawOffset);
        }
        const NS::Core::Vector3 scale = NS::Core::Vector3::Lerp(m_previousDrawScale, m_drawScale, alpha);
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
        NS::Core::Vector3 pivot{root._41, root._42, root._43};
        if (const NS::Core::AABB* localBounds = DrawnLocalBounds())
        {
            NS::Core::AABB worldBounds{};
            localBounds->Transform(worldBounds, root);
            pivot = NS::Core::Vector3{
                worldBounds.Center.x, worldBounds.Center.y - worldBounds.Extents.y, worldBounds.Center.z};
        }
        // 倍率は世界の軸で掛ける。局所の回転と根の回転に依らず世界の縦に潰れる
        NS::Core::Matrix scaled = drawn * NS::Core::Matrix::CreateTranslation(-pivot) *
                                  NS::Core::Matrix::CreateScale(scale) * NS::Core::Matrix::CreateTranslation(pivot);
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

        // context.resolvedSettings は project 既定に配置された平行光まで解決済
        const NS::Gfx::RenderSettings& settings = context.resolvedSettings;

        NS::Gfx::DrawItem item{};
        item.mesh = m_mesh;
        item.material = m_material;
        item.blend = m_material->Blend();
        item.constants.world = DrawWorldMatrix(context.alpha);
        item.constants.viewProj = context.viewProjection;
        item.constants.lightDir = settings.lightDir;
        item.constants.lightDir.Normalize();
        item.constants.baseColor = m_baseColor; // 個体色は lighting と別系統
        item.constants.lightColor = settings.lightColor;
        item.constants.ambientColor = settings.ambientColor;
        item.constants.groundColor = settings.groundColor;
        item.constants.exposure = settings.exposure;
        item.extraVsCb = m_perObjectVsCb;
        item.extraVsData = m_perObjectVsData;
        item.extraVsSize = m_perObjectVsSize;
        item.extraVsSlot = m_perObjectVsSlot;
        out.push_back(item);
    }

    RenderBucket Model::Bucket() const noexcept
    {
        if (m_material == nullptr)
        {
            return RenderBucket::Opaque;
        }
        if (m_material->Blend() == NS::Gfx::BlendMode::Opaque)
        {
            return RenderBucket::Opaque;
        }

        return RenderBucket::Transparent;
    }

    NS::Core::Vector3 Model::SortCenter() const noexcept
    {
        const Actor* owner = Owner();
        if (owner == nullptr)
        {
            return {};
        }
        const NS::Core::Matrix world = owner->Root().WorldMatrix();
        return NS::Core::Vector3{world._41, world._42, world._43};
    }

    int Model::SortPriority() const noexcept
    {
        if (m_material != nullptr)
        {
            return m_material->RenderPriority();
        }
        return 0;
    }

    NS::Core::AABB Model::WorldBounds() const noexcept
    {
        const Actor* owner = Owner();
        if (m_mesh == nullptr || owner == nullptr)
        {
            return {}; // 描くものが無い。間引かれても Collect が何も積まず結果は変わらない
        }
        NS::Core::AABB out{};
        // skinned は現在ポーズの override を優先、無ければ mesh 固定のバインド箱
        if (m_hasLocalBoundsOverride)
        {
            m_localBoundsOverride.Transform(out, owner->Root().WorldMatrix());
        }
        else
        {
            m_mesh->LocalBounds().Transform(out, owner->Root().WorldMatrix());
        }

        // 描く時は前と今の倍率の間を補間するので、成分ごとの大きい方で包む。下端はそのまま
        const NS::Core::Vector3 scale = NS::Core::Vector3::Max(m_previousDrawScale, m_drawScale);
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
