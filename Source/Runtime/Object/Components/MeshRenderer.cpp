#include "Runtime/Core/AABB.h"
#include "Runtime/Object/Components/MeshRenderer.h"

#include "Runtime/Graphics/Material.h"
#include "Runtime/Graphics/Mesh.h"
#include "Runtime/Graphics/RenderContext.h"
#include "Runtime/Graphics/StaticMesh.h"
#include "Runtime/Object/AssetManager.h"
#include "Runtime/Object/GameObject.h"
#include "Runtime/Object/Reflection/TypeRegistry.h"
#include "Runtime/Object/Scene/Scene.h"
#include "Runtime/Object/Transform.h"

namespace NS::Obj
{
    void MeshRenderer::SetPerObjectVsConstant(const NS::Gfx::Buffer* cb,
                                                       const void* cpuData,
                                                       std::size_t cpuDataSize,
                                                       unsigned slot) noexcept
    {
        m_perObjectVsCb = cb;
        m_perObjectVsData = cpuData;
        m_perObjectVsSize = cpuDataSize;
        m_perObjectVsSlot = slot;
    }

    void MeshRenderer::ResolveAssets(AssetManager& assets)
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

    void MeshRenderer::OnStart()
    {
        GameObject* owner = Owner();
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

    void MeshRenderer::OnEndPlay()
    {
        GameObject* owner = Owner();
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

    void MeshRenderer::Collect(const NS::Gfx::RenderContext& context,
                                        std::vector<NS::Gfx::DrawItem>& out)
    {
        GameObject* owner = Owner();
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
        item.constants.world = owner->Root().InterpolatedWorldMatrix(context.alpha);
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

    RenderBucket MeshRenderer::Bucket() const noexcept
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

    NS::Core::Vector3 MeshRenderer::SortCenter() const noexcept
    {
        const GameObject* owner = Owner();
        if (owner == nullptr)
        {
			return {};
        }
        const NS::Core::Matrix world = owner->Root().WorldMatrix();
        return NS::Core::Vector3{world._41, world._42, world._43};
    }

    int MeshRenderer::SortPriority() const noexcept
    {
        if (m_material != nullptr)
        {
            return m_material->RenderPriority();
        }
        return 0;
    }

    NS::Core::AABB MeshRenderer::WorldBounds() const noexcept
    {
        const GameObject* owner = Owner();
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
        return out;
    }

    // mesh / material は空で構築し、読み込み時にリフレクションと ResolveAssets が差し込む
    NS_CLASS(MeshRenderer)
} // namespace NS::Obj
