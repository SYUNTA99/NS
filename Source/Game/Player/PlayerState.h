#pragma once

#include "NSlib/Object/StateMachineOf.h"

class PlayerActor;

namespace GL::Player
{
    //! @brief PlayerActor の行動と状態遷移。共通の遷移条件は PlayerJudges で判定する
    template <typename TState> using PlayerState = NS::Obj::StateOf<TState, ::PlayerActor>;
} // namespace GL::Player
