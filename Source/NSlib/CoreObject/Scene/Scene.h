#pragma once

#include "NSlib/Core/NonCopyable.h"
#include "NSlib/CoreObject/ActorInitInfo.h"
#include "NSlib/CoreObject/ActorList.h"
#include "NSlib/CoreObject/IGet/IGetCamera.h"
#include "NSlib/CoreObject/IGet/IGetCollision.h"
#include "NSlib/CoreObject/IGet/IGetRenderer.h"
#include "NSlib/CoreObject/Reflection/Curve.h"
#include "NSlib/CoreObject/Scene/SceneCamera.h"
#include "NSlib/CoreObject/Scene/SceneJson.h"
#include "NSlib/CoreObject/System/CameraManager.h"
#include "NSlib/CoreObject/System/HitScreenDirector.h"
#include "NSlib/CoreObject/System/HitSensorDirector.h"
#include "NSlib/CoreObject/System/PadRumbleDirector.h"
#include "NSlib/CoreObject/System/SceneRenderer.h"
#include "NSlib/CoreObject/System/UIDirector.h"
#include "NSlib/Gfx/Render/ScreenFade.h"
#include "NSlib/Phys/PhysicsScene.h"

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
} // namespace NS

namespace NS
{
    namespace Audio
    {
        class AudioDevice;
    } // namespace Audio
} // namespace NS

namespace NS
{
    class AssetManager;
    class Object;
    class UIActor;

    //! アクターと共有システムを所有し、固定更新と描画を行うシーン
    //! JSON は保存・凍結・編集履歴に使い、実行中の状態は SceneActor と部品が持つ
    class Scene : public NonCopyable,
                  public IGetCamera,
                  public IGetCollision,
                  public IGetRenderer
    {
    public:
        Scene();
        virtual ~Scene();

        //! 遊び始める。ResetFromJson の後にだけ呼ぶ。プレイを終えるのは次の ResetFromJson
        void StartPlay();

        //! 派生の型ごとのタグ。RTTI を切っているので、SceneCast はこれで型を見分ける。派生の無いシーンは nullptr
        //! 派生は static const void* StaticKind() を持ち、Kind で同じ値を返す
        [[nodiscard]] virtual const void* Kind() const noexcept { return nullptr; }

        //! 可変フレーム Render の入口。標準のシーン描画パスを 1 回回す
        //! 世界が回っている間だけ、描く前に CameraManager が実カメラを 1 回書く
        void OnRender();

        //! 描く物・重ね描き・平行光の管理クラス
        //! アクターが OnInit で InitDraw・InitOverlay・InitLight を呼んで入れ、OnEndPlay で外す。
        //! 出る・消える時は、入れたまま組の有効と無効を切り替える
        [[nodiscard]] SceneRenderer* GetSceneRenderer() const noexcept override;

        [[nodiscard]] CameraManager* GetCameraManager() const noexcept override;

        //! カメラ管理が描画に使う実カメラ
        [[nodiscard]] SceneCamera* MainCamera() noexcept;

        //! Physics() と同じ物を、IGetCollision の形 (ポインタ) で返す
        [[nodiscard]] Phys::PhysicsScene* GetPhysicsScene() const noexcept override;

        //! エフェクトの EffectScene。描画を持たないシーン (試し) では nullptr
        [[nodiscard]] Gfx::EffectScene* GetEffectScene() const noexcept;

        //! シーンのヒットセンサーの一覧。HitSensor は持ち主の SceneActor が宣言から作る時 (OnInit の前) に入れ、OnEndPlay で出す
        //! 持ち主が消えている間は入ったまま、HitSensor::IsValid が偽になる
        [[nodiscard]] HitSensorDirector& HitSensors() noexcept { return m_hitSensors; }
        [[nodiscard]] const HitSensorDirector& HitSensors() const noexcept { return m_hitSensors; }

        //! シーン全体の暗転。UI の後に実時間で進め、全 UI の後に描く。時間停止中は凍る
        [[nodiscard]] Gfx::ScreenFade& Fade() noexcept { return m_fade; }
        [[nodiscard]] const Gfx::ScreenFade& Fade() const noexcept { return m_fade; }

        //! 開いている画面の管理役。UIActor の Open と Close が出し入れする
        [[nodiscard]] UIDirector& UI() noexcept { return m_ui; }
        [[nodiscard]] const UIDirector& UI() const noexcept { return m_ui; }

        //! 当たりの画面の演出 (白・震えの線・歪みの輪・床の波)。HitReaction が頼む
        [[nodiscard]] HitScreenDirector& HitScreen() noexcept { return m_hitScreen; }
        [[nodiscard]] const HitScreenDirector& HitScreen() const noexcept { return m_hitScreen; }

        //! パッドの振動。HitReaction が頼む
        [[nodiscard]] PadRumbleDirector& PadRumble() noexcept { return m_padRumble; }
        [[nodiscard]] const PadRumbleDirector& PadRumble() const noexcept { return m_padRumble; }

        //! PhysicsScene への参照。Scene が値で持つので寿命は Scene と同じ
        [[nodiscard]] Phys::PhysicsScene& Physics() noexcept { return m_physics; }
        [[nodiscard]] const Phys::PhysicsScene& Physics() const noexcept { return m_physics; }

        //! 音の装置を非所有で差す。PlaySound が使う。未設定 (テスト・当たりの下見) は鳴らさない
        void SetAudioDevice(Audio::AudioDevice* device) noexcept { m_audioDevice = device; }
        //! 音の装置。受け取っていなければ nullptr
        [[nodiscard]] Audio::AudioDevice* GetAudioDevice() const noexcept { return m_audioDevice; }

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

        //! 世界の更新を間引いて減速する。0 で停止、1 で通常。StepRealTime の物は毎歩更新する
        //! 非有限値は拒否し、0〜1 に収める。プレイ開始時に 1 へ戻す
        void SetWorldSpeed(float speed) noexcept;
        [[nodiscard]] float WorldSpeed() const noexcept { return m_worldSpeed; }

        //! fromSpeed 0〜1 から通常速度へ、実時間 realSeconds 秒で戻す。0 秒以下なら即時
        //! shape は経過割合から復帰割合への曲線で、空なら直線。速度設定やシミュレーション切替で解除する
        void StartWorldSpeedRamp(float fromSpeed, float realSeconds, const Curve& shape) noexcept;

        //! mover 以外のアクターを steps 歩止めるよう頼む。頼みを控えるだけで、呼んだ歩はそのまま最後まで進む。
        //! 次の OnUpdate の始めに効き、そこから世界を進める歩を steps 歩数える。止まっている間も全部のアクターを
        //! Update し、mover 以外は SceneActor::IsStopped が true になる。回す管理役は StepStopped が決める。
        //! 同じ mover の頼みは延長だけを受け、steps が 0 以下なら解く。止めている間と、同じ歩に先に止めを頼まれた間は、
        //! 先に頼んだアクターが止めを持ち、ほかのアクターの頼み (延長も解くのも) は受けない。mover は番地と id で
        //! 覚えて比べるだけなので、止めている間に一覧から外れても番地を読まず、止めは残りの歩で切れる。プレイ開始でも解く
        //! @param[in] mover 止め始めるアクター。事前条件: nullptr でない
        void RequestStopScene(const SceneActor* mover, int steps) noexcept;
        //! 止め始めたアクターに関わらず、止めと控えた頼みを今すぐ解く。アクターを出し直す側が、出し直す前に呼ぶ
        void CancelStopScene() noexcept;
        //! シーンが止まっている場合 true、それ以外の場合は false。控えただけの頼みでは true にならない
        [[nodiscard]] bool IsSceneStopped() const noexcept { return m_stopSceneSteps > 0; }
        //! シーンが止まっていて、actor が今の止めを始めたアクターなら true。nullptr は false
        [[nodiscard]] bool IsStopSceneMover(const SceneActor* actor) const noexcept;

        //! 減速中の経過に合わせた補間割合 0〜1。停止中は絵の変動を避けるため 1
        [[nodiscard]] float RenderAlpha(float frameAlpha) const noexcept;

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
        //! OnEndPlay を呼ぶ。居なければ空を返す。1 歩の更新の最中は断る
        [[nodiscard]] DetachedActor DetachActor(uint32 objectId);

        //! DetachActor で外したアクターを外す前の並びの位置へ戻す。外す前に出ていた物は Appear する
        SceneActor* ReattachActor(DetachedActor detached);

        //! 1 固定ステップを進める。始めに前の歩で頼まれた止めを効かせ、世界を進めない歩・シーンが止まっている歩・
        //! 普段の歩で StepRealTime・StepStopped・StepWorld を呼び分ける。止まっているかはこの歩の間変えない。
        //! 回している間はアクターを解放する口を断る。再入は禁止
        void OnUpdate();

        //! アクターと、アクターから借りている物と、固定ステップで積んだ開発用の図形を捨てる
        void OnShutdown();

    protected:
        //! StartPlay が 1 回のプレイにつき 1 回呼ぶ。派生はここでプレイを始める
        virtual void OnStartPlay() {}
        //! プレイ中のシーンを ResetFromJson が組み直す前に 1 回呼ぶ。派生はプレイの状態を戻す
        virtual void OnStopPlay() {}

        //! 世界を進める歩の、当たりの判定の後に 1 回呼ぶ。シーンが止まっている歩と世界を進めない歩では呼ばない。
        //! 同じ歩のアクターとセンサーが書いた知らせを読める
        virtual void StepGame() {}

    private:
        // 状態ごとの 1 歩。OnUpdate がどれか 1 つを選んで呼び、回す物と順番はこの 5 つが決める。中で止めを確かめない

        //! 普段の歩。アクター全部 → 物理 → 当たりの判定 → StepGame → StepView
        void StepWorld();
        //! シーンが止まっている歩。アクター全部 (止まったアクターは自分で状態機械と体を止める) → StepViewStopped。
        //! 物理・判定・StepGame・エフェクトは進めない
        void StepStopped();
        //! 普段の歩の見せる側。StepViewStopped → エフェクト
        void StepView();
        //! 止めている間も回す見せる側。カメラ → StepRealTime → 描く支度 → 画面の演出 → 振動。
        //! カメラまで止めると、進み続ける止め始めたアクターが画面の中で動いて見える
        void StepViewStopped();
        //! 世界の速さに依らず毎歩回す物。UI → 暗転。遅い世界で世界を進めない歩はこれだけを呼ぶ
        void StepRealTime();

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

        //! 前の歩で控えた止めの頼みを効かせる。OnUpdate の始めにだけ呼ぶ
        void ApplyStopSceneRequest() noexcept;

        // 描く物は持ち主の OnEndPlay で外れるので、アクターより先に宣言して後に破棄する
        SceneRenderer m_sceneRenderer;
        Audio::AudioDevice* m_audioDevice = nullptr; // 借りるだけ。持ち主は Application
        Gfx::ScreenFade m_fade;
        // アクターが持つ画面は破棄で自分を外すので、アクターより先に宣言して後に破棄する
        UIDirector m_ui;
        // 作る時に m_sceneRenderer へ重ね描きを入れ、破棄で外すので、m_sceneRenderer より後に宣言する。
        // アクターの HitReaction が頼むので、アクターより先に宣言して後に破棄する
        HitScreenDirector m_hitScreen{*this};
        PadRumbleDirector m_padRumble;

        //! 衝突判定の PhysicsScene。Collision が出る時と動いた歩に body を入れる
        //! Collision が持ち主の OnDisappear で body を外し、Collider が借りるので、m_actors より先に宣言して後に破棄する
        Phys::PhysicsScene m_physics;
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

        bool m_playing = false;                // StartPlay から次の ResetFromJson までの間か
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
        int m_stopSceneSteps = 0;             // シーンを止める残りの歩数
        const SceneActor* m_stopSceneMover = nullptr; // 止め始めたアクター。消えた後もあり得るので比べるだけで読まない
        uint32 m_stopSceneMoverId = k_NoActorId; // 止め始めたアクターの id。m_stopSceneSteps が正の間だけ読む
        // 控えた止めの頼み。次の OnUpdate の始めに効かせて消す。頼んだアクターは比べるだけで読まない
        bool m_stopSceneRequested = false;
        int m_requestedStopSceneSteps = 0;
        const SceneActor* m_stopSceneRequester = nullptr;
        uint32 m_stopSceneRequesterId = k_NoActorId;
    };

    //! scene が派生 T ならその T、違うか nullptr なら nullptr
    template <class T> [[nodiscard]] T* SceneCast(Scene* scene) noexcept
    {
        if (scene == nullptr || scene->Kind() != T::StaticKind())
        {
            return nullptr;
        }
        return static_cast<T*>(scene);
    }

    template <class T> [[nodiscard]] const T* SceneCast(const Scene* scene) noexcept
    {
        if (scene == nullptr || scene->Kind() != T::StaticKind())
        {
            return nullptr;
        }
        return static_cast<const T*>(scene);
    }
} // namespace NS
