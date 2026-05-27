#pragma once

/// @file SceneBase.h
/// @brief NS::Scene::SceneBase — レベル / ステージの実行時 root を表す scene-graph 基底クラス。
///
/// @details
/// UE5 の `UWorld` / Godot の `SceneTree` root に相当する scene-graph 基底。 命名は
/// 「Scene 層内の root scene」 を素直に表す `SceneBase` を採用 (旧 NS::App::Scene の
/// stutter `Scene::Scene` を回避しつつ「Scene」 という業界用語を保つ)。
///
/// 責務:
/// 1. **ライフサイクル hook** — Application から OnStart / OnUpdate(dt) / OnRender /
///    OnShutdown を順序通り呼び戻される ( fixed timestep + variable render)
/// 2. **IRenderable registry** — MeshComponent 等の自己登録窓口。
///    描画 iteration はここが握り、 Player.cpp / Block.cpp は render 0 行 (UE5/Unity 流儀)
/// 3. **scene-graph root** — GameObject (Player / Block 等) が AttachScene(this) で
///    この SceneBase に bind される ()
///
/// 寿命: Application が unique_ptr<SceneBase> で所有。 Run() 終了時に Shutdown() 後 reset()。
///
/// 派生想定: Game/LevelEditorScene 等が継承し OnStart で level 構築、 RegisterRenderable を
/// override して描画 list を貯める。 default 実装は全 method noop なので不要分は省略可。
///
/// 将来拡張: SceneManager (push/pop/replace) で複数 SceneBase の切替対応予定 ( 想定)。

namespace NS::Scene
{

    class IRenderable;

    class SceneBase
    {
    public:
        SceneBase() = default;
        virtual ~SceneBase() = default;

        SceneBase(const SceneBase&) = delete;
        SceneBase& operator=(const SceneBase&) = delete;
        SceneBase(SceneBase&&) = delete;
        SceneBase& operator=(SceneBase&&) = delete;

        /// Application::Run() 開始時に 1 回呼ばれる。Window/Renderer/Input は既に有効。
        virtual void OnStart() {}

        /// 固定タイムステップ Update。 dt は `NS::Core::FrameTimer::FixedDelta()` で取得
        /// (ApplicationDesc::fixedDelta 固定、 default 1/60)。 物理 / 入力判定はここで行い、
        /// Render 側は補間描画のみに留める。
        virtual void OnUpdate() {}

        /// 可変フレーム Render。Application::Alpha() で fixed 補間係数を取得可。
        virtual void OnRender() {}

        /// MainLoop 終了後に 1 回呼ばれる。Window/Renderer はまだ有効、Shutdown 後に解放。
        virtual void OnShutdown() {}

        /// IRenderable Component の自己登録。MeshComponent 等が OnStart で呼ぶ。
        /// 基底 default は no-op。LevelEditorScene などが override で RenderRegistry に追加する。
        virtual void RegisterRenderable(IRenderable* renderable) { (void)renderable; }
        /// IRenderable Component の自己解除。MeshComponent 等が OnEndPlay で呼ぶ。
        /// 基底 default は no-op。LevelEditorScene などが override で RenderRegistry から削除する。
        virtual void UnregisterRenderable(IRenderable* renderable) { (void)renderable; }
    };

} // namespace NS::Scene
