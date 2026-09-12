#pragma once

#include "Runtime/Object/Component.h"

#include <Jolt/Jolt.h>

#include <Jolt/Physics/Body/BodyID.h>

namespace NS::Physics
{
    class PhysicsScene;
}

namespace NS::Object
{
    //! @brief 当たり形状 Component の共通基底
    //! @details どの形の body として PhysicsScene へ入れるかは派生が決める
    //! ObjectList::SyncPhysics はこの型だけを見て回るので、形状を足しても同期側は変わらない
    //! body は id でだけ持つ。 どの PhysicsScene に居るかは持ち主の Scene が決め、 PhysicsScene の控えは持たない
    //! 抽象基底なので TypeRegistry には登録しない
    //! 依存: NS::Physics::PhysicsScene, JPH::BodyID
    class ColliderComponent : public Component
    {
    public:
        //! world 座標の当たりを body 1 個として physics へ入れる。 入れた body があれば置き直す
        //! 何も入れない形状もある。 持ち主が Scene に居る時、 Scene の PhysicsScene 以外はエラーを出して受け取らない
        void SyncToPhysics(NS::Physics::PhysicsScene& physics);

        //! 入れた body の id。 何も入れなかった形状では無効
        [[nodiscard]] JPH::BodyID BodyId() const noexcept { return m_bodyId; }

        //! 自分が入れた body を physics から外す。 入れていなければ何もしない
        //! 持ち主が Scene に居る時、 Scene の PhysicsScene 以外はエラーを出して受け取らない
        void RemoveFromPhysics(NS::Physics::PhysicsScene& physics);

        //! 配置物ごと消える前に、 持ち主の Scene の PhysicsScene から自分の body を外す
        //! body を持ったまま Scene に居なければ外す先が分からないので、 エラーを出して id だけ手放す
        void OnEndPlay() override;

        NS_REFLECT_NONE(ColliderComponent, Component)

    private:
        // current の body を自分の形と姿勢へ置き直した id を返す
        // current が無効なら新しく作る。 入れない形状は無効を返す
        // 外から呼べると PhysicsScene の確かめを飛ばせるので private にし、 SyncToPhysics だけが呼ぶ
        [[nodiscard]] virtual JPH::BodyID SyncBody(NS::Physics::PhysicsScene& physics, JPH::BodyID current) = 0;
        // 持ち主の Scene の PhysicsScene。 Scene に居なければ null
        [[nodiscard]] NS::Physics::PhysicsScene* ScenePhysics() const noexcept;
        // Scene に居なければどの PhysicsScene も受け取る。 Scene に居る時は Scene の PhysicsScene だけ
        [[nodiscard]] bool AcceptsScenePhysics(const NS::Physics::PhysicsScene& physics) const;

        JPH::BodyID m_bodyId;
    };
} // namespace NS::Object
