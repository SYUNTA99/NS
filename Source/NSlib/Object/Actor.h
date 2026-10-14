#pragma once

#include "NSlib/Core/Assert.h"
#include "NSlib/Object/IUse/IUseCamera.h"
#include "NSlib/Object/IUse/IUseCollision.h"
#include "NSlib/Object/IUse/IUseEffect.h"
#include "NSlib/Object/IUse/IUseSceneSystem.h"
#include "NSlib/Object/IUse/IUseState.h"
#include "NSlib/Object/IUse/IUseTime.h"
#include "NSlib/Object/Object.h"
#include "NSlib/Object/StateMachine.h"
#include "NSlib/Object/SubObject.h"
#include "NSlib/Object/Transform.h"
#include "NSlib/Object/UpdatePhase.h"

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace NS::Obj
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
    class TransformSubObject;

    //! @brief 基底の部品の枠。Actor の枠の関数だけが作り、CreateSubObj に渡す
    //! @details member は作った Actor の枠のメンバーを指すので、別の Actor へ渡さない
    template <class TBase> struct SubObjSlot
    {
        std::string_view name;
        TBase** member;
    };

    //! @brief 世界に置く物の基底。Transform と部品を持ち、MapParts / MapObj / Player などが派生する
    //! @details 所属シーンを持ち、IUse〜 の問いを居るシーンへ繋ぐ。シーンに居ない間の問いは nullptr を返す
    class Actor : public Object,
                  public IUseCamera,
                  public IUseSceneSystem,
                  public IUseCollision,
                  public IUseEffect,
                  public IUseState,
                  public IUseTime
    {
    public:
        Actor() noexcept;
        virtual ~Actor() noexcept;
        [[nodiscard]] std::uint32_t Id() const noexcept { return m_id; }
        [[nodiscard]] const std::string& Name() const noexcept { return m_name; }
        NS_REFLECT_NONE(Actor, Object)

        //! @brief 世界に出す。更新の段・描画・当たり・センサーへ登録する。出ていれば何もしない
        //! @details Destroy の後に呼ぶのは呼び手のバグで、NS_ASSERT が止める。出荷版では何もしない
        void Appear();
        //! @brief 世界から外す。出ていなければ何もしない
        //! @details 解放はしないので Appear で戻せる。子の値は触らないので、親を戻せば子も一緒に戻る
        void Disappear() noexcept;
        //! @brief 世界から外し、段を回し終えた後に解放させる。2 回目からは何もしない
        //! @details その場では解放しないので、同じ段の中では指したまま読める。やり直しの知らせは
        //! 届かず戻らないので、戻す物は Disappear を使う。子は解放せず、根になって残る
        void Destroy() noexcept;
        //! この配置物自身が世界に出ているか。親の状態は含まない
        [[nodiscard]] bool IsAppeared() const noexcept { return m_appeared; }
        //! Destroy を呼ばれて、段の後の解放を待っている場合 true、それ以外の場合は false
        [[nodiscard]] bool IsPendingDestroy() const noexcept { return m_pendingDestroy; }

        //! 所有 Scene。Scene attach 前 / 破棄後は nullptr
        [[nodiscard]] Scene* OwningScene() const noexcept { return m_scene; }
        //! Scene 側が attach 時に呼ぶ。派生から手動で呼ばない
        void AttachScene(Scene* scene) noexcept { m_scene = scene; }

        [[nodiscard]] CameraManager* GetCameraManager() const noexcept override;
        [[nodiscard]] SceneSystemHolder* GetSceneSystemHolder() const noexcept override;
        [[nodiscard]] NS::Phys::PhysicsScene* GetPhysicsScene() const noexcept override;
        //! 出したエフェクトを覚える部品。枠 Effect を作っていなければ nullptr
        [[nodiscard]] EffectKeeper* GetEffectKeeper() const noexcept override { return m_effectKeeper; }

        //! TransformSubObject が持つ Root Transform。階層構築は SetParent で
        [[nodiscard]] Transform& Root() noexcept { return *m_transform; }
        [[nodiscard]] const Transform& Root() const noexcept { return *m_transform; }
        [[nodiscard]] IStateMachine* GetStateMachine() noexcept override;
        [[nodiscard]] const IStateMachine* GetStateMachine() const noexcept override;
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

        //! @brief OnInit を 1 回だけ呼ぶ。2 回目からは何もしない
        //! @details CreateActorOfClass と ActorList に入る所が呼ぶ。Scene に入れずに部品を触る時は自分で呼ぶ
        void Init();

        //! 根の Transform、基底の枠を Model から Effect まで決まった順、派生の部品を作った順に並べた全部の部品
        [[nodiscard]] const std::vector<SubObject*>& SubObjs() const noexcept { return m_subObjOrder; }
        //! @brief 部品名 name の部品を返す
        //! @return 持たなければ nullptr
        [[nodiscard]] SubObject* FindSubObj(std::string_view name) const;
        //! @brief 部品名 name の部品を返す。持たなければ基底の枠を既定の型で作る
        //! @details 種類の既定値が名前だけで部品を足す口。Collision は BoxCollision、センサーは ShapeHitSensor で作る
        //! @return 持っていた部品か作った部品。基底の枠でない名前は nullptr
        SubObject* CreateSubObj(std::string_view name);
        [[nodiscard]] Model* ModelSubObj() noexcept { return m_model; }
        [[nodiscard]] const Model* ModelSubObj() const noexcept { return m_model; }
        [[nodiscard]] Animation* AnimationSubObj() noexcept { return m_animation; }
        [[nodiscard]] const Animation* AnimationSubObj() const noexcept { return m_animation; }
        [[nodiscard]] Shadow* ShadowSubObj() noexcept { return m_shadow; }
        [[nodiscard]] const Shadow* ShadowSubObj() const noexcept { return m_shadow; }
        [[nodiscard]] Collision* CollisionSubObj() noexcept { return m_collision; }
        [[nodiscard]] const Collision* CollisionSubObj() const noexcept { return m_collision; }
        //! その物の体の広がりのセンサー。持たなければ nullptr
        [[nodiscard]] HitSensor* BodySensorSubObj() noexcept { return m_bodySensor; }
        [[nodiscard]] const HitSensor* BodySensorSubObj() const noexcept { return m_bodySensor; }
        //! @brief 体と別の広がりを持つ 2 つ目のセンサー。持たなければ nullptr
        //! @details 調べ役は体のセンサーと区別せずに扱う。今はこの枠を作る種類が無い
        [[nodiscard]] HitSensor* AttackSensorSubObj() noexcept { return m_attackSensor; }
        [[nodiscard]] const HitSensor* AttackSensorSubObj() const noexcept { return m_attackSensor; }
        [[nodiscard]] HitReaction* HitReactionSubObj() noexcept { return m_hitReaction; }
        [[nodiscard]] const HitReaction* HitReactionSubObj() const noexcept { return m_hitReaction; }

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

        [[nodiscard]] Actor* Parent() const noexcept { return m_parent; }
        //! parent==nullptr で root 化。Transform の親子関係も同期更新する
        void SetParent(Actor* parent) noexcept;
        [[nodiscard]] const std::vector<Actor*>& Children() const noexcept { return m_children; }

        //! 配下 SubObject の OnStart を伝播
        void OnStart();
        //! 世界から外し、全ての部品の OnEndPlay を呼ぶ
        virtual void OnEndPlay();

        //! @brief 自分と全ての祖先が世界に出ているか
        //! @details SubObject::IsActive がこれを見るので、偽の間は配下 SubObject が更新も描画も当たりも止まる
        [[nodiscard]] bool IsActiveInHierarchy() const noexcept;

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

        //! @details 部品は CreateSubObj で作る。基底の枠の型を替える派生は、基底の OnInit を呼ばずに自分で作る
        //! 所属の Scene は入る道によって付いていないので、ここでは読まない
        virtual void OnInit() {}

        //! @brief 基底の枠 slot に、枠の型 TBase の派生 T を作って付ける
        //! @return 作った部品。枠の名前の部品を持っていれば NS_LOG_ERROR を出して作らず nullptr
        template <class T, class TBase, class... TArgs>
            requires std::derived_from<T, TBase>
        T* CreateSubObj(SubObjSlot<TBase> slot, TArgs&&... args)
        {
            if (FindSubObj(slot.name) != nullptr)
            {
                ReportTakenName(slot.name);
                return nullptr;
            }
            return AdoptSubObj(std::make_unique<T>(std::forward<TArgs>(args)...), slot.name, *slot.member);
        }
        //! @brief 派生の部品 T を部品名 name で作って付ける
        //! @return 作った部品。name の部品を持っていれば NS_LOG_ERROR を出して作らず nullptr
        template <class T, class... TArgs>
            requires std::derived_from<T, SubObject>
        T* CreateSubObj(std::string_view name, TArgs&&... args)
        {
            if (FindSubObj(name) != nullptr)
            {
                ReportTakenName(name);
                return nullptr;
            }
            return AdoptSubObj(std::make_unique<T>(std::forward<TArgs>(args)...), name);
        }
        [[nodiscard]] SubObjSlot<Model> ModelSlot() noexcept { return {"Model", &m_model}; }
        [[nodiscard]] SubObjSlot<Animation> AnimationSlot() noexcept { return {"Animation", &m_animation}; }
        [[nodiscard]] SubObjSlot<Shadow> ShadowSlot() noexcept { return {"Shadow", &m_shadow}; }
        [[nodiscard]] SubObjSlot<Collision> CollisionSlot() noexcept { return {"Collision", &m_collision}; }
        [[nodiscard]] SubObjSlot<HitSensor> BodySensorSlot() noexcept { return {"BodySensor", &m_bodySensor}; }
        [[nodiscard]] SubObjSlot<HitSensor> AttackSensorSlot() noexcept { return {"AttackSensor", &m_attackSensor}; }
        [[nodiscard]] SubObjSlot<HitReaction> HitReactionSlot() noexcept { return {"HitReaction", &m_hitReaction}; }
        [[nodiscard]] SubObjSlot<EffectKeeper> EffectSlot() noexcept { return {"Effect", &m_effectKeeper}; }

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
        static void TickSubObj(SubObject* subObject)
        {
            if (subObject != nullptr && subObject->IsActive())
            {
                subObject->OnUpdate();
            }
        }
        //! 自分を持ち主に、TStates の先頭へ入る状態機械を一度だけ構築する
        //! OnEnter が使う部品を先に用意する。outMachine は OnEnter 前に設定する
        //! 基底が所有し、型付きポインタは非所有。既に構築済みなら変更せず false
        template <typename TOwner, typename... TStates>
        bool BuildStateMachine(TOwner& owner, StateMachine<TOwner>*& outMachine)
        {
            static_assert(std::is_base_of_v<Actor, TOwner>, "持ち主は Actor の派生");
            NS_ASSERT(Scene,
                      static_cast<const Actor*>(&owner) == this,
                      "Actor::BuildStateMachine: 持ち主に自分以外の Actor を渡している");
            std::unique_ptr<StateMachine<TOwner>> machine = std::make_unique<StateMachine<TOwner>>();
            StateMachine<TOwner>* built = machine.get();
            if (!AdoptStateMachine(std::move(machine)))
            {
                return false;
            }
            outMachine = built;
            built->template Build<TStates...>(owner);
            return true;
        }
        //! 状態機械を 1 固定ステップ進める。持たなければ何もしない
        void StepStateMachine();

    private:
        friend class ActorList;
        friend bool SendMsg(const Message& msg, HitSensor& receiver, HitSensor* sender);
        friend bool SendMsgToActor(const Message& msg, Actor& receiver);
        //! @brief 補間の前の値として、根の Transform と Model の今の値を控える
        //! @details ActorList::SnapshotObjects が固定ステップの頭で全ての Actor に呼ぶ。世界を止めている間も呼ばれる
        void SnapshotForInterpolation() noexcept;
        //! 状態機械を預かる。既に持っていれば NS_LOG_ERROR を出して machine を捨て、false を返す
        [[nodiscard]] bool AdoptStateMachine(std::unique_ptr<IStateMachine> machine);
        void SetId(std::uint32_t id) noexcept { m_id = id; }
        void SetName(std::string name) noexcept { m_name = std::move(name); }
        template <class T> T* AdoptSubObj(std::unique_ptr<T> subObject, std::string_view name)
        {
            T* raw = subObject.get();
            AdoptSubObjBase(std::unique_ptr<SubObject>{std::move(subObject)}, name);
            return raw;
        }
        //! @brief 根か基底の枠 member に入れてから一覧に積む
        //! @details 並びの位置は member で決まるので、積む前に入れる
        template <class T, class TBase>
        T* AdoptSubObj(std::unique_ptr<T> subObject, std::string_view name, TBase*& member)
        {
            member = subObject.get();
            return AdoptSubObj(std::move(subObject), name);
        }
        void AdoptSubObjBase(std::unique_ptr<SubObject> subObject, std::string_view name);
        void ReportTakenName(std::string_view name) const;
        [[nodiscard]] std::size_t OrderRank(const SubObject* subObject) const noexcept;
        std::uint32_t m_id = 0;
        std::string m_name;
        Scene* m_scene = nullptr; // 所有 Scene、attach 前後は nullptr
        bool m_appeared = false;
        bool m_pendingDestroy = false;
        std::vector<std::unique_ptr<SubObject>> m_subObjects; // 根と作った部品の全部。作った順で、壊す時は逆順
        std::vector<SubObject*> m_subObjOrder;
        TransformSubObject* m_rootSubObj = nullptr;
        Model* m_model = nullptr;
        Animation* m_animation = nullptr;
        Shadow* m_shadow = nullptr;
        Collision* m_collision = nullptr;
        HitSensor* m_bodySensor = nullptr;
        HitSensor* m_attackSensor = nullptr;
        std::unique_ptr<IStateMachine> m_stateMachine; // BuildStateMachine が 1 回だけ預かる。持たない種類は nullptr
        HitReaction* m_hitReaction = nullptr;
        EffectKeeper* m_effectKeeper = nullptr;
        bool m_initialized = false;       // Init が OnInit を呼んだか
        Transform* m_transform = nullptr; // TransformSubObject が持つ実体、Actor が必ず 1 つ積む
        std::vector<Actor*> m_children;   // 子 Actor、非所有
        Actor* m_parent = nullptr;        // 親 Actor、root なら nullptr
        bool m_transient = false;         // 一時オブジェクトの印。保存・凍結に写らない
        float m_bodyTimeScale = 1.0f;

        void DetachFromParent() noexcept;
    };

} // namespace NS::Obj
