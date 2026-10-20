#include "Game/Player/States/ReboundPlayerState.h"

#include "Game/Player/States/IdlePlayerState.h"
#include "Game/Player/States/SkidPlayerState.h"
#include "Game/PlayerActor.h"

namespace GL
{
    namespace Player
    {
        void ReboundPlayerState::OnEnter(::PlayerActor& player)
        {
            StartCoroutine(Run(player));
        }

        NS::Coroutine ReboundPlayerState::Run(::PlayerActor& player)
        {
            while (true)
            {
                co_await NS::NextFrame{};
                const float dt = StepDelta();
                player.TickTimers(dt);
                player.AccelerateDuringRebound(dt);
                player.ReboundGravity(dt);
                if (player.LedgeGrab())
                {
                    co_return;
                }

                if (player.IsReboundLanded())
                {
                    // 外れは着いた後もこすって止まるまで操作が戻らない。真ん中は着いたフレームに立ちへ戻る
                    if (player.SkidsOnLanding())
                    {
                        player.States().Change<SkidPlayerState>();
                    }
                    else
                    {
                        player.States().Change<IdlePlayerState>();
                    }
                    co_return;
                }
            }
        }
    } // namespace Player
} // namespace GL
