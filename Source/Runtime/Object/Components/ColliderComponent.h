#pragma once

#include "Runtime/Object/Component.h"

#include <Jolt/Jolt.h>

#include <Jolt/Physics/Body/BodyID.h>

namespace NS::Phys
{
    class PhysicsScene;
}

namespace NS::Obj
{
    //! @brief 当たり形状 Component の共通基底
    //! @details どの形の body として PhysicsScene へ入れるかは派生が決める
    //! ObjectList::SyncPhysics はこの型だけを見て回るので、形状を足しても同期側は変わらない
    //! body は id でだけ持つ。どの PhysicsScene に居るかは持ち主の Scene が決め、PhysicsScene の控えは持たない
    //! 抽象基底なので TypeRegistry には登録しない
    //! 依存: NS::Phys::PhysicsScene, JPH::BodyID
    class ColliderComponent : public Component
    {
    public:
        //! world 座標の当たりを body 1 個として physics へ入れる。入れた body があれば置き直す
        //! 何も入れない形状もある。持ち主が Scene に居ない時と、持ち主の Scene 以外の PhysicsScene を
        //! 渡された時は、エラーを出して受け取らない
        void SyncToPhysics(NS::Phys::PhysicsScene& physics);

        //! 入れた body の id。何も入れなかった形状では無効
        [[nodiscard]] JPH::BodyID BodyId() const noexcept { return m_bodyId; }

        //! 自分が入れた body を physics から外す。入れていなければ何もしない
        //! 受け取る条件は SyncToPhysics と同じ
        void RemoveFromPhysics(NS::Phys::PhysicsScene& physics);

        //! 配置物ごと消える前に、持ち主の Scene の PhysicsScene から自分の body を外す
        //! body を持ったまま Scene に居なければ外す先が分からないので、エラーを出して id だけ手放す
        void OnEndPlay() override;

        NS_REFLECT_NONE(ColliderComponent, Component)

    private:
        // current の body を自分の形と姿勢へ置き直した id を返す
        // current が無効なら新しく作る。入れない形状は無効を返す
        // 外から呼べると PhysicsScene の確かめを飛ばせるので private にし、SyncToPhysics だけが呼ぶ
        [[nodiscard]] virtual JPH::BodyID SyncBody(NS::Phys::PhysicsScene& physics, JPH::BodyID current) = 0;
        // 持ち主の Scene の PhysicsScene。Scene に居なければ null
        [[nodiscard]] NS::Phys::PhysicsScene* ScenePhysics() const noexcept;
        // 受け取るのは持ち主の Scene の PhysicsScene だけ。Scene に居ない持ち主も断る
        // 断らないと、覚えている id がどの PhysicsScene の物か言えなくなる。別の PhysicsScene は
        // 同じ index と使い回し回数を配るので、無関係の body を作り変える
        [[nodiscard]] bool AcceptsScenePhysics(const NS::Phys::PhysicsScene& physics) const;

        JPH::BodyID m_bodyId;
    };
} // namespace NS::Obj
