#pragma once

#include "NSlib/Core/NonCopyable.h"
#include "NSlib/CoreObject/ActorInitInfo.h"
#include "NSlib/CoreObject/ActorList.h"
#include "NSlib/CoreObject/IUse/IUseCamera.h"
#include "NSlib/CoreObject/IUse/IUseCollision.h"
#include "NSlib/CoreObject/IUse/IUseSceneSystem.h"
#include "NSlib/CoreObject/Reflection/Curve.h"
#include "NSlib/CoreObject/Scene/SceneCamera.h"
#include "NSlib/CoreObject/Scene/SceneJson.h"
#include "NSlib/CoreObject/System/CameraManager.h"
#include "NSlib/CoreObject/System/HitSensorDirector.h"
#include "NSlib/CoreObject/System/PhysicsDirector.h"
#include "NSlib/CoreObject/System/SceneRenderer.h"
#include "NSlib/CoreObject/System/SceneSystemHolder.h"
#include "NSlib/CoreObject/System/ScreenFadeDirector.h"
#include "NSlib/CoreObject/System/UIDirector.h"

#include "NSlib/Core/Types.h"
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace NS
{
    namespace Gfx
    {
        class EffectScene;
    }
    namespace Gfx
    {
        class D3DDevice;
    }
} // namespace NS

namespace NS
{
    class AssetManager;
    class Object;
    struct ClassObjInfo;
    class DirectionalLight;
    class IRenderable;
    class IOverlay;
    class UIActor;

    //! @brief 配置物と共有システムを所有し、固定更新と描画を行うシーン
    //! @details JSON は保存・凍結・編集履歴に使い、実行中の状態は SceneActor と部品が持つ
    class Scene : public NonCopyable, public IUseCamera, public IUseSceneSystem, public IUseCollision
    {
    public:
        Scene();
        virtual ~Scene();

        //! @brief 可変フレーム Render の入口。標準のシーン描画パスを 1 回回す
        //! @details 世界が回っている間だけ、描く前に CameraManager が実カメラを 1 回書く
        void OnRender();

        //! 描く部品の登録。部品が自分の Register の中で呼ぶ。二重登録は無視する
        //! 基底の部品 (Model・Shadow) の Register は、持ち主の SceneActor が OnAppear で呼ぶ。登録簿は SceneRenderer が持つ
        void RegisterRenderable(IRenderable* renderable);
        //! 描く部品の解除。基底の部品の Unregister は、持ち主の SceneActor が OnDisappear で呼ぶ
        void UnregisterRenderable(IRenderable* renderable);

        //! 重ね描きの登録。部品は持ち主の SceneActor が OnStart で 1 回呼ぶ。二重登録は無視する
        //! 並びは OverlayOrder 昇順に保たれ、同値なら後から登録した方が後ろになる
        void RegisterOverlay(IOverlay* overlay);
        //! 重ね描きの解除。部品は持ち主の SceneActor が OnEndPlay で呼ぶ
        void UnregisterOverlay(IOverlay* overlay);

        //! 平行光の登録。持ち主の LightActor が OnStart で 1 回呼ぶ。二重登録は無視する
        //! 並びは登録順。ResolveSceneSettings はこの順に読み、持ち主が出ていない光は飛ばす
        void RegisterLight(DirectionalLight* light);
        //! 平行光の解除。持ち主の LightActor が OnEndPlay で呼ぶ
        void UnregisterLight(DirectionalLight* light);

        //! 描く物・重ね描き・平行光の登録簿に入っている数
        [[nodiscard]] std::size_t RenderableCount() const noexcept { return m_sceneRenderer.RenderableCount(); }
        [[nodiscard]] std::size_t OverlayCount() const noexcept { return m_sceneRenderer.OverlayCount(); }
        [[nodiscard]] std::size_t LightCount() const noexcept { return m_sceneRenderer.LightCount(); }

        [[nodiscard]] CameraManager* GetCameraManager() const noexcept override;

        //! カメラ管理が描画に使う実カメラ
        [[nodiscard]] SceneCamera* MainCamera() noexcept;

        [[nodiscard]] SceneSystemHolder* GetSceneSystemHolder() const noexcept override;

        //! Physics() と同じ物を、IUseCollision の形 (ポインタ) で返す
        [[nodiscard]] Phys::PhysicsScene* GetPhysicsScene() const noexcept override;

        //! エフェクトの EffectScene。描画を持たないシーン (試し) では nullptr
        [[nodiscard]] Gfx::EffectScene* GetEffectScene() const noexcept;

        //! シーンのヒットセンサーの一覧。HitSensor は持ち主の SceneActor の OnAppear で入り、OnDisappear で出る
        [[nodiscard]] HitSensorDirector& HitSensors() noexcept { return m_hitSensors; }
        [[nodiscard]] const HitSensorDirector& HitSensors() const noexcept { return m_hitSensors; }

        //! シーン全体の暗転。UI の段で進め、全 UI の後に描く。時間停止中は凍る
        [[nodiscard]] Gfx::ScreenFade& Fade() noexcept { return m_fade.Fade(); }
        [[nodiscard]] const Gfx::ScreenFade& Fade() const noexcept { return m_fade.Fade(); }

        //! @brief 管理役を段 phase の更新へ登録する。登録済みなら段だけを差し替える
        //! @details 同じ段の SceneActor の後に登録順で OnTick が呼ばれる
        //! 更新の最中に登録した物は、その歩の残りでは呼ばれない
        void AddSystemTick(SceneSystem& system, UpdatePhase phase);
        //! 管理役を更新から外す。更新の最中に外した物は、その歩の残りでは呼ばれない
        void RemoveSystemTick(SceneSystem& system) noexcept;

        //! 開いている画面の管理役。UIActor の Open と Close が出し入れする
        [[nodiscard]] UIDirector& UI() noexcept { return m_ui; }
        [[nodiscard]] const UIDirector& UI() const noexcept { return m_ui; }

        //! PhysicsScene への参照。Scene が値で持つので寿命は Scene と同じ
        [[nodiscard]] Phys::PhysicsScene& Physics() noexcept { return m_physics.Physics(); }
        [[nodiscard]] const Phys::PhysicsScene& Physics() const noexcept { return m_physics.Physics(); }

        //! 装置を非所有で差す。OnRender が使う。未設定 (テスト等) は描かない
        void SetDevice(Gfx::D3DDevice* device) noexcept { m_sceneRenderer.SetDevice(device); }

        //! @brief 次に描くフレームから使う床の波を書く。強さ 0 で消える
        void SetGroundWave(const Gfx::GroundWave& wave) noexcept { m_sceneRenderer.SetGroundWave(wave); }
        //! 描く時に使う床の波
        [[nodiscard]] const Gfx::GroundWave& GroundWaveShown() const noexcept
        {
            return m_sceneRenderer.GroundWaveShown();
        }

        //! @brief 次に描くフレームから使う歪みの輪を書く。押し 0 で消える
        void SetDistortionRing(const Gfx::DistortionRing& ring) noexcept
        {
            m_sceneRenderer.SetDistortionRing(ring);
        }
        //! 描く時に使う歪みの輪
        [[nodiscard]] const Gfx::DistortionRing& DistortionRingShown() const noexcept
        {
            return m_sceneRenderer.DistortionRingShown();
        }

        //! @brief 体の型に写す描く物を差し替える。空なら型を描かない
        //! @param[in] renderables 型に写す描く物、非所有。登録を外した物は型からも外れる
        void SetBodyMask(std::vector<IRenderable*> renderables) noexcept
        {
            m_sceneRenderer.SetBodyMask(std::move(renderables));
        }
        //! @brief ビュー列の index 番目に、今のフレームで描いた体の型を返す
        //! @return 1 色 8 ビットの型。未描画なら空
        [[nodiscard]] const Gfx::D3DTexture* BodyMaskShown(std::size_t viewIndex) const noexcept
        {
            return m_sceneRenderer.BodyMaskShown(viewIndex);
        }

        //! エフェクトを探すディレクトリを SceneRenderer へ渡す。空のままなら ContentRoot の Assets/Effects
        //! effectRoot は EffectScene の構築時に固まる。SetDevice より前に差す
        void SetEffectRoot(std::string root) noexcept { m_sceneRenderer.SetEffectRoot(std::move(root)); }

        //! @brief skybox cubemap のディレクトリまたは .dds の ContentRoot 配下相対パス。空なら skybox を描かない
        //! @details 描画が毎フレーム読む。保存は ToJson がここから写す
        [[nodiscard]] const std::string& SkyboxPath() const noexcept { return m_environment.skybox; }

        [[nodiscard]] const SceneEnvironment& Environment() const noexcept { return m_environment; }
        //! environment を丸ごと替える
        void SetEnvironment(const SceneEnvironment& environment) noexcept;

        //! @brief シーンの配置物の一覧
        [[nodiscard]] ActorList& Actors() noexcept { return m_actors; }
        [[nodiscard]] const ActorList& Actors() const noexcept { return m_actors; }

        //! @brief シーンの JSON 文書を取り込み配置物を組み直す。文書は取込後に用済みになる
        //! @details 組む前に id と名前を一意に揃える。前の世界の固定ステップで積んだ開発用の図形は捨てる
        void ResetFromJson(nlohmann::json scene);

        //! @brief シーンが自分を JSON 文書へ書き出す。保存と凍結の出所を実体に一本化する
        //! @details 全配置物を、全部品の値まで忠実に写す
        [[nodiscard]] nlohmann::json ToJson() const;

        //! @brief プレイ突入時に実体を JSON 文書へ凍結して返す。プレイ規則の判定と編集復帰の姿はこの凍結を読む
        const nlohmann::json& BeginPlayBaseline();

        //! @brief 直近の凍結。プレイ中に限り意味を持つ
        [[nodiscard]] const nlohmann::json& PlayBaseline() const noexcept { return m_playBaseline; }

        //! 手編集した欄だけを凍結へ写し、編集復帰後も残す。実行で動いた値は写さない
        //! row は actor のクラスの表の行。凍結にない個体、作られていない行、存在しない欄、.actor へ書く欄は変更しない
        void WritePlayBaselineField(const SceneActor& actor, const ClassObjInfo& row, std::string_view fieldName);

        //! シミュレーションを切り替え、一時停止とコマ送りを解除する
        //! 無効中はカメラも更新せず、部品の有効状態には反映しない
        void SetSimulationEnabled(bool enabled) noexcept;
        [[nodiscard]] bool IsSimulationEnabled() const noexcept { return m_simulationEnabled; }

        //! @brief 時間停止。snapshot だけ回し、描く補間の割合を 1 に留める
        //! @details 動きの一瞬を止めて観察できるようにする
        void SetSimulationPaused(bool paused) noexcept { m_simulationPaused = paused; }
        [[nodiscard]] bool IsSimulationPaused() const noexcept { return m_simulationPaused; }

        //! @brief 止めたまま次の固定ステップを 1 コマだけ進める。動いていればまず止める
        void StepSimulation() noexcept;

        //! 世界の更新を間引いて減速する。0 で停止、1 で通常。実時間の段は毎歩更新する
        //! 非有限値は拒否し、0〜1 に収める。プレイ開始時に 1 へ戻す
        void SetWorldSpeed(float speed) noexcept;
        [[nodiscard]] float WorldSpeed() const noexcept { return m_worldSpeed; }

        //! fromSpeed 0〜1 から通常速度へ、実時間 realSeconds 秒で戻す。0 秒以下なら即時
        //! shape は経過割合から復帰割合への曲線で、空なら直線。速度設定やシミュレーション切替で解除する
        void StartWorldSpeedRamp(float fromSpeed, float realSeconds, const Curve& shape) noexcept;

        //! 自機以外を steps 歩停止する。呼んだ歩の残りから効き、その歩も数える
        //! 残り時間は延長だけを受け付ける。0 以下またはプレイ開始で解除する
        void HoldOthers(int steps) noexcept;
        //! 自機以外の世界を止めている場合 true、それ以外の場合は false
        [[nodiscard]] bool IsHoldingOthers() const noexcept { return m_othersHeldSteps > 0; }

        //! 減速中の経過に合わせた補間割合 0〜1。停止中は絵の変動を避けるため 1
        [[nodiscard]] float RenderAlpha(float frameAlpha) const noexcept;

        //! @brief 1 フレームで描くビュー列を差す。空なら現描画先へ CameraManager の視点で 1 回だけ描く
        //! @details 空でない間は各ビューを順に bind して描き分ける。出荷 (Editor 無し) では常に空
        void SetSceneViews(std::vector<SceneView> views) noexcept { m_sceneRenderer.SetSceneViews(std::move(views)); }

        //! @brief 型 T の配置物をシーンの中で作って入れ、開始する。新しい永続 id と名前を振る
        //! @details 所有はシーンが握り、呼出側へは生ポインタだけ返す。名前は既存と重なれば番号を付ける。
        //! 値はクラスと同じ名前の .actor の値で、保存と凍結に写る
        template <class T, class... Args> T* SpawnActor(std::string name, Args&&... args)
        {
            const ActorInitInfo info{.scene = this};
            std::unique_ptr<T> obj = CreateActorWithInfo<T>(info, std::forward<Args>(args)...);
            T* raw = obj.get();
            AdoptActor(std::move(obj), std::move(name));
            return raw;
        }

        //! @brief 配置物の JSON から 1 体を組んで入れる。id と名前は JSON のまま使う
        //! @details 名前は既存と重なれば番号を付ける。資産を引き当ててから開始し、出ていれば当たりも入る
        //! undo の作り直しと複製が通る。.actor が見つからなければ、読む時と同じく JSON のまま持って nullptr を返す
        SceneActor* SpawnFromJson(const nlohmann::json& actorJson);

        //! @brief 同じ id の配置物を JSON の姿へ作り直す。並びの位置は保つ。居なければ SpawnFromJson と同じ
        //! @details 部品の増減も含めて姿を丸ごと入れ替える。.actor が見つからなければ、
        //! 前のアクターを外して JSON のまま持ち、nullptr を返す
        SceneActor* ReplaceFromJson(const nlohmann::json& actorJson);

        //! 同じ id の個体へ JSON を適用する。構成が同じなら実体を保ち、違えば再構築する
        //! 未配置なら生成する。動いた当たりは次の歩で揃う
        SceneActor* ApplyFromJson(const nlohmann::json& actorJson);

        //! @brief 配置物を 1 体消す。居なければ何もしない
        //! @details 当たり箱も揃うので、走っている世界を止めずに消せる。.actor が見つからずに持っている同じ id の JSON も捨てる
        void RemoveActor(uint32 objectId);

        //! 固定更新の段を順に実行する。同じ段では SceneActor の後にシステムを更新する
        void OnUpdate();

        //! @brief 段 phase を 1 回だけ回す。OnUpdate が段ごとに呼ぶ
        //! @details 有効な SceneActor を配置順に、続いてその段に登録した管理役を登録順に更新する
        //! 回している間は配置物を解放する口を断る。再入は禁止
        void ExecutePhase(UpdatePhase phase);

        //! 配置物と、配置物から借りている物と、固定ステップで積んだ開発用の図形を捨てる
        void OnShutdown();

    private:
        //! 古い世界のエフェクトを消し、時間を進めずに描画へ反映する
        void ClearEffects() noexcept;

        //! 世界の速さを溜めに足し、1 に届いたら 1 引く。世界を進める歩の場合 true、それ以外の場合は false
        bool AdvanceWorldClock() noexcept;

        //! 戻りの曲線を実時間の 1 歩だけ進め、世界の速さを書く
        void AdvanceWorldSpeedRamp() noexcept;

        //! @brief 渡されたシーンの JSON 文書から配置物と当たりの body を組み直す
        void RebuildSceneFrom(const nlohmann::json& scene);

        //! シーンに 1 つの物・暗転・配置物・エフェクト・開発用の図形を捨てる。組み直しと終わりが呼ぶ
        void ClearWorld();

        //! @brief 配置物の JSON 1 件からアクターを作る。組み直しと 1 体だけ入れる道の全部が通る
        //! @details .actor が見つからなければ作らず、JSON を m_unresolvedActors に id ごとに 1 つ持つ。
        //! 作れた時は同じ id の持っていた JSON を捨てる
        [[nodiscard]] std::unique_ptr<SceneActor> BuildOrKeepUnresolved(const nlohmann::json& actorJson);

        // SpawnActor の中身。外から個体を受けると、OnInit を通っていない個体が入る
        SceneActor* AdoptActor(std::unique_ptr<SceneActor> obj, std::string name);

        //! 実行時に入れた配置物の資産を引き当ててから開始する
        void StartSpawned(SceneActor& obj);

        SceneRenderer m_sceneRenderer;
        // m_sceneObjs が持つ CourseDirector が破棄で暗転を取り消すので、m_sceneObjs より先に宣言して後に破棄する
        ScreenFadeDirector m_fade;
        // 配置物が持つ画面は破棄で自分を外すので、配置物より先に宣言して後に破棄する
        UIDirector m_ui;

        //! 衝突判定の PhysicsScene を持つ。Collision が出る時と動いた歩に body を入れる
        //! Collision が OnEndPlay で body を外し、Collider が借りるので、m_objects より先に宣言して後に破棄する
        PhysicsDirector m_physics;
        // skybox と重力の向き。実体側の唯一の出所で、描画が毎フレーム読み、保存は ToJson がここから写す
        SceneEnvironment m_environment;

        // ヒットセンサーの調べ役。配置物の部品が OnEndPlay で外れるので、配置物より先に宣言して後に破棄する
        HitSensorDirector m_hitSensors;
        SceneCamera m_mainCamera;
        // 仮想カメラは持ち主の OnEndPlay で外れるので、配置物より先に宣言して後に破棄する
        CameraManager m_cameraManager;
        ActorList m_actors; // 配置物の一覧
        // .actor が見つからず作らなかった配置物の JSON。id ごとに 1 つ、持った順に並べ、保存ではアクターの後に書く。
        // id は、読む時は m_objects の次の id が数え、1 体だけ入れる道ではこのシーンの控えの id なので数え済み。
        // 新しいアクターに振られない
        std::vector<nlohmann::json> m_unresolvedActors;
        // 更新へ登録した管理役 1 件
        struct SystemEntry
        {
            SceneSystem* system = nullptr; // 非所有。実体はこのシーンのメンバーか保持体が持つ
            UpdatePhase phase = UpdatePhase::Triggers;
        };
        std::vector<SystemEntry> m_systems;           // このシーンが回す管理役と段。登録順
        std::vector<SceneSystem*> m_scheduledSystems; // ExecutePhase がその段で呼ぶ管理役を積む作業用の並び
        // シーンに 1 つの物。配置物と画面の一覧を借りるので、それより後に宣言して先に破棄する
        // const の窓口からも作れるよう mutable にする。作っても見かけのシーンの状態は変わらない
        mutable SceneSystemHolder m_sceneObjs{*this};

        nlohmann::json m_playBaseline = MakeSceneJson(); // プレイ突入時の凍結

        bool m_simulationEnabled = true;      // 世界を回すか。エディタの編集モードだけが下ろす
        bool m_simulationPaused = false;      // 時間停止中か
        int32 m_simulationStepFrames = 0; // コマ送り残り固定ステップ数。止めたままこの数だけ進める
        float m_worldSpeed = 1.0f;            // 世界の速さ (0〜1)
        float m_worldCarry = 0.0f;            // 前に世界を進めてから足した速さ。1 に届いたら世界を 1 歩進める
        bool m_rampActive = false;            // 世界の速さを戻している間か
        float m_rampFrom = 1.0f;              // 戻りの始まりの速さ
        float m_rampSeconds = 0.0f;           // 戻りの実時間の長さ (秒)
        float m_rampElapsed = 0.0f;           // 戻りを始めてからの実時間 (秒)
        Curve m_rampShape{};                  // 戻り方。点が無ければ直線
        int m_othersHeldSteps = 0;            // 自機以外の世界を止める残りの歩数
    };
} // namespace NS
