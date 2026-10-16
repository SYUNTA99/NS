#pragma once

#include "NSlib/CoreObject/IUse/IUseState.h"
#include "NSlib/CoreObject/SubObject.h"

#include <memory>

namespace NS
{
    //! @brief 持ち主の状態機械を持つ部品
    //! @details 機械は持ち主の型ごとの StateMachineOf で、組む時に持ち主を控える。
    //! 部品は持ち主の型を知らずに IStateMachine で進める。組むのは SceneActor::BuildStateMachine だけ
    class StateMachine final : public SubObject
    {
    public:
        explicit StateMachine(SceneActor* owner) noexcept : m_owner(owner) {}

        //! 組んだ機械を預かる。預かるのは 1 回だけで、2 回目は呼び手のバグ
        void Hold(std::unique_ptr<IStateMachine> machine) noexcept;

        [[nodiscard]] IStateMachine* Machine() noexcept { return m_machine.get(); }
        [[nodiscard]] const IStateMachine* Machine() const noexcept { return m_machine.get(); }

        void Update();

        NS_REFLECT_NONE(StateMachine, SubObject)

    private:
        std::unique_ptr<IStateMachine> m_machine;
        SceneActor* m_owner = nullptr; // 持ち主 (非所有)
    };
} // namespace NS
