#include "Game/Player/PlayerStates.h"

#include "Game/Player/PlayerJudges.h"
#include "Game/Player/PlayerParams.h"
#include "Game/PlayerActor.h"
#include "NSlib/CoreObject/Action/ActionTimeline.h"
#include "NSlib/CoreObject/ClassObjs/Body.h"
#include "NSlib/CoreObject/ClassObjs/PlayerInput.h"

namespace GL
{
    namespace Player
    {
        void IdlePlayerState::OnStep(::PlayerActor* player, float dt)
        {
            NS_ASSERT(Game, player != nullptr, "状態の持ち主が無い");
            player->TickTimers(dt);
            player->ApplyFriction(dt);
            player->Jump(dt);
            player->CutJumpRelease();
            player->Gravity(dt);

            const NS::Body& body = player->Body();
            const bool hasInput =
                PlayerJudgeMoveInput::Judge(player->DesiredSpeedScale(), player->Params().StickDeadzone());
            const bool stopped = PlayerJudgeStopped::Judge(body.LateralVelocity());
            if (PlayerJudgeFall::Judge(body.IsGrounded()))
            {
                player->States().Change<FallPlayerState>();
            }
            else if (PlayerJudgeWalk::Judge(body.IsGrounded(), hasInput, stopped))
            {
                player->States().Change<WalkPlayerState>();
            }
        }

        void WalkPlayerState::OnStep(::PlayerActor* player, float dt)
        {
            NS_ASSERT(Game, player != nullptr, "状態の持ち主が無い");
            player->TickTimers(dt);
            const NS::Body& body = player->Body();
            const PlayerParams& params = player->Params();
            const bool hasInput = PlayerJudgeMoveInput::Judge(player->DesiredSpeedScale(), params.StickDeadzone());
            const bool brake = PlayerJudgeBrake::Judge(
                hasInput, player->DesiredDirection(), body.LateralVelocity(), params.BrakeThreshold());
            if (!brake && hasInput)
            {
                player->AccelerateToInputDirection(dt);
            }
            else if (!brake)
            {
                player->ApplyFriction(dt);
            }
            player->Jump(dt);
            player->CutJumpRelease();
            player->Gravity(dt);

            const bool stopped = PlayerJudgeStopped::Judge(body.LateralVelocity());
            if (PlayerJudgeFall::Judge(body.IsGrounded()))
            {
                player->States().Change<FallPlayerState>();
            }
            else if (brake)
            {
                player->States().Change<BrakePlayerState>();
            }
            else if (PlayerJudgeIdle::Judge(body.IsGrounded(), hasInput, stopped))
            {
                player->States().Change<IdlePlayerState>();
            }
        }

        void BrakePlayerState::OnStep(::PlayerActor* player, float dt)
        {
            NS_ASSERT(Game, player != nullptr, "状態の持ち主が無い");
            player->TickTimers(dt);
            player->ApplyBrake(dt);
            player->Jump(dt);
            player->CutJumpRelease();
            player->Gravity(dt);

            const NS::Body& body = player->Body();
            if (PlayerJudgeFall::Judge(body.IsGrounded()))
            {
                player->States().Change<FallPlayerState>();
            }
            else if (PlayerJudgeStopped::Judge(body.LateralVelocity()))
            {
                player->States().Change<IdlePlayerState>();
            }
        }

        void FallPlayerState::OnStep(::PlayerActor* player, float dt)
        {
            NS_ASSERT(Game, player != nullptr, "状態の持ち主が無い");
            player->TickTimers(dt);
            player->AdvanceAirControlReturn(dt);
            player->AccelerateToInputDirection(dt);
            player->Jump(dt);
            player->CutJumpRelease();
            player->Gravity(dt);
            if (player->LedgeGrab())
            {
                return;
            }

            if (player->Body().IsGrounded())
            {
                player->States().Change<IdlePlayerState>();
            }
        }

        void LedgeHangingPlayerState::OnStep(::PlayerActor* player, float dt)
        {
            NS_ASSERT(Game, player != nullptr, "状態の持ち主が無い");
            if (!player->HoldLedge())
            {
                return;
            }
            if (player->LedgeJump())
            {
                return;
            }
            if (PlayerJudgeClimbLedge::Judge(player->ClimbForward()))
            {
                player->ClimbLedge();
                return;
            }
            if (PlayerJudgeDropLedge::Judge(player->Input().ReleaseLedgePressed()))
            {
                player->DropLedge();
                return;
            }
            player->Shimmy(dt);
        }

        void LedgeClimbingPlayerState::OnEnter(::PlayerActor* player)
        {
            NS_ASSERT(Game, player != nullptr, "状態の持ち主が無い");
            StartCoroutine(Run(player));
        }

        NS::Coroutine LedgeClimbingPlayerState::Run(::PlayerActor* player)
        {
            NS_ASSERT(Game, player != nullptr, "状態の持ち主が無い");
            while (true)
            {
                co_await NS::NextFrame{};
                player->UpdateLedgeClimb(StepDelta());
                if (!player->States().IsCurrent<LedgeClimbingPlayerState>())
                {
                    co_return;
                }
            }
        }

        void AirDashPlayerState::OnEnter(::PlayerActor* player)
        {
            NS_ASSERT(Game, player != nullptr, "状態の持ち主が無い");
            // 動作が始まらなければ終わりが来ないので、入った所で普段の動きへ移る。予約は次の確定で効く
            if (!player->StartAirDashAction())
            {
                player->ChangeToWalkOrFall();
            }
        }

        void AirDashPlayerState::OnStep(::PlayerActor* player, float)
        {
            NS_ASSERT(Game, player != nullptr, "状態の持ち主が無い");
            if (NS::IsStateEnd(player))
            {
                // 当たらずに終わった時だけ勢いを残す。当たった時は反動の速度が要るので OnExit には置かない
                player->KeepAirDashExitSpeed();
                player->ChangeToWalkOrFall();
                return;
            }
            player->KeepAirDashSpeed();
        }

        void BodySlamPlayerState::OnStep(::PlayerActor* player, float dt)
        {
            NS_ASSERT(Game, player != nullptr, "状態の持ち主が無い");
            player->UpdateBodySlam(dt);
        }

        void BeforeContactPlayerState::OnEnter(::PlayerActor* player)
        {
            NS_ASSERT(Game, player != nullptr, "状態の持ち主が無い");
            player->BeginBeforeContact();
        }

        void BeforeContactPlayerState::OnStep(::PlayerActor* player, float dt)
        {
            NS_ASSERT(Game, player != nullptr, "状態の持ち主が無い");
            player->UpdateBodySlam(dt);
        }

        void BeforeContactPlayerState::OnExit(::PlayerActor* player)
        {
            NS_ASSERT(Game, player != nullptr, "状態の持ち主が無い");
            player->EndBeforeContact();
        }

        void TackleHitPlayerState::OnEnter(::PlayerActor* player)
        {
            NS_ASSERT(Game, player != nullptr, "状態の持ち主が無い");
            player->BeginTackleHit();
        }

        void ReboundPlayerState::OnEnter(::PlayerActor* player)
        {
            NS_ASSERT(Game, player != nullptr, "状態の持ち主が無い");
            // 沈みの跳ね返りは反動に入るフレームに始まる (束「反動」)
            player->StartReboundReaction();
            StartCoroutine(Run(player));
        }

        NS::Coroutine ReboundPlayerState::Run(::PlayerActor* player)
        {
            NS_ASSERT(Game, player != nullptr, "状態の持ち主が無い");
            while (true)
            {
                co_await NS::NextFrame{};
                const float dt = StepDelta();
                player->TickTimers(dt);
                player->AccelerateDuringRebound(dt);
                player->ReboundGravity(dt);
                if (player->LedgeGrab())
                {
                    co_return;
                }

                if (player->IsReboundLanded())
                {
                    // 外れは着いた後もこすって止まるまで操作が戻らない。真ん中は着いたフレームに立ちへ戻る
                    if (player->SkidsOnLanding())
                    {
                        player->States().Change<SkidPlayerState>();
                    }
                    else
                    {
                        player->States().Change<IdlePlayerState>();
                    }
                    co_return;
                }
            }
        }

        void DashReboundPlayerState::OnStep(::PlayerActor* player, float dt)
        {
            NS_ASSERT(Game, player != nullptr, "状態の持ち主が無い");
            player->UpdateDashRebound(dt);
        }

        void SkidPlayerState::OnEnter(::PlayerActor* player)
        {
            NS_ASSERT(Game, player != nullptr, "状態の持ち主が無い");
            player->BeginSkid();
        }

        void SkidPlayerState::OnStep(::PlayerActor* player, float dt)
        {
            NS_ASSERT(Game, player != nullptr, "状態の持ち主が無い");
            player->TickTimers(dt);
            const bool stopped = player->AdvanceSkid();
            player->Gravity(dt);

            if (PlayerJudgeFall::Judge(player->Body().IsGrounded()))
            {
                player->States().Change<FallPlayerState>();
            }
            else if (stopped)
            {
                player->States().Change<IdlePlayerState>();
            }
        }
    } // namespace Player
} // namespace GL
