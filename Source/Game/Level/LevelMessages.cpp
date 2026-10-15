#include "Game/Level/LevelMessages.h"

#include "Game/Level/ImpactOutcome.h"
#include "NSlib/CoreObject/SceneActor.h"
#include "NSlib/CoreObject/SubObjects/HitSensor.h"
#include "NSlib/CoreObject/SubObjects/Model.h"

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

        bool SendMsgCourseRestart(NS::SceneActor& receiver, const nlohmann::json& baseline)
        {
            return NS::SendMsgToActor(MsgCourseRestart{baseline}, receiver);
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

        bool SendMsgTackleFreeze(NS::SceneActor& receiver, const TackleFreezeDesc& desc)
        {
            return NS::SendMsgToActor(MsgTackleFreeze{desc}, receiver);
        }

        bool IsMsgTackleFreeze(const NS::Message& msg) noexcept
        {
            return NS::IsMsg<MsgTackleFreeze>(msg);
        }

        bool SendMsgTackleShake(NS::SceneActor& receiver, const TackleShakeDesc& desc)
        {
            return NS::SendMsgToActor(MsgTackleShake{desc}, receiver);
        }

        bool IsMsgTackleShake(const NS::Message& msg) noexcept
        {
            return NS::IsMsg<MsgTackleShake>(msg);
        }

        NS::Vector3 TackleShakeDesc::OffsetAt(int frame) const noexcept
        {
            if (shape == TackleShakeShape::Depth)
            {
                return axis * DepthShakeOffset(frame, length, amplitude, pushFrames, returnRatio, seed);
            }
            return axis * BodyShakeOffset(frame, length, amplitude, seed, firstSign, flipFrames);
        }

        bool TackleGhostDesc::WriteTo(NS::Model& model, int frame) const noexcept
        {
            (void)model.SetGhostSpread(axis * BodyShakeReach(frame, stopLength, spread));
            if (frame == stopLength)
            {
                model.CaptureAfterimage();
            }
            const int release = frame - stopLength - 1;
            if (release < 0 || release >= releaseFrames)
            {
                (void)model.SetAfterimageOpacity(0.0f);
                return release < releaseFrames;
            }
            const float left = 1.0f - static_cast<float>(release) / static_cast<float>(releaseFrames);
            (void)model.SetAfterimageOpacity(releaseOpacity * left);
            return true;
        }

        bool SendMsgTackleGhost(NS::SceneActor& receiver, const TackleGhostDesc& desc)
        {
            return NS::SendMsgToActor(MsgTackleGhost{desc}, receiver);
        }

        bool IsMsgTackleGhost(const NS::Message& msg) noexcept
        {
            return NS::IsMsg<MsgTackleGhost>(msg);
        }

        bool SendMsgTackleTremor(NS::SceneActor& receiver, const TackleTremorDesc& desc)
        {
            return NS::SendMsgToActor(MsgTackleTremor{desc}, receiver);
        }

        bool IsMsgTackleTremor(const NS::Message& msg) noexcept
        {
            return NS::IsMsg<MsgTackleTremor>(msg);
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
