#pragma once

#include "NSlib/Core/Assert.h"
#include "NSlib/CoreObject/ClassObjs/StateMachine.h"
#include "NSlib/CoreObject/ClassObjs/Transform.h"
#include "NSlib/CoreObject/IUse/IUseCamera.h"
#include "NSlib/CoreObject/IUse/IUseCollision.h"
#include "NSlib/CoreObject/IUse/IUseEffect.h"
#include "NSlib/CoreObject/IUse/IUseSceneSystem.h"
#include "NSlib/CoreObject/IUse/IUseState.h"
#include "NSlib/CoreObject/Object.h"
#include "NSlib/CoreObject/StateMachineOf.h"
#include "NSlib/CoreObject/UpdatePhase.h"

#include "NSlib/Core/Types.h"
#include <concepts>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace NS
{
    class HitSensor;
    class Model;
    class Animation;
    class Shadow;
    class Collision;
    class EffectKeeper;
    class HitReaction;
    class ActionTimeline;
    class Message;
    class Scene;
    struct ActorInitInfo;

    //! @brief 世界に置く物の基底。Transform と部品を持ち、MapPartsActor / PropActor / PlayerActor などが派生する
    //! @details 所属シーンを持ち、IUse〜 の問いを居るシーンへ繋ぐ。シーンに居ない間の問いは nullptr を返す
    class SceneActor : public Object,
                       public IUseCamera,
                       public IUseSceneSystem,
                       public IUseCollision,
                       public IUseEffect,
                       public IUseState
    {
    public:
        SceneActor() noexcept;
        virtual ~SceneActor() noexcept;
        [[nodiscard]] uint32 Id() const noexcept { return m_id; }
        [[nodiscard]] const std::string& Name() const noexcept { return m_name; }
        //! 作る時に敷いた .actor の名前。型から作ったアクターはクラスと同じ名前で、その .actor が無くても変わらない
        [[nodiscard]] const std::string& ActorFileName() const noexcept { return m_actorFileName; }
        NS_REFLECT_DECLARE()

        //! @brief 世界に出す。更新の段・描画・当たり・センサーへ登録する。出ていれば何もしない
        //! @details Destroy の後に呼ぶのは呼び手のバグで、NS_ASSERT が止める。出荷版では何もしない
        void Appear();
        //! @brief 世界から外す。出ていなければ何もしない
        //! @details 解放はしないので Appear で戻せる
        void Disappear() noexcept;
        //! @brief 世界から外し、段を回し終えた後に解放させる。2 回目からは何もしない
        //! @details その場では解放しないので、同じ段の中では指したまま読める。やり直しの知らせは
        //! 届かず戻らないので、戻す物は Disappear を使う
        void Destroy() noexcept;
        //! この配置物がシーンに出ているか
        [[nodiscard]] bool IsAppeared() const noexcept { return m_presence == Presence::Appeared; }
        //! Destroy を呼ばれて、段の後の解放を待っている場合 true、それ以外の場合は false
        [[nodiscard]] bool IsPendingDestroy() const noexcept { return m_presence == Presence::PendingDestroy; }

        //! 入るシーン。作る時の ActorInitInfo::scene で、OnInit の前から決まっている。シーンへ入らない物は nullptr
        [[nodiscard]] Scene* OwningScene() const noexcept { return m_scene; }

        [[nodiscard]] CameraManager* GetCameraManager() const noexcept override;
        [[nodiscard]] SceneSystemHolder* GetSceneSystemHolder() const noexcept override;
        [[nodiscard]] Phys::PhysicsScene* GetPhysicsScene() const noexcept override;
        // 出したエフェクトを記録する部品。持たなければ nullptr
        [[nodiscard]] EffectKeeper* GetEffectKeeper() const noexcept override { return m_effectKeeper.get(); }

        //! 個体の位置・回転・大きさを持つ部品
        [[nodiscard]] Transform& Root() noexcept;
        [[nodiscard]] const Transform& Root() const noexcept;
        [[nodiscard]] IStateMachine* GetStateMachine() noexcept override;
        [[nodiscard]] const IStateMachine* GetStateMachine() const noexcept override;

        //! 重力の向き。シーンによらず真下で、Jolt の重力と同じ向き
        [[nodiscard]] Vector3 GravityDirection() const noexcept;
        //! 重力の向きへ加速する。strength は m/s²、dt は秒
        //! 非有限値または dt が 0 以下なら変更しない
        void AddGravity(Vector3& velocity, float strength, float dt) const noexcept;

        [[nodiscard]] Model* ModelClassObj() noexcept { return m_model.get(); }
        [[nodiscard]] const Model* ModelClassObj() const noexcept { return m_model.get(); }
        [[nodiscard]] Animation* AnimationClassObj() noexcept { return m_animation.get(); }
        [[nodiscard]] const Animation* AnimationClassObj() const noexcept { return m_animation.get(); }
        [[nodiscard]] Shadow* ShadowClassObj() noexcept { return m_shadow.get(); }
        [[nodiscard]] const Shadow* ShadowClassObj() const noexcept { return m_shadow.get(); }
        [[nodiscard]] Collision* CollisionClassObj() noexcept { return m_collision.get(); }
        [[nodiscard]] const Collision* CollisionClassObj() const noexcept { return m_collision.get(); }
        //! その物の体の広がりのセンサー。持たなければ nullptr
        [[nodiscard]] HitSensor* BodySensorClassObj() noexcept { return m_bodySensor.get(); }
        [[nodiscard]] const HitSensor* BodySensorClassObj() const noexcept { return m_bodySensor.get(); }
        //! @brief 体と別の広がりを持つ 2 つ目のセンサー。持たなければ nullptr
        //! @details 調べ役は体のセンサーと区別せずに扱う。今はこの枠を作る種類が無い
        [[nodiscard]] HitSensor* AttackSensorClassObj() noexcept { return m_attackSensor.get(); }
        [[nodiscard]] const HitSensor* AttackSensorClassObj() const noexcept { return m_attackSensor.get(); }
        [[nodiscard]] HitReaction* HitReactionClassObj() noexcept { return m_hitReaction.get(); }
        [[nodiscard]] const HitReaction* HitReactionClassObj() const noexcept { return m_hitReaction.get(); }
        [[nodiscard]] ActionTimeline* ActionTimelineClassObj() noexcept { return m_actionTimeline.get(); }
        [[nodiscard]] const ActionTimeline* ActionTimelineClassObj() const noexcept { return m_actionTimeline.get(); }

        //! @brief 当たりの止め (ヒットストップ) の残りを、frames が残りより長い時だけ延ばす
        //! @details 止めの間は Update が StateStep と BodyStep を飛ばす。呼んだフレームの残りの段から効き、
        //! そのフレームも数える。束を始める口 (HitReaction::Start) が束の止めの長さで呼ぶ
        void RequestHitStop(int frames) noexcept;
        //! @brief 当たりの止めの残りを frames に書き換える。短くもする
        //! @details 束を始めた後に、束と違う長さで止める当たり (貫通) が呼ぶ。事前条件: frames が 0 以上
        void OverrideHitStop(int frames) noexcept;
        //! 当たりの止めの残りのフレーム数。止めていなければ 0
        [[nodiscard]] int HitStopFramesRemaining() const noexcept { return m_hitStopFrames; }
        //! 当たりの止めの間なら true
        [[nodiscard]] bool IsHitStopping() const noexcept { return m_hitStopFrames > 0; }

        //! @brief Update を呼ばれる段を返す
        [[nodiscard]] virtual UpdatePhase Phase() const noexcept { return UpdatePhase::Triggers; }
        virtual void ReadInput() {}
        //! 観測・裁定・状態・身体・見た目を固定順で更新する。部品は各段で明示的に呼ぶ
        //! エフェクトの記録は観測前に、動作のタイムラインは状態の段の直後に進める。
        //! 当たりの止めの間は状態と身体の段を飛ばし、最後に止めの残りを 1 減らす
        void Update();
        virtual void PrepareRender();

        //! 全配置物の初期化後に一度呼ぶ。他の配置物を必要とする初期化を行う
        virtual void InitAfterPlacement() {}

        //! 異なる持ち主のセンサーが重なる間、互いに毎歩呼ばれる
        //! センサーの種類を判断し、必要なメッセージを送る
        virtual void AttackSensor(HitSensor& self, HitSensor& other)
        {
            (void)self;
            (void)other;
        }

        virtual void OnStart();
        //! 世界から外し、全ての部品の OnEndPlay を呼ぶ
        virtual void OnEndPlay();

        //! @brief 部品の素材の欄を引き直し、続けて OnAssetsLoaded を呼ぶ
        //! @details 作る時は Init が欄を書いた直後に呼ぶ。エディタやシーンが欄を書き換えた後も、これを呼んで読み直す
        void ReloadAssets();

    protected:
        //! @brief 知らせを受け取る。応じた場合 true、知らない知らせと応じなかった知らせは false
        //! @details 呼べるのは SendMsg と SendMsgToActor だけ。sender と receiver は知らせを運んだセンサーで、
        //! センサーを介さない知らせでは nullptr
        virtual bool ReceiveMsg(const Message& msg, HitSensor* sender, HitSensor* receiver)
        {
            (void)msg;
            (void)sender;
            (void)receiver;
            return false;
        }

        // 基底の部品の型を替える派生は、基底の OnInit を呼ばずに自分で作る
        //! 入るシーンはもう決まっているので、部品へシーンの仕組みを渡せる。id と名前はまだ付いていない
        virtual void OnInit() {}
        //! @brief 素材の欄を引いた直後に呼ぶ。読んだ物から作る物を、部品を名指しで呼んで作る
        //! @details 作る時と ReloadAssets の両方で呼ぶので、何回呼ばれても同じ姿になるように書く。上書きする派生は基底も呼ぶ
        virtual void OnAssetsLoaded();

        // 未登録・作成済みのメンバーや使用済みの保存キーは変更せず nullptr を返す
        template <class TBase, class T = TBase, class... TArgs>
            requires std::derived_from<T, TBase> && std::derived_from<T, Object>
        T* CreateClassObj(std::unique_ptr<TBase>& member, TArgs&&... args)
        {
            const ClassObjInfo* field = FindReflectedMember(&member);
            if (field == nullptr || member != nullptr || IsClassObjKeyCreated(field->key))
            {
                ReportInvalidClassObj(field);
                return nullptr;
            }
            std::unique_ptr<T> classObj = std::make_unique<T>(std::forward<TArgs>(args)...);
            T* raw = classObj.get();
            member = std::move(classObj);
            return raw;
        }

        virtual void OnAppear();
        virtual void OnDisappear() noexcept;
        //! 世界と相手を観測する段。状態・身体・見た目は変更しない
        virtual void ObserveStep() {}
        //! 観測結果からこの歩の値を決め、メッセージを送る段
        virtual void DecideStep() {}
        //! 状態機械を進める段。上書きは状態更新の前後に処理を挟む場合だけ
        virtual void StateStep();
        //! 身体を動かし、衝突形状を揃える段。仮想カメラもここで更新する
        virtual void BodyStep() {}
        //! 移動後の姿勢から見た目・演出・寿命を更新する段
        virtual void VisualStep() {}
        //! 自分を持ち主に、TStates の先頭へ入る状態機械を一度だけ構築する
        //! OnEnter が使う部品を先に用意する。outMachine は OnEnter 前に設定する
        //! 機械は基底の部品 StateMachine が持ち、型付きポインタは非所有。既に構築済みなら変更せず false
        template <typename TOwner, typename... TStates>
        bool BuildStateMachine(TOwner& owner, StateMachineOf<TOwner>*& outMachine)
        {
            static_assert(std::is_base_of_v<SceneActor, TOwner>, "持ち主は SceneActor の派生");
            NS_ASSERT(Scene,
                      static_cast<const SceneActor*>(&owner) == this,
                      "SceneActor::BuildStateMachine: 持ち主に自分以外の SceneActor を渡している");
            StateMachine* part = CreateClassObj(m_stateMachine, this);
            if (part == nullptr)
            {
                return false;
            }
            std::unique_ptr<StateMachineOf<TOwner>> machine = std::make_unique<StateMachineOf<TOwner>>();
            StateMachineOf<TOwner>* built = machine.get();
            part->Hold(std::move(machine));
            outMachine = built;
            built->template Build<TStates...>(owner);
            return true;
        }

    private:
        friend class ActorList;
        friend bool SendMsg(const Message& msg, HitSensor& receiver, HitSensor* sender);
        friend bool SendMsgToActor(const Message& msg, SceneActor& receiver);
        template <class T, class... TArgs> friend std::unique_ptr<T> CreateActor(TArgs&&... args);
        template <class T, class... TArgs>
        friend std::unique_ptr<T> CreateActorWithInfo(const ActorInitInfo& info, TArgs&&... args);
        friend std::unique_ptr<SceneActor> CreateActorOfClass(std::string_view className);
        friend std::unique_ptr<SceneActor> CreateActorOfClass(std::string_view className, const ActorInitInfo& info);
        friend float GetFrameRate(const SceneActor* actor) noexcept;
        friend bool SetFrameRate(SceneActor* actor, float rate) noexcept;
        //! @brief アクターを作る順。部品を作る → .actor → 配置の上書き → 素材
        //! @details 呼ぶのは作る関数だけで、作った直後に 1 回。private にして 2 回目を起こさない。
        //! info が nullptr のアクターも、配置の上書きを飛ばすだけで同じ順を通る
        void Init(const ActorInitInfo* info);
        //! @brief 補間の前の値として、根の Transform と Model の今の値を控える
        //! @details ActorList::SnapshotObjects が固定ステップの頭で全ての SceneActor
        //! に呼ぶ。世界を止めている間も呼ばれる
        void SnapshotForInterpolation() noexcept;
        void SetId(uint32 id) noexcept { m_id = id; }
        void SetName(std::string name) noexcept { m_name = std::move(name); }
        // member を指すアクターの表の行。表に無ければ nullptr
        [[nodiscard]] const ClassObjInfo* FindReflectedMember(const void* member) const;
        // 別のメンバーが同じ保存キーで作成済みか。同じキーの部品が 2 つあると保存で片方が消える
        [[nodiscard]] bool IsClassObjKeyCreated(std::string_view key) const;
        void ReportInvalidClassObj(const ClassObjInfo* field) const;
        // PendingDestroy からはどこへも移らない
        enum class Presence : uint8
        {
            Disappeared,
            Appeared,
            PendingDestroy,
        };
        uint32 m_id = 0;
        std::string m_name;
        std::string m_actorFileName; // 書くのは Init だけ
        Scene* m_scene = nullptr;    // 入るシーン。書くのは Init だけ
        Presence m_presence = Presence::Disappeared;
        std::unique_ptr<Transform> m_transform;

    protected:
        std::unique_ptr<Model> m_model;
        std::unique_ptr<Animation> m_animation;
        std::unique_ptr<Shadow> m_shadow;
        std::unique_ptr<Collision> m_collision;
        std::unique_ptr<HitSensor> m_bodySensor;
        std::unique_ptr<HitSensor> m_attackSensor;
        std::unique_ptr<HitReaction> m_hitReaction;
        std::unique_ptr<EffectKeeper> m_effectKeeper;
        std::unique_ptr<ActionTimeline> m_actionTimeline;
        // 状態が借りる部品より先に壊すので、部品の最後に置く。組むのは BuildStateMachine だけ
        std::unique_ptr<StateMachine> m_stateMachine;

    private:
        float m_frameRate = 1.0f; // 体の倍率。読み書きは GetFrameRate・SetFrameRate だけ
        int m_hitStopFrames = 0;  // 当たりの止めの残り。書くのは RequestHitStop・OverrideHitStop・Update だけ
    };

    //! @brief 体の倍率を返す。1 が等速
    //! @details 体の動きと見た目 (状態機械・自機の体・アニメ) だけに掛かる。シーン全体の止めと遅くは含めない
    [[nodiscard]] float GetFrameRate(const SceneActor* actor) noexcept;
    //! @brief 体の倍率を書く
    //! @return 書いた場合 true、非有限か 0 以下で断って前の値を残した場合は false
    bool SetFrameRate(SceneActor* actor, float rate) noexcept;
    //! 体の 1 フレームの秒。固定フレームの秒に体の倍率を掛けた値
    [[nodiscard]] float GetFrameDelta(const SceneActor* actor) noexcept;

    //! @brief T を作り、部品・クラスと同じ名前の .actor・素材まで済ませて返す
    //! @details Scene の中に作る時は Scene::SpawnObject を使う (中でこの関数を呼ぶ)。
    //! Scene に入れずに部品を触る試しは、この関数で作る
    template <class T, class... TArgs> [[nodiscard]] std::unique_ptr<T> CreateActor(TArgs&&... args)
    {
        static_assert(std::derived_from<T, SceneActor>, "T は SceneActor の派生でなければならない");
        std::unique_ptr<T> actor = std::make_unique<T>(std::forward<TArgs>(args)...);
        SceneActor& base = *actor;
        base.Init(nullptr);
        return actor;
    }

    //! @brief T を作り、info で部品・.actor・配置の上書き・素材まで済ませて返す
    //! @details シーンへ入れる口 (Scene::SpawnObject) が呼ぶ。シーンの一覧に入る前に素材を読み終える
    template <class T, class... TArgs>
    [[nodiscard]] std::unique_ptr<T> CreateActorWithInfo(const ActorInitInfo& info, TArgs&&... args)
    {
        static_assert(std::derived_from<T, SceneActor>, "T は SceneActor の派生でなければならない");
        std::unique_ptr<T> actor = std::make_unique<T>(std::forward<TArgs>(args)...);
        SceneActor& base = *actor;
        base.Init(&info);
        return actor;
    }

} // namespace NS
