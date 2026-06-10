#pragma once

/// @file SceneBase.h
/// @brief NS::Scene::SceneBase — レベル / ステージの実行時 root を表す scene-graph 基底クラス
///
/// @details
/// 命名は
/// 「Scene 層内の root scene」 を素直に表す `SceneBase` を採用 (旧 NS::App::Scene の
/// stutter `Scene::Scene` を回避しつつ「Scene」 という業界用語を保つ)
///
/// 責務:
/// 1. **ライフサイクル hook** — Application から OnStart / OnUpdate(dt) / OnRender /
///    OnShutdown を順序通り呼び戻される (fixed timestep + variable render)
/// 2. **IRenderable registry** — MeshRendererComponent 等の自己登録窓口
///    描画 iteration はここが握り、 Player.cpp / Block.cpp は render 0 行
/// 3. **scene-graph root** — GameObject (Player / Block 等) が AttachScene(this) で
///    この SceneBase に bind される
///
/// 寿命: Application が unique_ptr<SceneBase> で所有。 Run() 終了時に Shutdown() 後 reset()
///
/// 派生想定: Game/LevelEditorScene 等が継承し OnStart で level 構築、 RegisterRenderable を
/// override して描画 list を貯める。 default 実装は全 method が何もしない実装なので不要分は省略可
///
/// 将来拡張: SceneManager (push/pop/replace) で複数 SceneBase の切替対応予定

#include "Framework/Graphics/RenderSettings.h"

namespace NS::Scene
{

    class IRenderable;
    struct RenderContext;

    class SceneBase
    {
    public:
        SceneBase() = default;
        virtual ~SceneBase() = default;

        SceneBase(const SceneBase&) = delete;
        SceneBase& operator=(const SceneBase&) = delete;
        SceneBase(SceneBase&&) = delete;
        SceneBase& operator=(SceneBase&&) = delete;

        /// Application::Run() 開始時に 1 回呼ばれる。Window/Renderer/Input は既に有効
        virtual void OnStart() {}

        /// 固定タイムステップ Update (default 1/60)。物理・入力判定はここ、Render は補間のみ
        virtual void OnUpdate() {}

        /// 可変フレーム Render の入口。派生はこれを override せず OnRenderScene を実装する
        /// scene 段解決の呼び忘れを防ぐため final 化し、解決を必ず通してから描画本体へ委譲する
        virtual void OnRender() final;

        /// MainLoop 終了後に 1 回呼ばれる。Window/Renderer はまだ有効、Shutdown 後に解放
        virtual void OnShutdown() {}

        /// IRenderable Component の自己登録。MeshRendererComponent 等が OnStart で呼ぶ
        virtual void RegisterRenderable(IRenderable* renderable) { (void)renderable; }
        /// IRenderable Component の自己解除。MeshRendererComponent 等が OnEndPlay で呼ぶ
        virtual void UnregisterRenderable(IRenderable* renderable) { (void)renderable; }

    protected:
        /// 派生がシーン単位の上書きを宣言する hook。default は空 override (= project 既定値そのまま)
        /// lighting 3 種 (lightDir / lightColor / ambientColor) と clearColor を上書きできる
        virtual NS::Graphics::RenderSettingsOverride BuildSceneOverride() { return {}; }

        /// 可変フレーム Render の本体。派生が ctx を組み立てて描画する
        /// 描画前に ctx.resolvedSettings = ResolveSceneSettings(renderer.Settings()) を詰めること
        virtual void OnRenderScene() {}

        /// project 既定値に BuildSceneOverride() を Resolve した scene 段解決値を返す
        /// テスト用に protected 公開 (Application 経路では OnRenderScene が描画前に呼ぶ)
        [[nodiscard]] NS::Graphics::RenderSettings ResolveSceneSettings(
            const NS::Graphics::RenderSettings& projectDefaults);
    };

} // namespace NS::Scene
