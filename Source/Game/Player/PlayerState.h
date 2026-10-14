#pragma once

#include "NSlib/Object/StateMachine.h"

class Player;

namespace GL::Player
{
    //! @brief Player の行動と状態遷移。共通の遷移条件は PlayerJudges で判定する
    template <typename TState> using PlayerState = NS::Obj::StateOf<TState, ::Player>;
} // namespace GL::Player
