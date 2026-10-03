#pragma once

#include "Runtime/Core/Assert.h"
#include "Runtime/Object/ActorBase.h"
#include "Runtime/Object/Component.h"
#include "Runtime/Object/StateMachine.h"
#include "Runtime/Object/Transform.h"
#include "Runtime/Object/UpdatePhase.h"

#include <concepts>
#include <cstdint>
#include <functional>
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
    class Collider;
    class HitReaction;
    class ICameraTarget;
    class Message;
    class Scene;
    class TransformComponent;

    //! @brief 世界に置く物の基底。Transform と部品を持ち、MapParts / MapObj / Player などが派生する
    //! @details 所属シーンと窓口 (IUse〜) は土台の ActorBase が持つ
    class Actor : public ActorBase
    {
    public:
        Actor() noexcept;
        virtual ~Actor() noexcept;
        [[nodiscard]] std::uint32_t Id() const noexcept { return m_id; }
        NS_REFLECT_NONE(Actor, ActorBase)

        //! TransformComponent が持つ Root Transform。階層構築は SetParent で
        [[nodiscard]] Transform& Root() noexcept { return *m_transform; }
        [[nodiscard]] const Transform& Root() const noexcept { return *m_transform; }
        [[nodiscard]] IStateMachine* GetStateMachine() noexcept override;
        [[nodiscard]] const IStateMachine* GetStateMachine() const noexcept override;

        //! ForEachPart が部品名と部品を渡す先
        using PartVisitor = std::function<void(std::string_view, Component&)>;
        //! @brief 部品名と部品の組を決まった並びで visitor へ渡す
        //! @details 基底は Transform、Model、Animation、Shadow、Collision、BodySensor、AttackSensor、
        //! HitReaction の順で、持たない部品は飛ばす。派生は基底を呼んでから自分の部品を足す
        virtual void ForEachPart(const PartVisitor& visitor) const;
        //! @brief 部品名 name の部品を返す
        //! @return 持たなければ nullptr
        [[nodiscard]] Component* Part(std::string_view name) const;
        //! @brief part の部品名を返す
        //! @return この Actor の部品でなければ空
        [[nodiscard]] std::string_view PartName(const Component& part) const;
        //! @brief 部品名 name の部品を作って付ける。既に持っていればそれを返す
        //! @details 基底が作れるのは Model、Animation、Shadow、Collision、BodySensor、AttackSensor、HitReaction
        //! BodySensor と AttackSensor は形を自分で持つ ShapeHitSensor を作る
        //! @return 付けた部品。作れない名前は nullptr
        virtual Component* CreatePart(std::string_view name);
        [[nodiscard]] Model* ModelPart() noexcept { return m_model.get(); }
        [[nodiscard]] const Model* ModelPart() const noexcept { return m_model.get(); }
        [[nodiscard]] Animation* AnimationPart() noexcept { return m_animation.get(); }
        [[nodiscard]] const Animation* AnimationPart() const noexcept { return m_animation.get(); }
        [[nodiscard]] Shadow* ShadowPart() noexcept { return m_shadow.get(); }
        [[nodiscard]] const Shadow* ShadowPart() const noexcept { return m_shadow.get(); }
        [[nodiscard]] Collider* CollisionPart() noexcept { return m_collision.get(); }
        [[nodiscard]] const Collider* CollisionPart() const noexcept { return m_collision.get(); }
        //! その物の体の広がりのセンサー。持たなければ nullptr
        [[nodiscard]] HitSensor* BodySensorPart() noexcept { return m_bodySensor.get(); }
        [[nodiscard]] const HitSensor* BodySensorPart() const noexcept { return m_bodySensor.get(); }
        //! @brief 体と別の広がりを持つ 2 つ目のセンサー。持たなければ nullptr
        //! @details 調べ役は体のセンサーと区別せずに扱う。今はこの枠を作る種類が無い
        [[nodiscard]] HitSensor* AttackSensorPart() noexcept { return m_attackSensor.get(); }
        [[nodiscard]] const HitSensor* AttackSensorPart() const noexcept { return m_attackSensor.get(); }
        [[nodiscard]] HitReaction* HitReactionPart() noexcept { return m_hitReaction.get(); }
        [[nodiscard]] const HitReaction* HitReactionPart() const noexcept { return m_hitReaction.get(); }

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

        //! 配置物の組み直し・当たりの張り直しの後に scene が一時オブジェクトへ知らせる。データ由来の配置物には来ない
        virtual void OnObjectsRebuilt() {}

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

        //! 配下 Component の OnStart を伝播
        void OnStart();
        //! 世界から外し、全ての部品の OnEndPlay を呼ぶ
        virtual void OnEndPlay();

        //! この配置物自身の active 値。親の状態は含まない
        [[nodiscard]] bool IsActiveSelf() const noexcept { return IsAlive(); }

        //! @brief 自分と全ての祖先が有効か
        //! @details Component::IsActive がこれを見るので、偽の間は配下 Component が更新も描画も当たりも止まる
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
        void OnAppear() override;
        void OnKill() noexcept override;
        //! @brief 1 フレームの最初の段。世界を読んで控えるだけで、状態も身体も動かさない
        //! @details 既定は Model の前フレーム値の控え
        virtual void ObserveStep();
        //! @brief 観測の結果から、この歩に使う値を決める段。知らせを送る裁定はここに置き、観測の段へ入れない
        //! @details 既定は何もしない
        virtual void DecideStep() {}
        //! @brief 状態機械を 1 歩進める段
        //! @details 既定は状態機械を 1 歩進める。持たなければ何もしない
        virtual void StateStep();
        //! @brief 状態が決めた操作を身体へ当て、身体を動かす段。既定は何もしない
        virtual void BodyStep() {}
        //! @brief 動いた後の姿から見た目と演出を進める段
        //! @details 既定は HitReaction を進める
        virtual void VisualStep();
        static void TickPart(Component* component)
        {
            if (component != nullptr && component->IsActive())
            {
                component->OnUpdate();
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
        void AttachFixedComponent(Component& component);
        void SetCollisionPart(std::unique_ptr<Collider> collision);
        //! @brief 体のセンサーの部品 BodySensor を派生の型で差す。持ち主の形を映すセンサーを付ける口
        //! @details 既に持っているか sensor が nullptr なら何もしない。差した後の CreatePart("BodySensor") は
        //! 差した部品を返す
        //! @param[in] sensor 差す体のセンサー
        void SetBodySensorPart(std::unique_ptr<HitSensor> sensor);

    private:
        friend class ObjectList;
        //! 状態機械を預かる。既に持っていれば NS_LOG_ERROR を出して machine を捨て、false を返す
        [[nodiscard]] bool AdoptStateMachine(std::unique_ptr<IStateMachine> machine);
        void SetId(std::uint32_t id) noexcept { m_id = id; }
        std::uint32_t m_id = 0;
        std::unique_ptr<TransformComponent> m_rootPart;
        std::unique_ptr<Model> m_model;
        std::unique_ptr<Animation> m_animation;
        std::unique_ptr<Shadow> m_shadow;
        std::unique_ptr<Collider> m_collision;
        std::unique_ptr<HitSensor> m_bodySensor;
        std::unique_ptr<HitSensor> m_attackSensor;
        std::unique_ptr<IStateMachine> m_stateMachine; // BuildStateMachine が 1 回だけ預かる。持たない種類は nullptr
        std::unique_ptr<HitReaction> m_hitReaction;
        Transform* m_transform = nullptr; // TransformComponent が持つ実体、Actor が必ず 1 つ積む
        std::vector<Actor*> m_children;   // 子 Actor、非所有
        Actor* m_parent = nullptr;        // 親 Actor、root なら nullptr
        bool m_transient = false;         // 一時オブジェクトの印。保存・凍結に写らない

        void DetachFromParent() noexcept;
    };

} // namespace NS::Obj
