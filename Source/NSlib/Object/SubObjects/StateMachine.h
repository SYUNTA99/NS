#pragma once

#include "NSlib/Object/IUse/IUseState.h"
#include "NSlib/Object/SubObject.h"

#include <memory>

namespace NS::Obj
{
    //! @brief 持ち主の状態機械を持つ部品
    //! @details 機械は持ち主の型ごとの StateMachineOf で、組む時に持ち主を控える。
    //! 部品は持ち主の型を知らずに IStateMachine で進める。組むのは SceneActor::BuildStateMachine だけ
    class StateMachine final : public SubObject
    {
    public:
        StateMachine() noexcept = default;

        //! 組んだ機械を預かる。預かるのは 1 回だけで、2 回目は呼び手のバグ
        void Hold(std::unique_ptr<IStateMachine> machine) noexcept;

        [[nodiscard]] IStateMachine* Machine() noexcept { return m_machine.get(); }
        [[nodiscard]] const IStateMachine* Machine() const noexcept { return m_machine.get(); }

        void Update();

        NS_REFLECT_NONE(StateMachine, SubObject)

    private:
        std::unique_ptr<IStateMachine> m_machine;
    };
} // namespace NS::Obj
