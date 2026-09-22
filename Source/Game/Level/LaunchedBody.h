#pragma once

#include "Runtime/Core/Math.h"
#include "Runtime/Object/Component.h"

#include <Jolt/Jolt.h>

#include <Jolt/Physics/Body/BodyID.h>

namespace NS::Phys
{
    class PhysicsScene;
}

namespace NS::Game::Level
{
    class LaunchedBody : public NS::Obj::Component
    {
    public:
        // 物理の更新が済んでから body を読むので LateUpdate 帯で名乗る
        LaunchedBody() noexcept;

        // body の生成で確保が起きる。noexcept にしない
        void Launch(const NS::Core::Vector3& velocity);

        [[nodiscard]] bool IsFlying() const noexcept { return m_flying; }

        //! 飛んでいる間の動的 body の id。飛んでいない間は無効
        [[nodiscard]] JPH::BodyID BodyId() const noexcept { return m_bodyId; }

        [[nodiscard]] NS::Core::Vector3 Velocity() const noexcept;

        void SetRestLifeSeconds(float seconds) noexcept;

        void SetDebrisCount(int count) noexcept;

        // ObjectList からは消さない。更新の最中に消すと集めた並びに解放済みのポインタが残る
        void Shatter();

        void OnUpdate() override;

        void OnEndPlay() override;

        NS_REFLECT_BEGIN(LaunchedBody, NS::Obj::Component)
        NS_REFLECT_FIELD(m_restitution, "跳ね返り")
        NS_REFLECT_FIELD(m_friction, "摩擦")
        NS_REFLECT_FIELD(m_spinPerSpeed, "回転の強さ")
        NS_REFLECT_FIELD(m_restLifeSeconds, "止まってから消える秒")
        NS_REFLECT_FIELD(m_debrisCount, "破片の数")
        NS_REFLECT_FIELD(m_debrisSpeed, "破片の速さ")
        NS_REFLECT_FIELD(m_debrisLifeSeconds, "破片の寿命秒")
        NS_REFLECT_FIELD(m_debrisScale, "破片の大きさ")
        NS_REFLECT_FIELD(m_debrisBaseColor, "破片の色")
        NS_REFLECT_END()

    private:
        // 今と同じ値なら body を出し入れしない
        void SetColliderActive(bool active);

        [[nodiscard]] NS::Core::Vector3 TumbleFrom(const NS::Core::Vector3& velocity) const noexcept;

        [[nodiscard]] JPH::BodyID CreateFlyingBody(NS::Phys::PhysicsScene& physics) const;

        void RemoveFlyingBody() noexcept;

        void ComeToRest();

        void HideAndSleep();

        // 持ち主の Scene の PhysicsScene。 Scene に居なければ null
        // 控えを持つと Scene と正が 2 つになるので、 使う時に毎回引く
        [[nodiscard]] NS::Phys::PhysicsScene* ScenePhysics() const noexcept;

        float m_restitution = 0.35f;
        float m_friction = 0.6f;
        float m_spinPerSpeed = 0.5f;
        float m_restLifeSeconds = 0.0f;
        float m_restAge = 0.0f;
        int m_debrisCount = 5;      // 壊れた時に出す破片の数
        float m_debrisSpeed = 6.0f; // 質量 1 の物が壊れた時の破片の水平初速
        // 破片が止まってから消えるまでの秒。押し飛ばした配置物と違い破片は残さない
        float m_debrisLifeSeconds = 8.0f;
        // 破片 1 個のスケール。壊れた物より明確に小さくして、数で壊れた量を見せる
        float m_debrisScale = 0.25f;
        NS::Core::Vector3 m_debrisBaseColor{0.35f, 0.32f, 0.30f};

        JPH::BodyID m_bodyId;
        bool m_flying = false;
    };
} // namespace NS::Game::Level
