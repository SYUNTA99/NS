#pragma once

#include "NSlib/CoreObject/Object.h"
#include "NSlib/CoreObject/Reflection/Reflection.h"

#include <concepts>

namespace NS
{
    class SceneActor;
    class Scene;
    class Transform;

    //! @brief SceneActor が所有する部品。自分からは動かず、持ち主が部品の関数を名指しで呼ぶ
    //! @details 基底に更新と出入りの仮想関数は無い。体の経過秒は BodyDelta から取る
    class SubObject : public Object
    {
    public:
        SubObject() noexcept;

        virtual ~SubObject() noexcept;

        //! 持ち主の SceneActor。シーンへの所属後は必ず設定される
        [[nodiscard]] SceneActor* Owner() noexcept { return m_owner; }
        [[nodiscard]] const SceneActor* Owner() const noexcept { return m_owner; }

        //! 持ち主の居る Scene。持ち主が無いか Scene に居なければ nullptr
        [[nodiscard]] Scene* OwningScene() const noexcept;

        //! 持ち主の体の 1 歩の秒。持ち主が無ければ固定の 1 歩の秒
        [[nodiscard]] float BodyDelta() const noexcept;

        [[nodiscard]] Transform& RootTransform() noexcept;
        [[nodiscard]] const Transform& RootTransform() const noexcept;

        //! 持ち主が出ているか。持ち主に着く前は出ていると答える
        [[nodiscard]] bool IsOwnerAppeared() const noexcept;

        NS_REFLECT_NONE(SubObject, Object)

    private:
        friend class SceneActor;
        void AttachOwner(SceneActor* owner) noexcept { m_owner = owner; }

        SceneActor* m_owner = nullptr; // 所有 SceneActor、attach 前は nullptr
    };

} // namespace NS
