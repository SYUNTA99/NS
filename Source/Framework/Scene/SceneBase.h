#pragma once

/// @file SceneBase.h
/// @brief NS::Scene::SceneBase — レベル / ステージの実行時 root を表す scene-graph 基底クラス
///
/// @details
/// 命名は
/// 「Scene 層内の root scene」 を素直に表す `SceneBase` を採用した
/// 旧 NS::App::Scene の stutter `Scene::Scene` を回避しつつ「Scene」 という業界用語を保つ
///
/// 責務:
/// 1. **ライフサイクル hook** — Application から OnStart / OnUpdate(dt) / OnRender /
///    OnShutdown を順序通り呼び戻される。 fixed timestep + variable render で駆動する
/// 2. **IRenderable registry** — MeshRendererComponent 等の自己登録窓口
///    描画 iteration はここが握り、 Player.cpp / Block.cpp は render 0 行
/// 3. **scene-graph root** — Player / Block 等の GameObject が AttachScene(this) で
///    この SceneBase に bind される
///
/// 寿命: Application が unique_ptr<SceneBase> で所有。 Run() 終了時に Shutdown() 後 reset()
///
/// 派生想定: Game/LevelEditorScene 等が継承し OnStart で level 構築、 RegisterRenderable を
/// override して描画 list を貯める。 default 実装は全 method が何もしない実装なので不要分は省略可
///
/// 将来拡張: SceneManager で push/pop/replace により複数 SceneBase の切替対応予定

#include "Framework/Graphics/RenderSettings.h"
#include "Framework/Physics/PhysicsWorld.h"
#include "Framework/Scene/SceneSubsystem.h"

#include <memory>
#include <typeindex>
#include <unordered_map>
#include <vector>

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

        /// 固定タイムステップ Update で既定は 1/60。物理・入力判定はここ、Render は補間のみ
        virtual void OnUpdate() {}

        /// 可変フレーム Render の入口。派生はこれを override せず OnRenderScene を実装する
        /// scene 段解決の呼び忘れを防ぐため final 化し、解決を必ず通してから描画本体へ委譲する
        virtual void OnRender() final;

        /// MainLoop 終了後に 1 回呼ばれる。Window/Renderer はまだ有効、Shutdown 後に解放
        virtual void OnShutdown() {}

        /// IRenderable Component の自己登録。MeshRendererComponent 等が OnStart で呼ぶ。 二重登録は無視する
        /// 基底が container を一元管理する。テスト等が spy するため virtual だが、通常は override しない
        virtual void RegisterRenderable(IRenderable* renderable);
        /// IRenderable Component の自己解除。MeshRendererComponent 等が OnEndPlay で呼ぶ
        virtual void UnregisterRenderable(IRenderable* renderable);

        /// 型で service を取得する。scene tier を先に見て、無ければ app tier provider へ委譲する
        /// どちらにも無ければ nullptr を返す
        template <class T> [[nodiscard]] T* GetSubsystem() noexcept
        {
            const std::type_index key{typeid(T)};
            if (const auto it = m_subsystems.find(key); it != m_subsystems.end())
                return static_cast<T*>(it->second.get());
            if (m_appProvider != nullptr)
                return static_cast<T*>(m_appProvider->FindAppSubsystem(key));
            return nullptr;
        }

        /// app tier service の解決口を差し込む。SceneManager が OnStart 前に設定する
        void SetSubsystemProvider(ISubsystemProvider* provider) noexcept { m_appProvider = provider; }

        /// 登録テーブルの scene tier を走査し shouldCreate 通過分を生成・Initialize する
        /// SceneManager が OnStart 直前に呼ぶ。生成済みの型は再生成しない
        void CreateSceneSubsystems();

        /// 生成済みの scene tier service を Deinitialize する。SceneManager が OnShutdown 後に呼ぶ
        /// 実体の破棄は scene と共に行われ、借用元より後に service が死ぬ順序を保つ
        void DeinitSceneSubsystems();

        /// 全 scene が 1 個持つ衝突 world への可変ハンドル。派生 scene が build 時に満たし、
        /// CharacterMovementComponent 等の借用元は OnStart で所属 scene から取りに来る
        [[nodiscard]] NS::Physics::PhysicsWorld& Physics() noexcept { return m_physicsWorld; }

    protected:
        /// Opaque バケットの Renderable を登録順に描画する。Game が OnRenderScene から呼ぶ
        void DrawOpaque(const RenderContext& context);
        /// Transparent バケットを context.cameraPosition から遠い順すなわち back-to-front にソートして描画する
        /// 距離同値は SortPriority 昇順、さらに同値は登録順を保つ stable_sort のタイブレーク
        void DrawTransparent(const RenderContext& context);
        /// Overlay バケットを登録順に描画する。全 world 描画の後に呼び、暗転や HUD を最前面へ重ねる
        void DrawOverlay(const RenderContext& context);

        /// 派生がシーン単位の上書きを宣言する hook。default は空 override で project 既定値そのまま
        /// lighting 3 種すなわち lightDir / lightColor / ambientColor と clearColor を上書きできる
        virtual NS::Graphics::RenderSettingsOverride BuildSceneOverride() { return {}; }

        /// 可変フレーム Render の本体。派生が ctx を組み立てて描画する
        /// 描画前に ctx.resolvedSettings = ResolveSceneSettings(renderer.Settings()) を詰めること
        virtual void OnRenderScene() {}

        /// project 既定値に BuildSceneOverride() を Resolve した scene 段解決値を返す
        /// テスト用に protected 公開する。 Application 経路では OnRenderScene が描画前に呼ぶ
        [[nodiscard]] NS::Graphics::RenderSettings ResolveSceneSettings(
            const NS::Graphics::RenderSettings& projectDefaults);

    private:
        /// 全 scene が 1 個持つ衝突 world。当たりの有る scene だけが build で満たし、無ければ空のまま
        /// 借用する CMC は派生 scene のメンバで先に死ぬため、基底のこれは常に借用元より後まで生存する
        NS::Physics::PhysicsWorld m_physicsWorld;

        /// 登録された全 IRenderable で非所有。Game ではなく engine 側のこの基底が一元管理する
        std::vector<IRenderable*> m_renderables;

        /// type_index キーの scene tier service。scene と生成・破棄を共にする所有 collection
        std::unordered_map<std::type_index, std::unique_ptr<SceneSubsystem>> m_subsystems;
        /// app tier service の解決口で非所有。未設定なら app tier は解決しない
        ISubsystemProvider* m_appProvider = nullptr;
    };

} // namespace NS::Scene
