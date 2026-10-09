#pragma once

#include "NSlib/Core/Assert.h"
#include "NSlib/Object/ActorBase.h"
#include "NSlib/Object/StateMachine.h"
#include "NSlib/Object/SubObject.h"
#include "NSlib/Object/Transform.h"
#include "NSlib/Object/UpdatePhase.h"

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <memory>
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
    class HitReaction;
    class ICameraTarget;
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
    //! @details 所属シーンと窓口 (IUse〜) は土台の ActorBase が持つ
    class Actor : public ActorBase
    {
    public:
        Actor() noexcept;
        virtual ~Actor() noexcept;
        [[nodiscard]] std::uint32_t Id() const noexcept { return m_id; }
        NS_REFLECT_NONE(Actor, ActorBase)

        //! TransformSubObject が持つ Root Transform。階層構築は SetParent で
        [[nodiscard]] Transform& Root() noexcept { return *m_transform; }
        [[nodiscard]] const Transform& Root() const noexcept { return *m_transform; }
        [[nodiscard]] IStateMachine* GetStateMachine() noexcept override;
        [[nodiscard]] const IStateMachine* GetStateMachine() const noexcept override;

        //! @brief Init を 1 回だけ呼ぶ。2 回目からは何もしない
        //! @details CreateActorOfClass と ActorList に入る所が呼ぶ。Scene に入れずに部品を触る時は自分で呼ぶ
        void EnsureInit();

        //! 根の Transform、基底の枠を Model から HitReaction まで決まった順、派生の部品を作った順に並べた全部の部品
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
        //! @return 既定は Triggers
        [[nodiscard]] virtual UpdatePhase Phase() const noexcept { return UpdatePhase::Triggers; }
        //! Input の段で、出ている全ての Actor に呼ばれる。既定は何もしない
        virtual void ReadInput() {}
        //! @brief Phase が返す段で、1 固定ステップを観測、決定、状態、身体、見た目の順に 1 回ずつ進める
        //! @details 順は基底のここ 1 か所で決まり、派生は上書きできない。派生は段の中身だけを ObserveStep などで書く。
        //! 部品は自分では回らないので、各段で順に呼ぶ
        void Update();
        //! RenderPrep の段で、出ている全ての Actor に呼ばれる。既定は Animation を進める
        virtual void PrepareRender();

        //! 実行時にコードが足す一時オブジェクトか。true は保存・凍結・作業データに写らず、
        //! データからの組み直し後も残る
        [[nodiscard]] bool IsTransient() const noexcept { return m_transient; }

        //! 一時オブジェクトの印。Scene::SpawnTransient が立てる。テストは直接立ててよい
        void SetTransient(bool transient) noexcept { m_transient = transient; }

        //! @brief 全ての配置物の開始が済んだ後に 1 回呼ばれる。オデッセイの initAfterPlacement に当たる
        //! @details シーンに 1 つの物を作るなど、他の配置物が揃っている前提の用意を書く
        virtual void InitAfterPlacement() {}

        //! @brief 自分のセンサー self が、持ち主の違うセンサー other に重なったフレームに呼ばれる
        //! @details 重なっている間は毎フレーム呼ばれ、相手の持ち主にも向きを入れ替えて同じフレームに呼ばれる。
        //! 調べ役は種類を見ないので、相手の種類を見て知らせを送るかはここで決める。オデッセイの attackSensor
        virtual void AttackSensor(HitSensor& self, HitSensor& other)
        {
            (void)self;
            (void)other;
        }

        //! 追従カメラに追われる時の窓口。追われない物は nullptr
        [[nodiscard]] virtual const ICameraTarget* GetCameraTarget() const noexcept { return nullptr; }

        //! @brief 知らせを受け取る。応じた場合 true、知らない知らせと応じなかった知らせは false
        //! @details sender と receiver は知らせを運んだセンサー。センサーを介さない知らせでは nullptr
        virtual bool ReceiveMsg(const Message& msg, HitSensor* sender, HitSensor* receiver)
        {
            (void)msg;
            (void)sender;
            (void)receiver;
            return false;
        }

        [[nodiscard]] Actor* Parent() const noexcept { return m_parent; }
        //! parent==nullptr で root 化。Transform の親子関係も同期更新する
        void SetParent(Actor* parent) noexcept;
        [[nodiscard]] const std::vector<Actor*>& Children() const noexcept { return m_children; }

        //! 配下 SubObject の OnStart を伝播
        void OnStart();
        //! 世界から外し、全ての部品の OnEndPlay を呼ぶ
        virtual void OnEndPlay();

        //! この配置物自身の active 値。親の状態は含まない
        [[nodiscard]] bool IsActiveSelf() const noexcept { return IsAlive(); }

        //! @brief 自分と全ての祖先が有効か
        //! @details SubObject::IsActive がこれを見るので、偽の間は配下 SubObject が更新も描画も当たりも止まる
        [[nodiscard]] bool IsActiveInHierarchy() const noexcept;

        //! active を切り替える。子の値は触らないので、親を戻せば子も一緒に戻る
        void SetActive(bool active) noexcept
        {
            if (active)
            {
                Appear();
            }
            else
            {
                Kill();
            }
        }

    protected:
        //! @brief 部品を作る。EnsureInit から 1 回だけ呼ばれる。既定は何もしない
        //! @details 部品は CreateSubObj で作る。基底の枠の型を替える派生は、基底の Init を呼ばずに自分で作る
        //! 所属の Scene は入る道によって付いていないので、ここでは読まない
        virtual void Init() {}

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

        void OnAppear() override;
        void OnKill() noexcept override;
        //! @brief 観測の段。この歩の始まり
        //! @details 世界と相手を読んで控え、歩ごとの記録の時計を進める。状態・身体・見た目を書かない。
        //! 既定は何もしない。補間の前の値は段でなく固定ステップの頭で控える
        virtual void ObserveStep() {}
        //! @brief 裁定の段
        //! @details 観測から、この歩に使う値を決め、知らせを送る。既定は何もしない
        virtual void DecideStep() {}
        //! @brief 状態機械の段
        //! @details 状態機械を 1 歩進める。上書きするのは状態機械の前後に持ち主の処理を挟む時だけで、
        //! 状態機械を持たない物は上書きしない。既定は状態機械を 1 歩進め、持たなければ何もしない
        virtual void StateStep();
        //! @brief 身体の段
        //! @details 身体を動かし、物理と当たりの置き場を今の姿へ揃える。カメラは仮想カメラをここで 1 歩進める。
        //! 既定は何もしない
        virtual void BodyStep() {}
        //! @brief 見た目の段
        //! @details 動いた後の姿から見た目・演出・寿命の減りを進める。既定は HitReaction を進める
        virtual void VisualStep();
        static void TickSubObj(SubObject* subObject)
        {
            if (subObject != nullptr && subObject->IsActive())
            {
                subObject->OnUpdate();
            }
        }
        //! @brief 持ち主の型 TOwner の状態機械を TStates で組み、基底に預けて先頭の状態へ入る
        //! @details 状態機械は 1 体に 1 つで、Update が 1 固定ステップ進める。既に持っている時は組まずに
        //! NS_LOG_ERROR で知らせて false を返し、今の機械をそのまま残す (動いている機械は状態の中から呼ばれて
        //! いることがあり、捨てると呼び出し中の状態が消える)。型付きのポインタは先頭の OnEnter より前に
        //! outMachine へ書くので、OnEnter の中からも引ける。持ち主は OnEnter の触る所を作り終えてから呼ぶ。
        //! 持ち主を受けるのはここだけで、機械は生涯この持ち主を状態へ渡す
        //! @tparam TOwner 呼ぶ派生の型。状態は StateOf<自分の型, TOwner> から派生する
        //! @tparam TStates 状態の型の並び。先頭が初期状態で、並べた型が移れる状態の全部になる
        //! @param[in] owner 状態へ渡す持ち主。自分自身を渡す
        //! @param[out] outMachine 組んだ状態機械の置き場。基底が所有するので持ち主は参照を持つだけ。失敗時は触らない
        //! @return 組めた場合 true、既に状態機械を持っていて組まなかった場合 false
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
        //! @brief 補間の前の値として、根の Transform と Model の今の値を控える
        //! @details ActorList::SnapshotObjects が固定ステップの頭で全ての Actor に呼ぶ。世界を止めている間も呼ばれる
        void SnapshotForInterpolation() noexcept;
        //! 状態機械を預かる。既に持っていれば NS_LOG_ERROR を出して machine を捨て、false を返す
        [[nodiscard]] bool AdoptStateMachine(std::unique_ptr<IStateMachine> machine);
        void SetId(std::uint32_t id) noexcept { m_id = id; }
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
        //! 部品名 name が使われていて作れない事を NS_LOG_ERROR で知らせる
        void ReportTakenName(std::string_view name) const;
        //! SubObjs の並びの順位。根が 0、基底の枠が決まった順に 1 から 7、派生の部品は 8
        [[nodiscard]] std::size_t OrderRank(const SubObject* subObject) const noexcept;
        std::uint32_t m_id = 0;
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
        bool m_initialized = false;       // EnsureInit が Init を呼んだか
        Transform* m_transform = nullptr; // TransformSubObject が持つ実体、Actor が必ず 1 つ積む
        std::vector<Actor*> m_children;   // 子 Actor、非所有
        Actor* m_parent = nullptr;        // 親 Actor、root なら nullptr
        bool m_transient = false;         // 一時オブジェクトの印。保存・凍結に写らない

        void DetachFromParent() noexcept;
    };

} // namespace NS::Obj
