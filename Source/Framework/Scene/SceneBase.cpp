#include "Framework/Scene/SceneBase.h"

#include "Framework/Graphics/RenderSettings.h"
#include "Framework/Scene/IRenderable.h"
#include "Framework/Scene/RenderContext.h"
#include "Framework/Scene/SubsystemRegistry.h"

#include <algorithm>
#include <utility>

namespace NS::Scene
{

    NS::Graphics::RenderSettings SceneBase::ResolveSceneSettings(const NS::Graphics::RenderSettings& projectDefaults)
    {
        return NS::Graphics::Resolve(projectDefaults, BuildSceneOverride());
    }

    void SceneBase::OnRender()
    {
        // 入口を派生に握らせず final 化することで scene 解決の呼び忘れを防ぐ
        // renderer / camera は Game 層しか知らないため ctx 構築は派生の OnRenderScene に委ねる
        OnRenderScene();
    }

    void SceneBase::RegisterRenderable(IRenderable* renderable)
    {
        if (renderable == nullptr)
            return;
        // 二重登録を防ぐ。Component 側で OnStart が誤って 2 回呼ばれても二重描画にならない
        if (std::find(m_renderables.begin(), m_renderables.end(), renderable) != m_renderables.end())
            return;
        m_renderables.push_back(renderable);
    }

    void SceneBase::UnregisterRenderable(IRenderable* renderable)
    {
        if (renderable == nullptr)
            return;
        // erase-remove で全要素を消し、一意性と防御的削除を両立する
        m_renderables.erase(std::remove(m_renderables.begin(), m_renderables.end(), renderable), m_renderables.end());
    }

    void SceneBase::DrawOpaque(const RenderContext& context)
    {
        for (IRenderable* r : m_renderables)
        {
            if (r != nullptr && r->Bucket() == RenderBucket::Opaque)
                r->Draw(context);
        }
    }

    void SceneBase::DrawTransparent(const RenderContext& context)
    {
        std::vector<IRenderable*> transparent;
        transparent.reserve(m_renderables.size());
        for (IRenderable* r : m_renderables)
        {
            if (r != nullptr && r->Bucket() == RenderBucket::Transparent)
                transparent.push_back(r);
        }

        const NS::Math::Vector3 camPos = context.cameraPosition;
        // stable_sort で同 key 要素の登録順を保つ。 距離・priority 同値時の最終タイブレークになる
        std::stable_sort(
            transparent.begin(), transparent.end(), [camPos](const IRenderable* a, const IRenderable* b) noexcept {
                const float da = (a->SortCenter() - camPos).LengthSquared();
                const float db = (b->SortCenter() - camPos).LengthSquared();
                if (da != db)
                    return da > db; // 遠い順すなわち back-to-front
                return a->SortPriority() < b->SortPriority();
            });

        for (IRenderable* r : transparent)
            r->Draw(context);
    }

    void SceneBase::CreateSceneSubsystems()
    {
        for (const SubsystemEntry& entry : SubsystemRegistry::Get().Entries())
        {
            if (entry.tier != SubsystemTier::Scene)
                continue;
            if (entry.shouldCreate != nullptr && !entry.shouldCreate(*this))
                continue;
            if (m_subsystems.find(entry.type) != m_subsystems.end())
                continue;
            std::unique_ptr<SceneSubsystem> sub = entry.factory();
            sub->Initialize(*this);
            m_subsystems.emplace(entry.type, std::move(sub));
        }
    }

    void SceneBase::DeinitSceneSubsystems()
    {
        // 解放フックだけ回し、実体の破棄は scene と共に基底 collection に委ねる
        // 借用元 (CMC 等) が生存中に service を消さず、派生メンバより後に service が死ぬ順序を保つ
        for (auto& [type, sub] : m_subsystems)
        {
            (void)type;
            if (sub)
                sub->Deinitialize();
        }
    }

} // namespace NS::Scene
