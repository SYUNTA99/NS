#pragma once

#include "NSlib/CoreObject/StateMachineOf.h"

class PlayerActor;

namespace GL
{
    namespace Player
    {
        /// PlayerActor の行動と状態遷移。共通の遷移条件は PlayerJudges で判定する
        template <typename TState> using PlayerState = NS::StateOf<TState, ::PlayerActor>;
    } // namespace Player
} // namespace GL
