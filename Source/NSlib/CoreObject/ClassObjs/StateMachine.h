#pragma once

#include "NSlib/CoreObject/IGet/IGetState.h"
#include "NSlib/CoreObject/Object.h"

#include <memory>

namespace NS
{
    //! 持ち主の状態機械を持つ部品
    //! 機械は持ち主の型ごとの StateMachineOf で、組む時に持ち主を控える。
    //! 部品は持ち主の型を知らずに IStateMachine で進める。組むのは SceneActor::BuildStateMachine だけ
    class StateMachine final : public Object
    {
    public:

        //! 組んだ機械を預かる。預かるのは 1 回だけで、2 回目は呼び手のバグ
        void Hold(std::unique_ptr<IStateMachine> machine) noexcept;

        [[nodiscard]] IStateMachine* Machine() noexcept { return m_machine.get(); }
        [[nodiscard]] const IStateMachine* Machine() const noexcept { return m_machine.get(); }

        //! 機械を dt (体の倍率を掛けた 1 フレームの秒) だけ進める。機械が無ければ何もしない
        void Update(float dt);

        NS_REFLECT_NONE(StateMachine, Object)

    private:
        std::unique_ptr<IStateMachine> m_machine;
    };
} // namespace NS
