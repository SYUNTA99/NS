#pragma once

#include "Runtime/Core/NonCopyable.h"
#include "Runtime/Graphics/RenderSettings.h"
#include "Runtime/Object/Components/CameraComponent.h"
#include "Runtime/Object/Components/VirtualCamera.h"
#include "Runtime/Object/IUse/IUseCamera.h"
#include "Runtime/Object/IUse/IUseCollision.h"
#include "Runtime/Object/IUse/IUseEffect.h"
#include "Runtime/Object/IUse/IUseSceneObj.h"
#include "Runtime/Object/ObjectList.h"
#include "Runtime/Object/Scene/HitSensorDirector.h"
#include "Runtime/Object/Scene/SceneJson.h"
#include "Runtime/Object/Scene/SceneObjHolder.h"
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
    class CameraManager;
    class CameraComponent;
    class Component;
    class DirectionalLight;
    class IRenderable;
    class OverlayRenderer;
    class UIActor;

    //! @brief シーンの JSON 文書から組んだ ObjectList を駆動するシーン
    //! @details ObjectList と skybox を所有し、JSON
    //! 文書への書き出しと読み込み・プレイの凍結・標準のシーン描画パスを受け持つ Application から OnUpdate /
    //! OnRender / OnShutdown を順に呼び戻される 固定ステップの更新と可変フレームの描画で駆動し、IRenderable と
    //! OverlayRenderer の自己登録先も兼ねる live な Actor/Component が唯一の表現で、JSON は実体でない姿
    //! (ファイル・凍結・undo の控え) にだけ使う 配置物は TypeRegistry と ResolveAssets
    //! で自力で組む。寿命は SceneManager が unique_ptr
    //! で所有する 窓口 (カメラ・シーンに 1 つの物・地形の当たり・エフェクト) の出所。Actor と UIActor はここへ繋ぐ
    //! 依存: ObjectList / SceneJson / ObjectBuilder / TypeRegistry
    class Scene : public NS::Core::NonCopyable,
                  public IUseCamera,
                  public IUseSceneObj,
                  public IUseCollision,
                  public IUseEffect
    {
    public:
        Scene();
        virtual ~Scene();

        //! @brief 可変フレーム Render の入口。標準のシーン描画パスを 1 回回す
        void OnRender();

        //! IRenderable Component の自己登録。Model 等が OnStart で呼ぶ。二重登録は無視する
        //! 登録簿は SceneRenderer が持つ
        void RegisterRenderable(IRenderable* renderable);
        //! IRenderable Component の自己解除。Model 等が OnEndPlay で呼ぶ
        void UnregisterRenderable(IRenderable* renderable);

        //! OverlayRenderer の自己登録。基底の OnStart が呼ぶ。二重登録は無視する
        //! 並びは priority 昇順に保たれ、同値なら後から登録した方が後ろになる
        void RegisterOverlay(OverlayRenderer* overlay);
        //! OverlayRenderer の自己解除。基底の OnEndPlay が呼ぶ
        void UnregisterOverlay(OverlayRenderer* overlay);

        //! 平行光の自己登録。DirectionalLight が OnStart で呼ぶ。二重登録は無視する
        //! 並びは登録順。ResolveSceneSettings はこの順に読む
        void RegisterLight(DirectionalLight* light);
        //! 平行光の自己解除。DirectionalLight が OnEndPlay で呼ぶ
        void UnregisterLight(DirectionalLight* light);

        //! シーンに 1 つのカメラの管理役。シーンの破棄後は nullptr
        [[nodiscard]] CameraManager* GetCameraManager() const noexcept override;

        //! 管理役が駆動する実カメラ。シーンの破棄後は nullptr
        [[nodiscard]] CameraComponent* MainCamera() noexcept;

        //! シーンに 1 つの物の置き場
        [[nodiscard]] SceneObjHolder* GetSceneObjHolder() const noexcept override;

        //! 地形の当たりの PhysicsScene
        [[nodiscard]] NS::Phys::PhysicsScene* GetPhysicsScene() const noexcept override;

        //! エフェクトの EffectScene。描画を持たないシーン (試し) では nullptr
        [[nodiscard]] NS::Gfx::EffectScene* GetEffectScene() const noexcept override;

        //! シーンのヒットセンサーの調べ役。HitSensor が OnStart で入り OnEndPlay で出る
        [[nodiscard]] HitSensorDirector& HitSensors() noexcept { return m_hitSensors; }
        [[nodiscard]] const HitSensorDirector& HitSensors() const noexcept { return m_hitSensors; }

        //! 画面に出す物を一覧へ入れる。UIActor::Open が呼ぶ。二重登録は無視する
        void RegisterUIActor(UIActor* actor);
        //! 画面に出す物を一覧から外す。UIActor::Close が呼ぶ
        void UnregisterUIActor(UIActor* actor) noexcept;

        //! PhysicsScene への参照。Scene が値で持つので寿命は Scene と同じ
        [[nodiscard]] NS::Phys::PhysicsScene& Physics() noexcept { return m_physicsScene; }
        [[nodiscard]] const NS::Phys::PhysicsScene& Physics() const noexcept { return m_physicsScene; }

        //! シーンに 1 つの重力の向き。長さ 1 で、既定は -Y
        [[nodiscard]] const NS::Core::Vector3& GravityDirection() const noexcept { return m_gravityDirection; }
        //! @brief 重力の向きを差し替え、物理の重力も同じ向きにする
        //! @details NormalizeGravityDirection で長さ 1 に揃えてから使う
        void SetGravityDirection(const NS::Core::Vector3& direction) noexcept;

        //! AssetManager を非所有で差す。組み立て時の参照実体化が使う。未設定 (テスト等) は解決を跳ばす
        void SetAssets(AssetManager* assets) noexcept { m_assets = assets; }

        //! レンダラーを非所有で差す。OnRender が使う。未設定 (テスト等) は描かない
        void SetRenderer(NS::Gfx::Renderer* renderer) noexcept { m_sceneRenderer.SetRenderer(renderer); }

        //! エフェクトを探すディレクトリを SceneRenderer へ渡す。空のままなら ContentRoot の Assets/Effects
        //! effectRoot は EffectScene の構築時に固まる。SetRenderer より前に差す
        void SetEffectRoot(std::string root) noexcept { m_sceneRenderer.SetEffectRoot(std::move(root)); }

        //! @brief skybox cubemap のディレクトリまたは .dds の ContentRoot 配下相対パス。空なら skybox を描かない
        //! @details 描画が毎フレーム読む。保存は ToJson がここから写す
        [[nodiscard]] const std::string& SkyboxPath() const noexcept { return m_skyboxPath; }
        void SetSkyboxPath(std::string path) noexcept { m_skyboxPath = std::move(path); }

        //! @brief シーンの配置物の一覧
        [[nodiscard]] NS::Obj::ObjectList& Objects() noexcept { return m_objects; }
        [[nodiscard]] const NS::Obj::ObjectList& Objects() const noexcept { return m_objects; }

        //! @brief シーンの JSON 文書を取り込み配置物を組み直す。文書は取込後に用済みになる
        //! @details 組む前に id と名前を一意に揃える
        void LoadJson(nlohmann::json scene);

        //! @brief シーンが自分を JSON 文書へ書き出す。保存と凍結の出所を実体に一本化する
        //! @details 一時オブジェクトを除く全配置物を、全 component 値まで忠実に写す
        [[nodiscard]] nlohmann::json ToJson() const;

        //! @brief 編集で動いた live の当たりを張り直し、一時オブジェクトへ知らせる。object は作り直さない
        void SyncPhysics();

        //! @brief プレイ突入時に live を JSON 文書へ凍結して返す。プレイ規則の判定と編集復帰の姿はこの凍結を読む
        const nlohmann::json& BeginPlayBaseline();

        //! @brief 直近の凍結。プレイ中に限り意味を持つ
        [[nodiscard]] const nlohmann::json& PlayBaseline() const noexcept { return m_playBaseline; }

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

        //! @brief 時間停止。snapshot だけ回し、描く補間の割合を 1 に留める
        //! @details 動きの一瞬を止めて観察できるようにする
        void SetSimulationPaused(bool paused) noexcept { m_simulationPaused = paused; }
        [[nodiscard]] bool IsSimulationPaused() const noexcept { return m_simulationPaused; }

        //! @brief 止めたまま次の fixed step を 1 コマだけ進める。動いていればまず止める
        void StepSimulation() noexcept;

        //! @brief 世界が実際に進んだ固定ステップの数を返す
        //! @details OnUpdate が段を回した回数。編集モードと一時停止で段を回さずに戻った回は数えない
        //! 単調に増え、組み直しでも戻らない。前に読んだ値と比べて、その間に世界が進んだかを知る口
        //! @return 段を回した固定ステップの累計
        [[nodiscard]] std::uint64_t SimulationStepCount() const noexcept { return m_simulationStepCount; }

        //! @brief 1 フレームで描くビュー列を差す。空なら現描画先へ CameraManager の視点で 1 回だけ描く
        //! @details 空でない間は各ビューを順に bind して描き分ける。出荷 (Editor 無し) では常に空
        void SetSceneViews(std::vector<SceneView> views) noexcept { m_sceneRenderer.SetSceneViews(std::move(views)); }

        //! @brief 型 T の一時オブジェクトをシーンの中で作って入れる。呼出側へは生ポインタだけ返す
        //! @details 所有はシーンが握る。印立てと開始は unique_ptr を取る SpawnTransient が行う
        template <class T, class... Args> T* SpawnTransient(Args&&... args)
        {
            std::unique_ptr<T> obj = std::make_unique<T>(std::forward<Args>(args)...);
            T* raw = obj.get();
            SpawnTransient(std::unique_ptr<Actor>{std::move(obj)});
            return raw;
        }

        //! @brief 実行時の一時オブジェクトを ObjectList へ入れる。一時オブジェクトの印はここで立てる
        //! @details 保存・凍結に写らず、データからの組み直し後も残る。更新は配置物と同じく自分の段で回る
        //! 型が実行時にしか決まらない時の受け口。型が分かっているなら SpawnTransient<T> を使う
        Actor* SpawnTransient(std::unique_ptr<Actor> obj);

        //! @brief 実行時に配置物を 1 体入れる。新しい永続 id と名前を振る。名前は既存と重なれば番号を付ける
        //! @details 一時オブジェクトと違い保存に写り、データからの組み直しで他の配置物と一緒に消える
        Actor* SpawnObject(std::unique_ptr<Actor> obj, std::string name);

        //! @brief 配置物の JSON から 1 体を組んで入れる。id と名前と component の id は JSON のまま使う
        //! @details 名前は既存と重なれば番号を付ける。親の id があれば親へぶら下げ、資産を引き当ててから開始する
        //! undo の作り直しと複製が通る。当たりは呼出側が SyncPhysics で張り直す
        Actor* SpawnFromJson(const nlohmann::json& object);

        //! @brief 同じ id の配置物を JSON の姿へ作り直す。並びの位置と子の親子は保つ。居なければ SpawnFromJson と同じ
        //! @details component の増減も含めて姿を丸ごと入れ替える。当たりは呼出側が SyncPhysics で張り直す
        Actor* ReplaceFromJson(const nlohmann::json& object);

        //! @brief 同じ id の配置物へ JSON の姿を書き戻す。undo / redo が通る
        //! @details クラスと component の構成 (並び・型・id) が同じなら実体はそのまま残し、名前・有効・親・
        //! component の名前と有効と値だけを写す。値の変わった component だけ資産を引き直す
        //! 構成が違えば ReplaceFromJson で作り直し、居なければ SpawnFromJson で置く
        //! 当たりは呼出側が SyncPhysics で張り直す
        Actor* ApplyFromJson(const nlohmann::json& object);

        //! @brief 配置物を 1 体消す。居なければ何もしない
        //! @details 当たり箱も揃うので、走っている世界を止めずに消せる
        //! 子は根として残る。まとめて消したい呼び出し側が並びを決めて 1 体ずつ呼ぶ
        void DestroyObject(std::uint32_t objectId);

        //! 補間スナップショットの後に、UpdatePhase の段を表の順に 1 つずつ回す
        //! 世界の駆動はここが持つ
        //! 物理の段では、Jolt の 1 歩、その段に置いた物の順に呼ぶ
        //! Camera の段の後に CameraManager、UI の段の後に開いている UIActor、
        //! Effects の段の後にエフェクトの 1 フレームを進める
        //! 読み込んだら回り続けるのが既定で、止める口は SetSimulationEnabled / SetSimulationPaused
        void OnUpdate();

        //! 配置物と、配置物から借りている物を捨てる
        void OnShutdown();

    private:
        //! @brief 渡されたシーンの JSON 文書から配置物と当たりの body を組み直す
        void RebuildObjectsFrom(const nlohmann::json& scene);

        //! 配置物の変化を一時オブジェクトへ知らせる。組み直しと当たりの張り直しの後に呼ぶ
        void NotifyTransientsObjectsRebuilt();

        //! 実行時に入れた配置物の資産を引き当ててから開始する
        void StartSpawned(Actor& obj);

        SceneRenderer m_sceneRenderer;

        //! 衝突判定の PhysicsScene。当たりの有る scene だけ ObjectList::SyncPhysics が body を入れ、無ければ空のまま
        //! m_objects より前に宣言してあるので破棄は後になり、これを借りる移動の Component より長く生きる
        NS::Phys::PhysicsScene m_physicsScene;
        NS::Core::Vector3 m_gravityDirection{0.0f, -1.0f, 0.0f};

        // ヒットセンサーの調べ役。配置物の部品が OnEndPlay で外れるので、配置物より先に宣言して後に破棄する
        HitSensorDirector m_hitSensors;
        CameraComponent m_mainCamera;
        std::unique_ptr<NS::Obj::CameraManager> m_cameraManager;
        NS::Obj::ObjectList m_objects; // 配置物の一覧
        // シーンに 1 つの物。配置物と画面の一覧を借りるので、それより後に宣言して先に破棄する
        // const の窓口からも作れるよう mutable にする。作っても見かけのシーンの状態は変わらない
        mutable SceneObjHolder m_sceneObjs{*this};
        std::string m_skyboxPath; // skybox のパス。実体側の唯一の出所

        AssetManager* m_assets = nullptr; // AssetManager、非所有。未設定なら参照の実体化を跳ばす

        nlohmann::json m_playBaseline = MakeSceneJson(); // プレイ突入時の凍結

        bool m_simulationEnabled = true;         // 世界を回すか。エディタの編集モードだけが下ろす
        bool m_simulationPaused = false;         // 時間停止中か
        std::int32_t m_simulationStepFrames = 0; // コマ送り残り fixed step 数。止めたままこの数だけ進める
        std::uint64_t m_simulationStepCount = 0; // 段を回した固定ステップの累計
    };
} // namespace NS::Obj
