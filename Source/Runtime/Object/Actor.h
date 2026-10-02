#pragma once

#include "Runtime/Object/ActorBase.h"
#include "Runtime/Object/Component.h"
#include "Runtime/Object/Transform.h"

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
    class CapsuleCollider;
    class Collider;
    class StateMachineComponent;
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
        //! @details 基底は Transform、Model、Animation、Shadow、Collider、Collision、BodySensor、AttackSensor、
        //! StateMachine、HitReaction の順で、持たない部品は飛ばす。派生は基底を呼んでから自分の部品を足す
        virtual void ForEachPart(const PartVisitor& visitor) const;
        //! @brief 部品名 name の部品を返す
        //! @return 持たなければ nullptr
        [[nodiscard]] Component* Part(std::string_view name) const;
        //! @brief part の部品名を返す
        //! @return この Actor の部品でなければ空
        [[nodiscard]] std::string_view PartName(const Component& part) const;
        //! @brief 部品名 name の部品を作って付ける。既に持っていればそれを返す
        //! @details 基底が作れるのは Model、Animation、Shadow、Collider、Collision、BodySensor、AttackSensor、
        //! StateMachine、HitReaction
        //! @return 付けた部品。作れない名前は nullptr
        virtual Component* CreatePart(std::string_view name);
        [[nodiscard]] Model* ModelPart() noexcept { return m_model.get(); }
        [[nodiscard]] const Model* ModelPart() const noexcept { return m_model.get(); }
        [[nodiscard]] Animation* AnimationPart() noexcept { return m_animation.get(); }
        [[nodiscard]] const Animation* AnimationPart() const noexcept { return m_animation.get(); }
        [[nodiscard]] Shadow* ShadowPart() noexcept { return m_shadow.get(); }
        [[nodiscard]] const Shadow* ShadowPart() const noexcept { return m_shadow.get(); }
        [[nodiscard]] CapsuleCollider* ColliderPart() noexcept { return m_collider.get(); }
        [[nodiscard]] const CapsuleCollider* ColliderPart() const noexcept { return m_collider.get(); }
        [[nodiscard]] Collider* CollisionPart() noexcept { return m_collision.get(); }
        [[nodiscard]] const Collider* CollisionPart() const noexcept { return m_collision.get(); }
        [[nodiscard]] HitSensor* BodySensorPart() noexcept { return m_bodySensor.get(); }
        [[nodiscard]] const HitSensor* BodySensorPart() const noexcept { return m_bodySensor.get(); }
        [[nodiscard]] HitSensor* AttackSensorPart() noexcept { return m_attackSensor.get(); }
        [[nodiscard]] const HitSensor* AttackSensorPart() const noexcept { return m_attackSensor.get(); }
        [[nodiscard]] HitReaction* HitReactionPart() noexcept { return m_hitReaction.get(); }
        [[nodiscard]] const HitReaction* HitReactionPart() const noexcept { return m_hitReaction.get(); }

        //! Model、StateMachine、HitReaction の順に進める
        void Update() override;
        //! Animation を進める
        void PrepareRender() override;

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

        //! @brief 自分のセンサー self が、self の種類が調べる種類の相手のセンサー other に重なったフレームに呼ばれる
        //! @details 重なっている間は毎フレーム呼ばれる。相手へ知らせを送るかはここで決める。オデッセイの attackSensor
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
        //! Update を呼ぶ
        void OnUpdate();
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
        static void TickPart(Component* component)
        {
            if (component != nullptr && component->IsActive())
            {
                component->OnUpdate();
            }
        }
        void AttachFixedComponent(Component& component);
        void SetCollisionPart(std::unique_ptr<Collider> collision);

    private:
        friend class ObjectList;
        void SetId(std::uint32_t id) noexcept { m_id = id; }
        std::uint32_t m_id = 0;
        std::unique_ptr<TransformComponent> m_rootPart;
        std::unique_ptr<Model> m_model;
        std::unique_ptr<Animation> m_animation;
        std::unique_ptr<Shadow> m_shadow;
        std::unique_ptr<CapsuleCollider> m_collider;
        std::unique_ptr<Collider> m_collision;
        std::unique_ptr<HitSensor> m_bodySensor;
        std::unique_ptr<HitSensor> m_attackSensor;
        std::unique_ptr<StateMachineComponent> m_stateMachine;
        std::unique_ptr<HitReaction> m_hitReaction;
        Transform* m_transform = nullptr; // TransformComponent が持つ実体、Actor が必ず 1 つ積む
        std::vector<Actor*> m_children;   // 子 Actor、非所有
        Actor* m_parent = nullptr;        // 親 Actor、root なら nullptr
        bool m_transient = false;         // 一時オブジェクトの印。保存・凍結に写らない

        void DetachFromParent() noexcept;
    };

} // namespace NS::Obj
