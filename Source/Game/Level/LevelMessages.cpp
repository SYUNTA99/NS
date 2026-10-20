#include "Game/Level/LevelMessages.h"

#include "Game/Level/TackleGhost.h"
#include "NSlib/CoreObject/ClassObjs/HitSensor.h"
#include "NSlib/CoreObject/SceneActor.h"

namespace GL
{
    namespace Level
    {
        bool SendMsgInstantDeath(NS::HitSensor& receiver, NS::HitSensor& sender)
        {
            return NS::SendMsg(MsgInstantDeath{}, receiver, &sender);
        }

        bool IsMsgInstantDeath(const NS::Message& msg) noexcept
        {
            return NS::IsMsg<MsgInstantDeath>(msg);
        }

        bool SendMsgGoal(NS::HitSensor& receiver, NS::HitSensor& sender, float fadeOutSeconds, float fadeInSeconds)
        {
            return NS::SendMsg(MsgGoal{fadeOutSeconds, fadeInSeconds}, receiver, &sender);
        }

        bool IsMsgGoal(const NS::Message& msg) noexcept
        {
            return NS::IsMsg<MsgGoal>(msg);
        }

        bool SendMsgCourseStart(NS::SceneActor& receiver)
        {
            return NS::SendMsgToActor(MsgCourseStart{}, receiver);
        }

        bool IsMsgCourseStart(const NS::Message& msg) noexcept
        {
            return NS::IsMsg<MsgCourseStart>(msg);
        }

        bool SendMsgCourseRestart(NS::SceneActor& receiver)
        {
            return NS::SendMsgToActor(MsgCourseRestart{}, receiver);
        }

        bool IsMsgCourseRestart(const NS::Message& msg) noexcept
        {
            return NS::IsMsg<MsgCourseRestart>(msg);
        }

        bool SendMsgInputLock(NS::SceneActor& receiver, bool locked)
        {
            return NS::SendMsgToActor(MsgInputLock{locked}, receiver);
        }

        bool IsMsgInputLock(const NS::Message& msg) noexcept
        {
            return NS::IsMsg<MsgInputLock>(msg);
        }

        bool SendMsgAskTackleTarget(NS::HitSensor& receiver, TackleTargetAnswer& outAnswer)
        {
            return NS::SendMsg(MsgAskTackleTarget{outAnswer}, receiver, nullptr);
        }

        bool IsMsgAskTackleTarget(const NS::Message& msg) noexcept
        {
            return NS::IsMsg<MsgAskTackleTarget>(msg);
        }

        bool SendMsgTackleHit(NS::HitSensor& receiver, NS::HitSensor& sender, const TackleHitDesc& desc)
        {
            return NS::SendMsg(MsgTackleHit{desc}, receiver, &sender);
        }

        bool IsMsgTackleHit(const NS::Message& msg) noexcept
        {
            return NS::IsMsg<MsgTackleHit>(msg);
        }

        bool TackleGhostDesc::WriteTo(TackleGhost* ghost, int frame) const noexcept
        {
            NS_ASSERT(Game, ghost != nullptr, "残像が渡されていない");
            (void)ghost->SetSpread(axis * NS::BodyShakeReach(frame, stopLength, spread));
            (void)ghost->SetSideOpacity(sideOpacity);
            if (frame == stopLength)
            {
                ghost->CaptureAfterimage();
            }
            const int release = frame - stopLength - 1;
            if (release < 0 || release >= releaseFrames)
            {
                (void)ghost->SetAfterimageOpacity(0.0f);
                return release < releaseFrames;
            }
            const float left = 1.0f - static_cast<float>(release) / static_cast<float>(releaseFrames);
            (void)ghost->SetAfterimageOpacity(releaseOpacity * left);
            return true;
        }

        bool SendMsgTackleRelease(NS::SceneActor& receiver, const TackleReleaseDesc& desc)
        {
            return NS::SendMsgToActor(MsgTackleRelease{desc}, receiver);
        }

        bool IsMsgTackleRelease(const NS::Message& msg) noexcept
        {
            return NS::IsMsg<MsgTackleRelease>(msg);
        }
    } // namespace Level
} // namespace GL
