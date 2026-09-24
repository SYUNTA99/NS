#pragma once

#include "Runtime/Core/NonCopyable.h"
#include "Runtime/Graphics/RenderSettings.h"
#include "Runtime/Object/Components/VirtualCamera.h"
#include "Runtime/Object/ObjectList.h"
#include "Runtime/Object/Scene/SceneData.h"
#include "Runtime/Object/Scene/SceneRenderer.h"
#include "Runtime/Physics/PhysicsScene.h"

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace NS::Gfx
{
    class EffectScene;
    class Renderer;
    struct RenderContext;
} // namespace NS::Gfx

namespace NS::Obj
{
    class AssetManager;
    class CameraBrain;
    class CameraComponent;
    class Component;
    class DirectionalLight;
    class IRenderable;
    class OverlayRenderer;

    //! @brief SceneData から組んだ ObjectList を駆動するシーン
    //! @details ObjectList と環境値を所有し、SceneData の読み書き・プレイの凍結・標準のシーン描画パスを受け持つ
    //! Application から OnStart / OnUpdate / OnRender / OnShutdown を順に呼び戻される
    //! 固定ステップの更新と可変フレームの描画で駆動し、IRenderable と OverlayRenderer の自己登録先も兼ねる
    //! live な GameObject/Component が唯一の表現で、SceneData は境界でだけ使う一時データ
    //! 配置物は TypeRegistry と ResolveAssets で自力で組む。組み直し後の参照解決だけ派生が OnObjectsRebuilt で埋める
    //! 寿命は SceneManager が unique_ptr で所有する
    //! 依存: ObjectList / SceneData / ObjectBuilder / TypeRegistry
    class Scene : public NS::Core::NonCopyable
    {
    public:
        Scene();
        virtual ~Scene();

        //! SceneManager::LoadScene がシーンを立てた直後に 1 回呼ぶ。Window/Renderer/Input は既に有効
        virtual void OnStart() {}

        //! 可変フレーム Render の入口。描画本体は OnRenderScene に書く
        void OnRender();

        //! IRenderable Component の自己登録。MeshRenderer 等が OnStart で呼ぶ。二重登録は無視する
        //! 登録簿は SceneRenderer が持つ。テスト等が差し替えて観測するため virtual だが、通常はオーバーライドしない
        virtual void RegisterRenderable(IRenderable* renderable);
        //! IRenderable Component の自己解除。MeshRenderer 等が OnEndPlay で呼ぶ
        virtual void UnregisterRenderable(IRenderable* renderable);

        //! OverlayRenderer の自己登録。基底の OnStart が呼ぶ。二重登録は無視する
        //! 並びは priority 昇順に保たれ、同値なら後から登録した方が後ろになる
        virtual void RegisterOverlay(OverlayRenderer* overlay);
        //! OverlayRenderer の自己解除。基底の OnEndPlay が呼ぶ
        virtual void UnregisterOverlay(OverlayRenderer* overlay);

        //! 平行光の自己登録。DirectionalLight が OnStart で呼ぶ。二重登録は無視する
        //! 並びは登録順。ResolveSceneSettings はこの順に読む
        virtual void RegisterLight(DirectionalLight* light);
        //! 平行光の自己解除。DirectionalLight が OnEndPlay で呼ぶ
        virtual void UnregisterLight(DirectionalLight* light);

        //! シーンの描画を駆動する brain。シーンの破棄後は nullptr
        // 関数名が型名 CameraBrain を隠すので修飾して書く
        [[nodiscard]] NS::Obj::CameraBrain* CameraBrain() noexcept;

        //! brain が駆動する実カメラ。シーンの破棄後は nullptr
        [[nodiscard]] CameraComponent* MainCamera() noexcept;

        //! PhysicsScene への参照。Scene が値で持つので寿命は Scene と同じ
        [[nodiscard]] NS::Phys::PhysicsScene& Physics() noexcept { return m_physicsScene; }
        [[nodiscard]] const NS::Phys::PhysicsScene& Physics() const noexcept { return m_physicsScene; }

        //! AssetManager を非所有で差す。組み立て時の参照実体化が使う。未設定 (テスト等) は解決を跳ばす
        void SetAssets(AssetManager* assets) noexcept { m_assets = assets; }

        //! レンダラーを非所有で差す。標準の OnRenderScene が使う。未設定 (テスト等) は描かない
        void SetRenderer(NS::Gfx::Renderer* renderer) noexcept { m_sceneRenderer.SetRenderer(renderer); }

        //! エフェクトを探すディレクトリを SceneRenderer へ渡す。空のままなら ContentRoot の Assets/Effects
        //! effectRoot は EffectScene の構築時に固まる。SetRenderer より前に差す
        void SetEffectRoot(std::string root) noexcept { m_sceneRenderer.SetEffectRoot(std::move(root)); }

        //! SceneRenderer が所有する EffectScene。SetRenderer より前は nullptr
        [[nodiscard]] NS::Gfx::EffectScene* Effects() noexcept { return m_sceneRenderer.Effects(); }

        //! @brief シーンの見た目を確定する環境値。実体側が唯一の出所で、保存は保存時にここから写す
        [[nodiscard]] SceneEnvironment& Environment() noexcept { return m_environment; }
        [[nodiscard]] const SceneEnvironment& Environment() const noexcept { return m_environment; }

        //! @brief シーンデータから組んだ配置物の一覧
        [[nodiscard]] NS::Obj::ObjectList& Objects() noexcept { return m_objects; }
        [[nodiscard]] const NS::Obj::ObjectList& Objects() const noexcept { return m_objects; }

        //! @brief 読み込んだシーンデータを取り込み配置物を組み直す。データは取込後に用済みになる一時データ
        void LoadFromData(SceneData&& data);

        //! @brief 編集で動いた live の当たりを張り直し、OnObjectsRebuilt を呼ぶ。object は作り直さない
        void SyncPhysics();

        //! @brief プレイ突入時に live を凍結して返す。プレイ規則の判定と編集復帰の姿はこの凍結を読む
        const SceneData& BeginPlayBaseline();

        //! @brief 直近の凍結スナップショット。プレイ中に限り意味を持つ
        [[nodiscard]] const SceneData& PlayBaseline() const noexcept { return m_playBaseline; }

        //! @brief テスト用に凍結スナップショットを外から与える。以降の BeginPlayBaseline は捕捉せず据え置く
        void SetPlayBaselineForTest(SceneData data);

        //! @brief live component の欄 1 つを凍結スナップショットの同じ欄へ写す
        //! @details プレイ中の手編集を、凍結から組み直す編集復帰の後へ残すための口
        //! component 丸写しにしないのは、シミュレーションが動かした値まで写すと試走の結果が凍結へ漏れるため
        //! 凍結に居ない相手 (プレイ中に湧いた object・一時オブジェクト) は写す先が無く、何もしない
        //! @param[in] comp 手編集を受けた live component
        //! @param[in] fieldName リフレクションのフィールド名。持っていない欄なら何もしない
        void WritePlayBaselineField(const Component& comp, std::string_view fieldName);

        //! @brief 世界を回すかの切替。既定は回す。エディタが編集モードの間だけ下ろす
        //! @details 切替時に一時停止とコマ送りは払う
        void SetSimulationEnabled(bool enabled) noexcept;
        [[nodiscard]] bool IsSimulationEnabled() const noexcept { return m_simulationEnabled; }

        //! @brief 時間停止。snapshot だけ回して補間を凍らせ、動きの一瞬を止めて観察できるようにする
        void SetSimulationPaused(bool paused) noexcept { m_simulationPaused = paused; }
        [[nodiscard]] bool IsSimulationPaused() const noexcept { return m_simulationPaused; }

        //! @brief 止めたまま次の fixed step を 1 コマだけ進める。動いていればまず止める
        void StepSimulation() noexcept;

        //! @brief 1 フレームで描くビュー列を差す。空なら現描画先へ Brain の視点で 1 回だけ描く
        //! @details 空でない間は各ビューを順に bind して描き分ける。出荷 (Editor 無し) では常に空
        void SetSceneViews(std::vector<SceneView> views) noexcept { m_sceneRenderer.SetSceneViews(std::move(views)); }

        //! @brief 型 T の一時オブジェクトをシーンの中で作って入れる。呼出側へは生ポインタだけ返す
        //! @details 所有はシーンが握る。印立てと開始は unique_ptr を取る SpawnTransient が行う
        template <class T, class... Args> T* SpawnTransient(Args&&... args)
        {
            auto obj = std::make_unique<T>(std::forward<Args>(args)...);
            T* raw = obj.get();
            SpawnTransient(std::unique_ptr<GameObject>{std::move(obj)});
            return raw;
        }

        //! @brief 実行時の一時オブジェクトを ObjectList へ入れる。一時オブジェクトの印はここで立てる
        //! @details 保存・凍結に写らず、データからの組み直し後も残る。更新は配置物と同じ帯に乗る
        //! 型が実行時にしか決まらない時の受け口。型が分かっているなら SpawnTransient<T> を使う
        GameObject* SpawnTransient(std::unique_ptr<GameObject> obj);

        //! @brief 実行時に配置物を 1 体入れる。新しい永続 id を振る
        //! @details 一時オブジェクトと違い保存に写り、データからの組み直しで他の配置物と一緒に消える
        GameObject* SpawnObject(std::unique_ptr<GameObject> obj);

        //! @brief 配置物を 1 体消す。居なければ何もしない
        //! @details 当たり箱も揃うので、走っている世界を止めずに消せる
        //! 子は根として残る。まとめて消したい呼び出し側が並びを決めて 1 体ずつ呼ぶ
        void DestroyObject(std::uint32_t objectId);

        //! @brief live な配置物から SceneData を作る。保存の出所を実体に一本化するための捕捉
        //! @details リフレクションで全 component の値を忠実に写す
        [[nodiscard]] SceneData CaptureLiveToSceneData() const;

        //! 補間スナップショット・帯の更新・LateUpdate 帯の手前で物理の 1 フレーム・最後にエフェクトの 1 フレーム
        //! 世界の駆動はここが持つ
        //! 物理の 1 歩の直前と直後に、稼働中の RigidBody の PrePhysicsStep / PostPhysicsStep を呼ぶ
        //! 読み込んだら回り続けるのが既定で、止める口は SetSimulationEnabled / SetSimulationPaused
        virtual void OnUpdate();

        //! 配置物の破棄。派生の OnShutdown はここを呼ぶ
        virtual void OnShutdown();

    protected:
        //! Opaque バケットの Renderable を並べ替えずに描画する
        void DrawOpaque(const NS::Gfx::RenderContext& context);
        //! Transparent バケットを context.cameraPosition から遠い順にソートして描画する
        //! 距離が同じなら SortPriority 昇順、それも同じなら stable_sort が元の並びを保つ
        void DrawTransparent(const NS::Gfx::RenderContext& context);

        //! 登録中の OverlayRenderer を priority 昇順で描画する。IsActive が偽なら飛ばす
        void DrawOverlays(const NS::Gfx::RenderContext& context);

        //! @brief 標準の描画。シーン描画パス→デバッグ描画の吐き出し→OverlayRenderer の重ね描き
        virtual void OnRenderScene();

        //! @brief プロジェクト既定値からシーンの描画設定を作る。登録された平行光があれば照明を上書きする
        //! 登録が無ければプロジェクト既定値がそのまま残る
        [[nodiscard]] NS::Gfx::RenderSettings ResolveSceneSettings(const NS::Gfx::RenderSettings& projectDefaults);

        //! @brief 配置物の組み直し・当たりの張り直しの後に呼ばれる。派生は live への参照をここで取り直す
        virtual void OnObjectsRebuilt() {}

        //! @brief 渡されたシーンデータから配置物と当たりの body を組み直す。データはその場限りの一時データ
        void RebuildObjectsFrom(const SceneData& data);

    private:
        //! 配置物の変化を一時オブジェクトへ知らせる。組み直しと当たりの張り直しの後に呼ぶ
        void NotifyTransientsObjectsRebuilt();

        //! 実行時に入れた配置物の資産を引き当ててから開始する
        void StartSpawned(GameObject& obj);

        SceneRenderer m_sceneRenderer;

        //! 衝突判定の PhysicsScene。当たりの有る scene だけ ObjectList::SyncPhysics が body を入れ、無ければ空のまま
        //! m_objects より前に宣言してあるので破棄は後になり、これを借りる移動の Component より長く生きる
        NS::Phys::PhysicsScene m_physicsScene;

        NS::Obj::ObjectList m_objects;           // 配置物の一覧
        NS::Obj::CameraBrain* m_brain = nullptr; // 常駐するカメラ一時オブジェクトの brain。所有は m_objects、これは控え
        SceneEnvironment m_environment;          // シーンの環境値。実体側の唯一の出所

        AssetManager* m_assets = nullptr; // AssetManager、非所有。未設定なら参照の実体化を跳ばす

        SceneData m_playBaseline;            // プレイ突入時の凍結スナップショット
        bool m_playBaselineInjected = false; // テスト注入の凍結を捕捉で潰さないための印

        bool m_simulationEnabled = true;         // 世界を回すか。エディタの編集モードだけが下ろす
        bool m_simulationPaused = false;         // 時間停止中か
        std::int32_t m_simulationStepFrames = 0; // コマ送り残り fixed step 数。止めたままこの数だけ進める
    };
} // namespace NS::Obj
