#include "Framework/Scene/SceneBase.h"

#include "Framework/Graphics/RenderSettings.h"
#include "Framework/Scene/RenderContext.h"

namespace NS::Scene
{

    NS::Graphics::RenderSettings SceneBase::ResolveSceneSettings(const NS::Graphics::RenderSettings& projectDefaults)
    {
        return NS::Graphics::Resolve(projectDefaults, BuildSceneOverride());
    }

    void SceneBase::OnRender()
    {
        // 入口を派生に握らせず final 化することで scene 解決の呼び忘れを防ぐ
        // renderer / camera は Game 層しか知らないため ctx 構築は派生 (OnRenderScene) に委ねる
        OnRenderScene();
    }

} // namespace NS::Scene
