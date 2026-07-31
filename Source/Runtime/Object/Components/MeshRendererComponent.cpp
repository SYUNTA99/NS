#include "Runtime/Object/Components/MeshRendererComponent.h"

#include "Runtime/Graphics/Material.h"
#include "Runtime/Graphics/Mesh.h"
#include "Runtime/Graphics/RenderContext.h"
#include "Runtime/Graphics/StaticMesh.h"
#include "Runtime/Object/AssetManager.h"
#include "Runtime/Object/GameObject.h"
#include "Runtime/Object/Reflection/TypeRegistry.h"
#include "Runtime/Object/Scene/Scene.h"
#include "Runtime/Object/Transform.h"

namespace NS::Object
{
    void MeshRendererComponent::SetPerObjectVsConstant(const NS::Graphics::Buffer* cb,
                                                       const void* cpuData,
                                                       std::size_t cpuDataSize,
                                                       unsigned slot) noexcept
    {
        m_perObjectVsCb = cb;
        m_perObjectVsData = cpuData;
        m_perObjectVsSize = cpuDataSize;
        m_perObjectVsSlot = slot;
    }

    void MeshRendererComponent::ResolveAssets(AssetManager& assets)
    {
        // 共有 material 名 (player / water / shadow) を先に引き、 外れたら .mat 相対パスとして読む
        // 空・トラバーサル・読込失敗は既定の共有 player material へ倒し、 描けない状態を作らない
        NS::Graphics::Material* material = assets.SharedMaterial(m_materialRef);
        if (material == nullptr)
        {
            material = assets.SharedMaterial("player");
            if (!m_materialRef.empty())
            {
                if (const auto resolved = ResolveContentPath(m_materialRef))
                {
                    if (const LoadedMaterial loaded = assets.LoadMaterial(*resolved); loaded.material != nullptr)
                        material = loaded.material;
                }
            }
        }
        SetMaterial(material);

        // mesh は解決不可なら cube をフォールバックとする
        NS::Graphics::Mesh* resolved = ResolveMeshFromRef(assets, m_meshRef);
        if (resolved == nullptr)
            resolved = assets.Builtin("cube");
        SetMesh(resolved);
    }

    void MeshRendererComponent::OnStart()
    {
        GameObject* owner = Owner();
        if (owner == nullptr)
            return;
        Scene* scene = owner->OwningScene();
        if (scene == nullptr)
            return;
        scene->RegisterRenderable(this);
    }

    void MeshRendererComponent::OnEndPlay()
    {
        GameObject* owner = Owner();
        if (owner == nullptr)
            return;
        Scene* scene = owner->OwningScene();
        if (scene == nullptr)
            return;
        scene->UnregisterRenderable(this);
    }

    void MeshRendererComponent::Collect(const NS::Graphics::RenderContext& context,
                                        std::vector<NS::Graphics::DrawItem>& out)
    {
        GameObject* owner = Owner();
        if (!IsActive() || m_mesh == nullptr || m_material == nullptr || owner == nullptr)
            return;

        // ctx.resolvedSettings は project 既定 ← scene override まで解決済。ここに個体段 override を載せる
        const NS::Graphics::RenderSettings finalSettings =
            NS::Graphics::Resolve(context.resolvedSettings, m_objectOverride);

        NS::Graphics::DrawItem item{};
        item.mesh = m_mesh;
        item.material = m_material;
        item.blend = m_material->Blend();
        item.constants.world = owner->Root().InterpolatedWorldMatrix(context.alpha);
        item.constants.viewProj = context.viewProjection;
        item.constants.lightDir = finalSettings.lightDir;
        item.constants.lightDir.Normalize();
        item.constants.baseColor = m_baseColor; // 個体色は lighting と別系統
        item.constants.lightColor = finalSettings.lightColor;
        item.constants.ambientColor = finalSettings.ambientColor;
        item.constants.groundColor = finalSettings.groundColor;
        item.constants.exposure = finalSettings.exposure;
        item.extraVsCb = m_perObjectVsCb;
        item.extraVsData = m_perObjectVsData;
        item.extraVsSize = m_perObjectVsSize;
        item.extraVsSlot = m_perObjectVsSlot;
        out.push_back(item);
    }

    RenderBucket MeshRendererComponent::Bucket() const noexcept
    {
        if (m_material == nullptr)
            return RenderBucket::Opaque;
        if (m_material->Blend() == NS::Graphics::BlendMode::Opaque)
            return RenderBucket::Opaque;
        return RenderBucket::Transparent;
    }

    NS::Math::Vector3 MeshRendererComponent::SortCenter() const noexcept
    {
        const GameObject* owner = Owner();
        if (owner == nullptr)
            return {};
        const NS::Math::Matrix world = owner->Root().WorldMatrix();
        return NS::Math::Vector3{world._41, world._42, world._43};
    }

    int MeshRendererComponent::SortPriority() const noexcept
    {
        if (m_material != nullptr)
            return m_material->RenderPriority();
        return 0;
    }

    NS::Math::AABB MeshRendererComponent::WorldBounds() const noexcept
    {
        const GameObject* owner = Owner();
        if (m_mesh == nullptr || owner == nullptr)
            return {}; // 描くものが無い。間引かれても Collect が何も積まず結果は変わらない
        NS::Math::AABB out{};
        // skinned は現在ポーズの override を優先、無ければ mesh 固定のバインド箱
        if (m_hasLocalBoundsOverride)
            m_localBoundsOverride.Transform(out, owner->Root().WorldMatrix());
        else
            m_mesh->LocalBounds().Transform(out, owner->Root().WorldMatrix());
        return out;
    }

    // 既定コンストラクタが無いので mesh / material 空で構築し、読み込み時に反射 / ResolveAssets が差し込む
    NS_CLASS(MeshRendererComponent)
} // namespace NS::Object
