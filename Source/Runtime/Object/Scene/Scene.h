#pragma once

#include "Runtime/Core/NonCopyable.h"
#include "Runtime/Graphics/RenderScene.h"
#include "Runtime/Graphics/RenderSettings.h"
#include "Runtime/Object/CameraSubsystem.h"
#include "Runtime/Object/Components/VirtualCameraComponent.h"
#include "Runtime/Object/Scene/SceneData.h"
#include "Runtime/Object/SkyboxSubsystem.h"
#include "Runtime/Object/World.h"
#include "Runtime/Physics/PhysicsWorld.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <type_traits>
#include <utility>
#include <vector>

namespace NS::Graphics
{
    class Renderer;
    class RenderTarget;
    struct RenderContext;
} // namespace NS::Graphics

namespace NS::Object
{
    class AssetManager;
    class IRenderable;

    /// @brief 1 つのシーンビュー。 指定の描画先へ指定の視点で world を描く単位
    /// @details target が null なら backbuffer、 viewPose が空なら Brain の選ぶカメラで描く
    struct SceneView
    {
        NS::Graphics::RenderTarget* target = nullptr; // 描画先、 非所有。 null は backbuffer
        std::optional<CameraPose> viewPose;           // 描画視点。 空なら Brain の選ぶカメラ
    };

    /// @brief SceneData から組んだ World を運転する scene
    /// @details world と環境値の所有、読込/保存/プレイ凍結の出入口、標準の world 描画パスを担う
    /// Application から OnStart / OnUpdate / OnRender / OnShutdown を順に呼び戻される
    /// fixed timestep + variable render で駆動し、IRenderable の自己登録窓口も兼ねる
    /// live な GameObject/Component が唯一の表現で、SceneData は境界でだけ使う一時器
    /// 配置物は TypeRegistry と ResolveAssets で自力で組む。組み直し後の参照解決だけ派生が hook で埋める
    /// 寿命は SceneManager が unique_ptr で所有する
    /// 依存: World / SceneData / ObjectBuilder / TypeRegistry
    class Scene : public NS::Core::NonCopyable
    {
    public:
        Scene();
        virtual ~Scene();

        /// Application::Run() 開始時に 1 回呼ばれる。Window/Renderer/Input は既に有効
        virtual void OnStart() {}

        /// 可変フレーム Render の入口。描画本体は OnRenderScene に書く
        void OnRender();

        /// IRenderable Component の自己登録。MeshRendererComponent 等が OnStart で呼ぶ。 二重登録は無視する
        /// 登録先はここが一元管理する。テスト等が差し替えて観測するため virtual だが、通常は override しない
        virtual void RegisterRenderable(IRenderable* renderable);
        /// IRenderable Component の自己解除。MeshRendererComponent 等が OnEndPlay で呼ぶ
        virtual void UnregisterRenderable(IRenderable* renderable);

        /// 型でサブシステムを取得する。scene が直接所有する 2 つのどちらかを返し、他の型は nullptr
        template <class T> [[nodiscard]] T* GetSubsystem() noexcept
        {
            if constexpr (std::is_same_v<T, CameraSubsystem>)
                return &m_cameraSubsystem;
            else if constexpr (std::is_same_v<T, SkyboxSubsystem>)
                return &m_skyboxSubsystem;
            else
                return nullptr;
        }

        /// 所有するサブシステムを Initialize する。SceneManager が OnStart 直前に呼ぶ。二度目は何もしない
        void CreateSceneSubsystems();

        /// サブシステムを Deinitialize する。SceneManager が OnShutdown 後に呼ぶ
        /// 本体の破棄は scene と共に行い、借用元より後に死ぬ順序はメンバの宣言順が保つ
        void DeinitSceneSubsystems();

        /// 衝突 world への可変ハンドル。build 時に満たし、
        /// CharacterMovementComponent 等の借用元は OnStart で所属 scene から取りに来る
        [[nodiscard]] NS::Physics::PhysicsWorld& Physics() noexcept { return m_physicsWorld; }

        /// 資産の窓口を非所有で差す。組み立て時の参照実体化が使う。未設定 (テスト等) は解決を跳ばす
        void SetAssets(AssetManager* assets) noexcept { m_assets = assets; }

        /// レンダラーを非所有で差す。標準の OnRenderScene が使う。未設定 (テスト等) は描かない
        void SetRenderer(NS::Graphics::Renderer* renderer) noexcept { m_renderer = renderer; }

        /// @brief シーンの見た目を確定する環境値。 実体側が唯一の出所で、 保存は保存時にここから写す
        [[nodiscard]] SceneEnvironment& Environment() noexcept { return m_environment; }
        [[nodiscard]] const SceneEnvironment& Environment() const noexcept { return m_environment; }

        /// @brief 直近のシーン上書きの控え。エディタの由来表示が読む
        [[nodiscard]] const NS::Graphics::RenderSettingsOverride& LastSceneOverride() const noexcept
        {
            return m_lastSceneOverride;
        }
        /// @brief 直近の scene 段解決値の控え。エディタの由来表示が読む
        [[nodiscard]] const NS::Graphics::RenderSettings& LastResolvedSettings() const noexcept
        {
            return m_lastResolvedSettings;
        }

        /// @brief シーンデータから構築されたランタイムワールド
        /// 取得子と型が同じ綴りなので、 このクラスの中では型を完全修飾しないと関数名として解決される
        [[nodiscard]] NS::Object::World& World() noexcept { return m_world; }

        /// @brief 読み込んだシーンデータを取り込み world を組み直す。 データは取込後に用済みになる一時器
        void LoadFromData(SceneData&& data);

        /// @brief 編集で動いた live の当たりを張り直し、 組み直し後 hook を呼ぶ。 object は作り直さない
        void SyncPhysics();

        /// @brief プレイ突入時に live を凍結して返す。 プレイ規則の判定と編集復帰の姿はこの凍結を読む
        const SceneData& BeginPlayBaseline();

        /// @brief 直近の凍結スナップショット。 プレイ中に限り意味を持つ
        [[nodiscard]] const SceneData& PlayBaseline() const noexcept { return m_playBaseline; }

        /// @brief テスト用に凍結スナップショットを外から与える。 以降の BeginPlayBaseline は捕捉せず据え置く
        void SetPlayBaselineForTest(SceneData data);

        /// @brief 世界を回すかの切替。 既定は回す。 エディタが編集モードの間だけ下ろす
        /// @details 切替時に一時停止とコマ送りは払う
        void SetSimulationEnabled(bool enabled) noexcept;
        [[nodiscard]] bool IsSimulationEnabled() const noexcept { return m_simulationEnabled; }

        /// @brief 時間停止。 snapshot だけ回して補間を凍らせ、 動きの一瞬を止めて観察できるようにする
        void SetSimulationPaused(bool paused) noexcept { m_simulationPaused = paused; }
        [[nodiscard]] bool IsSimulationPaused() const noexcept { return m_simulationPaused; }

        /// @brief 止めたまま次の fixed step を 1 コマだけ進める。 動いていればまず止める
        void StepSimulation() noexcept;

        /// @brief 1 フレームで描くビュー列を差す。 空なら現描画先へ Brain の視点で 1 回だけ描く
        /// @details 空でない間は各ビューを順に bind して描き分ける。 出荷 (Editor 無し) では常に空
        void SetSceneViews(std::vector<SceneView> views) noexcept { m_sceneViews = std::move(views); }

        /// @brief 型 T の一時オブジェクトをシーンの中で作って入れる。 呼出側へは生ポインタだけ返す
        /// @details 所有はシーンが握る。 印立てと開始は下の受け口が行う
        template <class T, class... Args> T* SpawnTransient(Args&&... args)
        {
            auto obj = std::make_unique<T>(std::forward<Args>(args)...);
            T* raw = obj.get();
            SpawnTransient(std::unique_ptr<GameObject>{std::move(obj)});
            return raw;
        }

        /// @brief 実行時の一時オブジェクトを world へ入れる。 一時オブジェクトの印はここで立てる
        /// @details 保存・凍結に写らず、 データからの組み直しを生き残る。 更新は配置物と同じ帯に乗る
        /// 型が実行時にしか決まらない時の受け口。 型が分かっているなら SpawnTransient<T> を使う
        GameObject* SpawnTransient(std::unique_ptr<GameObject> obj);

        /// @brief 配置物を 1 体消す。 居なければ何もしない
        /// @details 当たり箱も揃うので、 走っている世界を止めずに消せる
        /// 子は根として残る。 まとめて消したい呼び出し側が並びを決めて 1 体ずつ呼ぶ
        void DestroyObject(std::uint32_t objectId);

        /// @brief live な配置物から SceneData を起こす。 保存の出所を実体に一本化するための捕捉
        /// @details 反射で全 component の値を忠実に写す
        [[nodiscard]] SceneData CaptureLiveToSceneData() const;

        /// カメラブレンドの更新・補間スナップショット・帯の一括更新。 世界の駆動はここが持つ
        /// 読み込んだら回り続けるのが既定で、 止める口は SetSimulationEnabled / SetSimulationPaused
        virtual void OnUpdate();

        /// カメラ登録の解除と world の破棄。 派生の OnShutdown はここを呼ぶ
        virtual void OnShutdown();

    protected:
        /// Opaque バケットの Renderable を登録順に描画する
        void DrawOpaque(const NS::Graphics::RenderContext& context);
        /// Transparent バケットを context.cameraPosition から遠い順にソートして描画する
        /// 距離が同じなら SortPriority 昇順、それも同じなら stable_sort が登録順を保つ
        void DrawTransparent(const NS::Graphics::RenderContext& context);

        /// @brief 標準の描画。 world 描画パス→デバッグ描画の吐き出し→一時オブジェクトの重ね描き
        virtual void OnRenderScene();

        /// @brief シーン単位の描画上書きを返す。既定は配置された平行光から照明 3 種を組む
        /// 平行光が無ければ空を返し、project 既定値がそのまま残る
        virtual NS::Graphics::RenderSettingsOverride BuildSceneOverride();

        /// @brief project 既定値に BuildSceneOverride() を Resolve し、上書きと解決値を控えに残す
        [[nodiscard]] virtual NS::Graphics::RenderSettings ResolveSceneSettings(
            const NS::Graphics::RenderSettings& projectDefaults);

        /// @brief world の組み直し・当たりの張り直しの後に呼ばれる。 派生は live への参照をここで取り直す
        virtual void OnWorldChanged() {}

        /// @brief 渡されたシーンデータから world と衝突判定世界を組み直す。 データはその場限りの一時器
        void RebuildWorldFrom(const SceneData& data);

        /// @brief 標準の world 描画パス。 環境同期→カメラ評価→不透明→空→半透明
        /// @param viewOverride 描画視点の上書き。 空なら Brain の選ぶカメラで描く
        /// @return 組んだ描画コンテキスト。 カメラ不在なら nullopt を返し何も描かない
        [[nodiscard]] std::optional<NS::Graphics::RenderContext> RenderWorld(
            NS::Graphics::Renderer& renderer, const std::optional<CameraPose>& viewOverride);

    private:
        /// world の変化を一時オブジェクトへ知らせる。 組み直しと当たりの張り直しの後に呼ぶ
        void NotifyTransientsWorldChanged();

        /// 1 ビュー分の world 描画と、 デバッグ描画・一時オブジェクトの重ね描きをまとめて行う
        void RenderViewWithOverlays(const std::optional<CameraPose>& viewOverride);

        /// 登録中の全 renderable の bounds とソート情報を RenderScene へ同期する。描画の入口で呼ぶ
        void SyncRenderBounds();

        /// IRenderable と RenderScene 登録ハンドルの対。renderable は非所有
        struct RenderEntry
        {
            IRenderable* renderable = nullptr;
            NS::Graphics::RenderHandle handle{};
        };
        std::vector<RenderEntry> m_renderables;

        /// 描画物の登録簿と視錐台カリングを持つレンダラ側の描画シーン
        NS::Graphics::RenderScene m_renderScene;

        CameraSubsystem m_cameraSubsystem;    // 実カメラ + Brain の窓口。scene と生成・破棄を共にする
        SkyboxSubsystem m_skyboxSubsystem;    // skybox 装置。scene と生成・破棄を共にする
        bool m_subsystemsInitialized = false; // CreateSceneSubsystems の二度目を何もしないための印

        /// 衝突 world。当たりの有る scene だけが build で満たし、無ければ空のまま
        /// 借用する CMC は先に死ぬので、これは常に借用元より後まで生存する
        NS::Physics::PhysicsWorld m_physicsWorld;

        NS::Object::World m_world;     // ランタイムワールド
        SceneEnvironment m_environment; // シーンの環境値。 実体側の唯一の出所

        NS::Graphics::RenderSettingsOverride m_lastSceneOverride{}; // 直近のシーン上書きの控え
        NS::Graphics::RenderSettings m_lastResolvedSettings{};      // 直近の scene 段解決値の控え
        bool m_warnedZeroLightDirection = false;                    // 平行光 zero 警告の 1 回制御
        AssetManager* m_assets = nullptr;             // 資産の窓口、 非所有。 未設定なら参照の実体化を跳ばす
        NS::Graphics::Renderer* m_renderer = nullptr; // レンダラー、 非所有。 未設定なら描かない

        SceneData m_playBaseline;            // プレイ突入時の凍結スナップショット
        bool m_playBaselineInjected = false; // テスト注入の凍結を捕捉で潰さないための印

        bool m_simulationEnabled = true;         // 世界を回すか。エディタの編集モードだけが下ろす
        bool m_simulationPaused = false;         // 時間停止中か
        std::int32_t m_simulationStepFrames = 0; // コマ送り残り fixed step 数。止めたままこの数だけ進める

        std::vector<SceneView> m_sceneViews; // 描くビュー列。 空なら現描画先へ 1 回だけ描く
    };
} // namespace NS::Object
