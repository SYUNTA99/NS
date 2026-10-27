#pragma once

#include "NSlib/Core/NonCopyable.h"
#include "NSlib/CoreObject/ActorInitInfo.h"
#include "NSlib/CoreObject/ActorList.h"
#include "NSlib/CoreObject/IGet/IGetCamera.h"
#include "NSlib/CoreObject/IGet/IGetCollision.h"
#include "NSlib/CoreObject/IGet/IGetRenderer.h"
#include "NSlib/CoreObject/IGet/IGetSceneSystem.h"
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
#include "NSlib/CoreObject/UpdatePhase.h"

#include "NSlib/Core/Types.h"
#include <memory>
#include <string>
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
    class IRenderable;
    class UIActor;

    //! アクターと共有システムを所有し、固定更新と描画を行うシーン
    //! JSON は保存・凍結・編集履歴に使い、実行中の状態は SceneActor と部品が持つ
    class Scene : public NonCopyable,
                  public IGetCamera,
                  public IGetSceneSystem,
                  public IGetCollision,
                  public IGetRenderer
    {
    public:
        Scene();
        virtual ~Scene();

        //! 可変フレーム Render の入口。標準のシーン描画パスを 1 回回す
        //! 世界が回っている間だけ、描く前に CameraManager が実カメラを 1 回書く
        void OnRender();

        //! 描く物・重ね描き・平行光の管理クラス
        //! アクターが OnInit で InitDraw・InitOverlay・InitLight を呼んで入れ、OnEndPlay で外す。
        //! 出る・消える時は、入れたまま組の有効と無効を切り替える
        [[nodiscard]] SceneRenderer* GetSceneRenderer() const noexcept override;

        //! 描く物・重ね描き・平行光の登録簿に入っている数。無効の組も数える
        [[nodiscard]] std::size_t RenderableCount() const noexcept { return m_sceneRenderer.RenderableCount(); }
        [[nodiscard]] std::size_t OverlayCount() const noexcept { return m_sceneRenderer.OverlayCount(); }
        [[nodiscard]] std::size_t LightCount() const noexcept { return m_sceneRenderer.LightCount(); }

        //! 描く物・重ね描き・平行光の登録簿のうち、有効な組の数
        [[nodiscard]] std::size_t EnabledRenderableCount() const noexcept { return m_sceneRenderer.EnabledRenderableCount(); }
        [[nodiscard]] std::size_t EnabledOverlayCount() const noexcept { return m_sceneRenderer.EnabledOverlayCount(); }
        [[nodiscard]] std::size_t EnabledLightCount() const noexcept { return m_sceneRenderer.EnabledLightCount(); }

        //! 登録中の描く物。登録順、非所有
        [[nodiscard]] std::vector<IRenderable*> Renderables() const { return m_sceneRenderer.Renderables(); }

        [[nodiscard]] CameraManager* GetCameraManager() const noexcept override;

        //! カメラ管理が描画に使う実カメラ
        [[nodiscard]] SceneCamera* MainCamera() noexcept;

        [[nodiscard]] SceneSystemHolder* GetSceneSystemHolder() const noexcept override;

        //! Physics() と同じ物を、IGetCollision の形 (ポインタ) で返す
        [[nodiscard]] Phys::PhysicsScene* GetPhysicsScene() const noexcept override;

        //! エフェクトの EffectScene。描画を持たないシーン (試し) では nullptr
        [[nodiscard]] Gfx::EffectScene* GetEffectScene() const noexcept;

        //! シーンのヒットセンサーの一覧。HitSensor は持ち主の SceneActor が宣言から作る時 (OnInit の前) に入れ、OnEndPlay で出す
        //! 持ち主が消えている間は入ったまま、HitSensor::IsValid が偽になる
        [[nodiscard]] HitSensorDirector& HitSensors() noexcept { return m_hitSensors; }
        [[nodiscard]] const HitSensorDirector& HitSensors() const noexcept { return m_hitSensors; }

        //! シーン全体の暗転。UI の段で進め、全 UI の後に描く。時間停止中は凍る
        [[nodiscard]] Gfx::ScreenFade& Fade() noexcept { return m_fade.Fade(); }
        [[nodiscard]] const Gfx::ScreenFade& Fade() const noexcept { return m_fade.Fade(); }

        //! 管理役を段 phase の更新へ登録する。登録済みなら段だけを差し替える
        //! その段のアクター (Actors の Update・RenderPrep の PrepareRender) の後に登録順で OnTick が呼ばれる
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

        //! 次に描くフレームから使う床の波を書く。強さ 0 で消える
        void SetGroundWave(const Gfx::GroundWave& wave) noexcept { m_sceneRenderer.SetGroundWave(wave); }
        //! 描く時に使う床の波
        [[nodiscard]] const Gfx::GroundWave& GroundWaveShown() const noexcept
        {
            return m_sceneRenderer.GroundWaveShown();
        }

        //! 次に描くフレームから使う歪みの輪を書く。押し 0 で消える
        void SetDistortionRing(const Gfx::DistortionRing& ring) noexcept
        {
            m_sceneRenderer.SetDistortionRing(ring);
        }
        //! 描く時に使う歪みの輪
        [[nodiscard]] const Gfx::DistortionRing& DistortionRingShown() const noexcept
        {
            return m_sceneRenderer.DistortionRingShown();
        }

        //! 体の型に写す描く物を差し替える。空なら型を描かない
        //! @param[in] renderables 型に写す描く物、非所有。登録を外した物は型からも外れる
        void SetBodyMask(std::vector<IRenderable*> renderables) noexcept
        {
            m_sceneRenderer.SetBodyMask(std::move(renderables));
        }
        //! ビュー列の index 番目に、今のフレームで描いた体の型を返す
        //! @return 1 色 8 ビットの型。未描画なら空
        [[nodiscard]] const Gfx::D3DTexture* BodyMaskShown(std::size_t viewIndex) const noexcept
        {
            return m_sceneRenderer.BodyMaskShown(viewIndex);
        }

        //! エフェクトを探すディレクトリを SceneRenderer へ渡す。空のままなら ContentRoot の Assets/Effects
        //! effectRoot は EffectScene の構築時に固まる。SetDevice より前に差す
        void SetEffectRoot(std::string root) noexcept { m_sceneRenderer.SetEffectRoot(std::move(root)); }

        //! skybox cubemap のディレクトリまたは .dds の ContentRoot 配下相対パス。空なら skybox を描かない
        //! 描画が毎フレーム読む。保存は ToJson がここから写す
        [[nodiscard]] const std::string& SkyboxPath() const noexcept { return m_environment.skybox; }

        [[nodiscard]] const SceneEnvironment& Environment() const noexcept { return m_environment; }
        //! environment を丸ごと替える
        void SetEnvironment(const SceneEnvironment& environment) noexcept;

        //! シーンのアクターの一覧
        [[nodiscard]] ActorList& Actors() noexcept { return m_actors; }
        [[nodiscard]] const ActorList& Actors() const noexcept { return m_actors; }

        //! シーンの JSON 文書を取り込みアクターを組み直す。文書は取込後に用済みになる
        //! 組む前に id と名前を一意に揃える。前の世界の固定ステップで積んだ開発用の図形は捨てる
        void ResetFromJson(nlohmann::json scene);

        //! シーンが自分を JSON 文書へ書き出す。保存と凍結の出所を実体に一本化する
        //! 全アクターを、全部品の値まで忠実に写す
        [[nodiscard]] nlohmann::json ToJson() const;

        //! シミュレーションを切り替え、一時停止とコマ送りを解除する
        //! 無効中はカメラも更新せず、部品の有効状態には反映しない
        void SetSimulationEnabled(bool enabled) noexcept;
        [[nodiscard]] bool IsSimulationEnabled() const noexcept { return m_simulationEnabled; }

        //! 時間停止。snapshot だけ回し、描く補間の割合を 1 に留める
        //! 動きの一瞬を止めて観察できるようにする
        void SetSimulationPaused(bool paused) noexcept { m_simulationPaused = paused; }
        [[nodiscard]] bool IsSimulationPaused() const noexcept { return m_simulationPaused; }

        //! 止めたまま次の固定ステップを 1 コマだけ進める。動いていればまず止める
        void StepSimulation() noexcept;

        //! 世界の更新を間引いて減速する。0 で停止、1 で通常。実時間の段は毎歩更新する
        //! 非有限値は拒否し、0〜1 に収める。プレイ開始時に 1 へ戻す
        void SetWorldSpeed(float speed) noexcept;
        [[nodiscard]] float WorldSpeed() const noexcept { return m_worldSpeed; }

        //! fromSpeed 0〜1 から通常速度へ、実時間 realSeconds 秒で戻す。0 秒以下なら即時
        //! shape は経過割合から復帰割合への曲線で、空なら直線。速度設定やシミュレーション切替で解除する
        void StartWorldSpeedRamp(float fromSpeed, float realSeconds, const Curve& shape) noexcept;

        //! holder 以外を steps 歩止める。止めている間に Update するアクターは holder だけ
        //! 回す管理役は RunsWhileOthersHeld の表で決まる。呼んだ歩の残りから効き、その歩も数える。
        //! 同じ holder の頼みは延長だけを受け、steps が 0 以下なら解く。止めている間は先に止め始めたアクターが止めを持ち、
        //! ほかのアクターの頼み (延長も解くのも) は受けない。holder は番地と id で覚えて比べるだけなので、止めている間に
        //! holder が一覧から外れても番地を読まず、止めは残りの歩で切れる。プレイ開始でも解く
        //! @param[in] holder 止め始めるアクター。事前条件: nullptr でない
        void HoldOthers(const SceneActor* holder, int steps) noexcept;
        //! 止め始めたアクターに関わらず、ほかを止めているのを解く。アクターを出し直す側が、出し直す前に呼ぶ
        void ReleaseOthersHold() noexcept { m_othersHeldSteps = 0; }
        //! ほかを止めている場合 true、それ以外の場合は false
        [[nodiscard]] bool IsHoldingOthers() const noexcept { return m_othersHeldSteps > 0; }
        //! actor が今の止めを始めたアクターなら true
        [[nodiscard]] bool IsOthersHolder(const SceneActor* actor) const noexcept;

        //! 減速中の経過に合わせた補間割合 0〜1。停止中は絵の変動を避けるため 1
        [[nodiscard]] float RenderAlpha(float frameAlpha) const noexcept;

        //! 1 フレームで描くビュー列を差す。空なら現描画先へ CameraManager の視点で 1 回だけ描く
        //! 空でない間は各ビューを順に bind して描き分ける。出荷 (Editor 無し) では常に空
        void SetSceneViews(std::vector<SceneView> views) noexcept { m_sceneRenderer.SetSceneViews(std::move(views)); }

        //! 型 T のアクターをシーンの中で作って入れ、開始する。新しい永続 id と名前を振る
        //! 所有はシーンが握り、呼出側へは生ポインタだけ返す。名前は既存と重なれば番号を付ける。
        //! 値はクラスと同じ名前の .actor の値で、保存と凍結に写る
        template <class T, class... Args> T* SpawnActor(std::string name, Args&&... args)
        {
            const ActorInitInfo info{.scene = this};
            std::unique_ptr<T> obj = CreateActorWithInfo<T>(info, std::forward<Args>(args)...);
            T* raw = obj.get();
            AdoptActor(std::move(obj), std::move(name));
            return raw;
        }

        //! アクターの JSON から 1 体を組んで入れる。id と名前は JSON のまま使う
        //! 名前は既存と重なれば番号を付ける。資産を引き当ててから開始し、出ていれば当たりも入る
        //! undo の作り直しと複製が通る。.actor が見つからなければ、読む時と同じく JSON のまま持って nullptr を返す
        SceneActor* SpawnFromJson(const nlohmann::json& actorJson);

        //! 同じ id のアクターへ JSON の姿を当てる。居なければ SpawnFromJson と同じ。動いた当たりは次の歩で揃う
        //! .actor が替わっても同じクラスならアクターを使い回し、替えた先の .actor の全部の値とシーンの上書きを
        //! 書いてから、束・動作・素材を読み直す。替えた先の .actor が違うクラスなら ERROR を出し、何も変えずに nullptr を返す。
        //! 替えた先の .actor が見つからない時と重ねるのに失敗した時だけ作り直し、古いアクターを解放せずに外して
        //! replaced へ渡す。replaced を捨てる時は、呼び手が先に OnEndPlay を呼ぶ
        //! @param[out] replaced 作り直した時の古いアクター。作り直さなければ触らない。nullptr は渡さない
        SceneActor* ApplyFromJson(const nlohmann::json& actorJson, std::unique_ptr<SceneActor>* replaced);

        //! アクターを 1 体消す。居なければ何もしない
        //! 当たり箱も揃うので、走っている世界を止めずに消せる。.actor が見つからずに持っている同じ id の JSON も捨てる
        void RemoveActor(uint32 objectId);

        //! アクターを 1 体、解放せずにシーンから外して返す。.actor が見つからずに持っている同じ id の JSON は捨てる
        //! Disappear で登録の組が無効になるが、OnEndPlay は呼ばない。返した物を捨てる時は、呼び手が先に
        //! OnEndPlay を呼ぶ。居なければ空を返す。段の更新の最中は断る
        [[nodiscard]] DetachedActor DetachActor(uint32 objectId);

        //! DetachActor で外したアクターを外す前の並びの位置へ戻す。外す前に出ていた物は Appear する
        SceneActor* ReattachActor(DetachedActor detached);

        //! 固定更新の並びを UpdatePhase の順に実行する。アクターを回す段では、アクターの後に管理役を更新する
        void OnUpdate();

        //! 段 phase を 1 回だけ回す。OnUpdate が段ごとに呼ぶ
        //! Actors は出ているアクターを並びの順に Update し (ほかを止めている間は止め始めたアクターだけ)、
        //! RenderPrep は全部の出ているアクターの PrepareRender を呼ぶ。続いてその段に登録した管理役を登録順に更新する
        //! 回している間はアクターを解放する口を断る。再入は禁止
        void ExecutePhase(UpdatePhase phase);

        //! アクターと、アクターから借りている物と、固定ステップで積んだ開発用の図形を捨てる
        void OnShutdown();

    private:
        //! 古い世界のエフェクトを消し、時間を進めずに描画へ反映する
        void ClearEffects() noexcept;

        //! 世界の速さを溜めに足し、1 に届いたら 1 引く。世界を進める歩の場合 true、それ以外の場合は false
        bool AdvanceWorldClock() noexcept;

        //! 戻りの曲線を実時間の 1 歩だけ進め、世界の速さを書く
        void AdvanceWorldSpeedRamp() noexcept;

        //! 渡されたシーンの JSON 文書からアクターと当たりの body を組み直す
        void RebuildSceneFrom(const nlohmann::json& scene);

        //! シーンに 1 つの物・暗転・アクター・エフェクト・開発用の図形を捨てる。組み直しと終わりが呼ぶ
        void ClearWorld();

        //! アクターの JSON 1 件からアクターを作る。組み直しと 1 体だけ入れる道の全部が通る
        //! .actor が見つからなければ作らず、JSON を m_unresolvedActors に id ごとに 1 つ持つ。
        //! 作れた時は同じ id の持っていた JSON を捨てる
        [[nodiscard]] std::unique_ptr<SceneActor> BuildOrKeepUnresolved(const nlohmann::json& actorJson);

        //! ApplyFromJson の作り直す道。同じ id のアクターを外して replaced へ渡し、同じ並びの位置へ JSON から作って入れる
        SceneActor* RebuildActorFromJson(const nlohmann::json& snapshot, std::unique_ptr<SceneActor>* replaced);

        // SpawnActor の中身。外から個体を受けると、OnInit を通っていない個体が入る
        SceneActor* AdoptActor(std::unique_ptr<SceneActor> obj, std::string name);

        //! 実行時に入れたアクターの資産を引き当ててから開始する
        void StartSpawned(SceneActor* obj);

        //! ほかを止めていないか、actor が止め始めたアクターなら true
        [[nodiscard]] bool UpdatesWhileHeld(const SceneActor* actor) const noexcept;

        // 描く物は持ち主の OnEndPlay で外れるので、アクターより先に宣言して後に破棄する
        SceneRenderer m_sceneRenderer;
        // m_sceneObjs が持つ CourseDirector が破棄で暗転を取り消すので、m_sceneObjs より先に宣言して後に破棄する
        ScreenFadeDirector m_fade;
        // アクターが持つ画面は破棄で自分を外すので、アクターより先に宣言して後に破棄する
        UIDirector m_ui;

        //! 衝突判定の PhysicsScene を持つ。Collision が出る時と動いた歩に body を入れる
        //! Collision が持ち主の OnDisappear で body を外し、Collider が借りるので、m_actors より先に宣言して後に破棄する
        PhysicsDirector m_physics;
        // skybox と重力の向き。実体側の唯一の出所で、描画が毎フレーム読み、保存は ToJson がここから写す
        SceneEnvironment m_environment;

        // ヒットセンサーの調べ役。アクターの部品が持ち主の OnEndPlay で外れるので、アクターより先に宣言して後に破棄する
        HitSensorDirector m_hitSensors;
        SceneCamera m_mainCamera;
        // 仮想カメラは持ち主の OnEndPlay で外れるので、アクターより先に宣言して後に破棄する
        CameraManager m_cameraManager;
        ActorList m_actors; // アクターの一覧
        // .actor が見つからず作らなかったアクターの JSON。id ごとに 1 つ、持った順に並べ、保存ではアクターの後に書く。
        // id は、読む時は m_actors の次の id が数え、1 体だけ入れる道ではこのシーンの控えの id なので数え済み。
        // 新しいアクターに振られない
        std::vector<nlohmann::json> m_unresolvedActors;
        // 更新へ登録した管理役 1 件
        struct SystemEntry
        {
            SceneSystem* system = nullptr; // 非所有。実体はこのシーンのメンバーか保持体が持つ
            UpdatePhase phase = UpdatePhase::Actors;
        };
        std::vector<SystemEntry> m_systems;           // このシーンが回す管理役と段。登録順
        std::vector<SceneSystem*> m_scheduledSystems; // ExecutePhase がその段で呼ぶ管理役を積む作業用の並び
        // シーンに 1 つの物。アクターと画面の一覧を借りるので、それより後に宣言して先に破棄する
        // const の窓口からも作れるよう mutable にする。作っても見かけのシーンの状態は変わらない
        mutable SceneSystemHolder m_sceneObjs{*this};

        bool m_simulationEnabled = true;       // 世界を回すか。エディタの編集モードだけが下ろす
        bool m_simulationPaused = false;       // 時間停止中か
        int32 m_simulationStepFrames = 0;      // コマ送り残り固定ステップ数。止めたままこの数だけ進める
        float m_worldSpeed = 1.0f;             // 世界の速さ (0〜1)
        float m_worldCarry = 0.0f;             // 前に世界を進めてから足した速さ。1 に届いたら世界を 1 歩進める
        bool m_rampActive = false;             // 世界の速さを戻している間か
        float m_rampFrom = 1.0f;               // 戻りの始まりの速さ
        float m_rampSeconds = 0.0f;            // 戻りの実時間の長さ (秒)
        float m_rampElapsed = 0.0f;            // 戻りを始めてからの実時間 (秒)
        Curve m_rampShape{};                   // 戻り方。点が無ければ直線
        int m_othersHeldSteps = 0;             // ほかを止める残りの歩数
        const SceneActor* m_othersHolder = nullptr; // 止め始めたアクター。消えた後もあり得るので比べるだけで読まない
        uint32 m_othersHolderId = k_NoActorId; // 止め始めたアクターの id。m_othersHeldSteps が正の間だけ読む
    };
} // namespace NS
