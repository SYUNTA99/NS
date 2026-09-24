#pragma once

#include "Runtime/Core/Math.h"
#include "Runtime/Object/Component.h"
#include "Runtime/Physics/PhysicsScene.h"

namespace NS::Obj
{
    //! @brief シーンの物理の世界の設定を持つ Component
    //! @details 配置物に 1 つ載せ、持ち主の Scene の PhysicsScene へ重力を入れる
    //! 重力は RigidBody と押し飛ばした配置物など、Jolt が動かす body にだけ掛かる。自機は重力を自分で持つ
    //! 入れるのは Scene に入った時と、世界が回っている間の毎歩。エディタの欄の書き換えは次の 1 歩で効く
    //! 複数置くと、後から更新された物の値が効く
    //! 外れる時は重力を DefaultGravity へ戻す
    class PhysicsSettings : public Component
    {
    public:
        //! 他の Component の更新より先に重力を入れるので EarlyUpdate 帯で名乗る
        PhysicsSettings() noexcept;

        //! 世界の重力 (m/s^2)。非有限の成分を含む値は PhysicsScene が受け取らない
        void SetGravity(const NS::Core::Vector3& gravity) noexcept { m_gravity = gravity; }
        [[nodiscard]] const NS::Core::Vector3& Gravity() const noexcept { return m_gravity; }

        void OnStart() override;
        void OnUpdate() override;
        void OnEndPlay() override;

        NS_REFLECT_BEGIN(PhysicsSettings, Component)
        NS_REFLECT_FIELD(m_gravity, "重力")
        NS_REFLECT_END()

    private:
        // 持ち主の Scene の PhysicsScene へ重力を入れる。Scene に居なければ何もしない
        void Apply(const NS::Core::Vector3& gravity) noexcept;

        NS::Core::Vector3 m_gravity = NS::Phys::DefaultGravity(); // 世界の重力 (m/s^2)
    };
} // namespace NS::Obj
