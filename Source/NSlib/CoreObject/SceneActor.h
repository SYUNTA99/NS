#pragma once

#include "NSlib/Core/Assert.h"
#include "NSlib/CoreObject/IUse/IUseCamera.h"
#include "NSlib/CoreObject/IUse/IUseCollision.h"
#include "NSlib/CoreObject/IUse/IUseEffect.h"
#include "NSlib/CoreObject/IUse/IUseSceneSystem.h"
#include "NSlib/CoreObject/IUse/IUseState.h"
#include "NSlib/CoreObject/IUse/IUseTime.h"
#include "NSlib/CoreObject/Object.h"
#include "NSlib/CoreObject/StateMachineOf.h"
#include "NSlib/CoreObject/SubObject.h"
#include "NSlib/CoreObject/SubObjects/StateMachine.h"
#include "NSlib/CoreObject/SubObjects/Transform.h"
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
    class Message;
    class Scene;

    //! @brief 世界に置く物の基底。Transform と部品を持ち、MapPartsActor / PropActor / PlayerActor などが派生する
    //! @details 所属シーンを持ち、IUse〜 の問いを居るシーンへ繋ぐ。シーンに居ない間の問いは nullptr を返す
    class SceneActor : public Object,
                       public IUseCamera,
                       public IUseSceneSystem,
                       public IUseCollision,
                       public IUseEffect,
                       public IUseState,
                       public IUseTime
    {
    public:
        SceneActor() noexcept;
        virtual ~SceneActor() noexcept;
        [[nodiscard]] NS::uint32 Id() const noexcept { return m_id; }
        [[nodiscard]] const std::string& Name() const noexcept { return m_name; }
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

        //! 所有 Scene。Scene attach 前 / 破棄後は nullptr
        [[nodiscard]] Scene* OwningScene() const noexcept { return m_scene; }
        //! Scene 側が attach 時に呼ぶ。派生から手動で呼ばない
        void AttachScene(Scene* scene) noexcept { m_scene = scene; }

        [[nodiscard]] CameraManager* GetCameraManager() const noexcept override;
        [[nodiscard]] SceneSystemHolder* GetSceneSystemHolder() const noexcept override;
        [[nodiscard]] NS::Phys::PhysicsScene* GetPhysicsScene() const noexcept override;
        // 出したエフェクトを記録する部品。持たなければ nullptr
        [[nodiscard]] EffectKeeper* GetEffectKeeper() const noexcept override { return m_effectKeeper.get(); }

        //! 個体の位置・回転・大きさを持つ部品
        [[nodiscard]] Transform& Root() noexcept;
        [[nodiscard]] const Transform& Root() const noexcept;
        [[nodiscard]] IStateMachine* GetStates() noexcept override;
        [[nodiscard]] const IStateMachine* GetStates() const noexcept override;
        [[nodiscard]] float BodyDelta() const noexcept override;
        [[nodiscard]] float BodyTimeScale() const noexcept override { return m_bodyTimeScale; }
        //! @brief 体の倍率を書く
        //! @return 書いた場合 true、非有限か 0 以下で断って前の値を残した場合は false
        [[nodiscard]] bool SetBodyTimeScale(float scale) noexcept;

        //! @brief 居るシーンの重力の向きを返す
        //! @return シーンに居なければ (0, -1, 0)
        [[nodiscard]] NS::Vector3 GravityDirection() const noexcept;
        //! シーンの重力方向へ加速する。strength は m/s²、dt は秒
        //! 非有限値または dt が 0 以下なら変更しない
        void AddGravity(NS::Vector3& velocity, float strength, float dt) const noexcept;

        // 登録情報の保存キー。この SceneActor の部品でなければ空
        [[nodiscard]] std::string_view SubObjKey(const SubObject& part) const;
        // 保存キーに対応する部品。作っていなければ nullptr
        [[nodiscard]] SubObject* FindSubObj(std::string_view name) const;
        [[nodiscard]] Model* ModelSubObj() noexcept { return m_model.get(); }
        [[nodiscard]] const Model* ModelSubObj() const noexcept { return m_model.get(); }
        [[nodiscard]] Animation* AnimationSubObj() noexcept { return m_animation.get(); }
        [[nodiscard]] const Animation* AnimationSubObj() const noexcept { return m_animation.get(); }
        [[nodiscard]] Shadow* ShadowSubObj() noexcept { return m_shadow.get(); }
        [[nodiscard]] const Shadow* ShadowSubObj() const noexcept { return m_shadow.get(); }
        [[nodiscard]] Collision* CollisionSubObj() noexcept { return m_collision.get(); }
        [[nodiscard]] const Collision* CollisionSubObj() const noexcept { return m_collision.get(); }
        //! その物の体の広がりのセンサー。持たなければ nullptr
        [[nodiscard]] HitSensor* BodySensorSubObj() noexcept { return m_bodySensor.get(); }
        [[nodiscard]] const HitSensor* BodySensorSubObj() const noexcept { return m_bodySensor.get(); }
        //! @brief 体と別の広がりを持つ 2 つ目のセンサー。持たなければ nullptr
        //! @details 調べ役は体のセンサーと区別せずに扱う。今はこの枠を作る種類が無い
        [[nodiscard]] HitSensor* AttackSensorSubObj() noexcept { return m_attackSensor.get(); }
        [[nodiscard]] const HitSensor* AttackSensorSubObj() const noexcept { return m_attackSensor.get(); }
        [[nodiscard]] HitReaction* HitReactionSubObj() noexcept { return m_hitReaction.get(); }
        [[nodiscard]] const HitReaction* HitReactionSubObj() const noexcept { return m_hitReaction.get(); }

        //! @brief Update を呼ばれる段を返す
        [[nodiscard]] virtual UpdatePhase Phase() const noexcept { return UpdatePhase::Triggers; }
        virtual void ReadInput() {}
        //! 観測・裁定・状態・身体・見た目を固定順で更新する。部品は各段で明示的に呼ぶ
        //! エフェクトの記録は観測前に進める
        void Update();
        virtual void PrepareRender();

        //! 実行時にコードが足す一時オブジェクトか。true は保存・凍結・作業データに写らない
        [[nodiscard]] bool IsTransient() const noexcept { return m_transient; }

        //! 一時オブジェクトの印。Scene::SpawnTransient が立てる。テストは直接立ててよい
        void SetTransient(bool transient) noexcept { m_transient = transient; }

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
        //! 所属の Scene は入る道によって付いていないので、ここでは読まない
        virtual void OnInit() {}

        // 未登録・作成済みのメンバーや使用済みの保存キーは変更せず nullptr を返す
        template <class TBase, class T = TBase, class... TArgs>
            requires std::derived_from<T, TBase> && std::derived_from<T, SubObject>
        T* CreateSubObj(std::unique_ptr<TBase>& member, TArgs&&... args)
        {
            const SubObjectDesc* field = FindSubObjMember(&member);
            if (field == nullptr || member != nullptr || FindSubObj(field->key) != nullptr)
            {
                ReportInvalidSubObj(field);
                return nullptr;
            }
            std::unique_ptr<T> subObject = std::make_unique<T>(std::forward<TArgs>(args)...);
            T* raw = subObject.get();
            raw->AttachOwner(this);
            member = std::move(subObject);
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
            StateMachine* part = CreateSubObj(m_stateMachine);
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
        friend std::unique_ptr<SceneActor> CreateActorOfClass(std::string_view className);
        // 呼ぶのは作る関数 2 つだけで、作った直後に 1 回。private にして 2 回目を起こさない
        void Init() { OnInit(); }
        //! @brief 補間の前の値として、根の Transform と Model の今の値を控える
        //! @details ActorList::SnapshotObjects が固定ステップの頭で全ての SceneActor
        //! に呼ぶ。世界を止めている間も呼ばれる
        void SnapshotForInterpolation() noexcept;
        void SetId(NS::uint32 id) noexcept { m_id = id; }
        void SetName(std::string name) noexcept { m_name = std::move(name); }
        [[nodiscard]] const SubObjectDesc* FindSubObjMember(const void* member) const;
        void ReportInvalidSubObj(const SubObjectDesc* field) const;
        // PendingDestroy からはどこへも移らない
        enum class Presence : NS::uint8
        {
            Disappeared,
            Appeared,
            PendingDestroy,
        };
        NS::uint32 m_id = 0;
        std::string m_name;
        Scene* m_scene = nullptr; // 所有 Scene、attach 前後は nullptr
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
        // 状態が借りる部品より先に壊すので、部品の最後に置く。組むのは BuildStateMachine だけ
        std::unique_ptr<StateMachine> m_stateMachine;

    private:
        bool m_transient = false; // 一時オブジェクトの印。保存・凍結に写らない
        float m_bodyTimeScale = 1.0f;
    };

    //! @brief T を作り、OnInit で部品を組んでから返す
    //! @details Scene の中に作る時は Scene::SpawnTransient か Scene::SpawnObject を使う (中でこの関数を呼ぶ)。
    //! Scene に入れずに部品を触る試しは、この関数で作る
    template <class T, class... TArgs> [[nodiscard]] std::unique_ptr<T> CreateActor(TArgs&&... args)
    {
        static_assert(std::derived_from<T, SceneActor>, "T は SceneActor の派生でなければならない");
        std::unique_ptr<T> actor = std::make_unique<T>(std::forward<TArgs>(args)...);
        SceneActor& base = *actor;
        base.Init();
        return actor;
    }

} // namespace NS
