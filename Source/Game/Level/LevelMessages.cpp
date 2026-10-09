#include "Game/Level/LevelMessages.h"

#include "Game/Level/ImpactOutcome.h"
#include "NSlib/Object/Actor.h"
#include "NSlib/Object/SubObjects/HitSensor.h"
#include "NSlib/Object/SubObjects/Model.h"

namespace GL::Level
{
    bool SendMsgInstantDeath(NS::Obj::HitSensor& receiver, NS::Obj::HitSensor& sender)
    {
        return NS::Obj::SendMsg(MsgInstantDeath{}, receiver, &sender);
    }

    bool IsMsgInstantDeath(const NS::Obj::Message& msg) noexcept
    {
        return NS::Obj::IsMsg<MsgInstantDeath>(msg);
    }

    bool SendMsgGoal(NS::Obj::HitSensor& receiver,
                     NS::Obj::HitSensor& sender,
                     float fadeOutSeconds,
                     float fadeInSeconds)
    {
        return NS::Obj::SendMsg(MsgGoal{fadeOutSeconds, fadeInSeconds}, receiver, &sender);
    }

    bool IsMsgGoal(const NS::Obj::Message& msg) noexcept
    {
        return NS::Obj::IsMsg<MsgGoal>(msg);
    }

    bool SendMsgCourseRestart(NS::Obj::Actor& receiver, const nlohmann::json& baseline)
    {
        return NS::Obj::SendMsgToActor(MsgCourseRestart{baseline}, receiver);
    }

    bool IsMsgCourseRestart(const NS::Obj::Message& msg) noexcept
    {
        return NS::Obj::IsMsg<MsgCourseRestart>(msg);
    }

    bool SendMsgInputLock(NS::Obj::Actor& receiver, bool locked)
    {
        return NS::Obj::SendMsgToActor(MsgInputLock{locked}, receiver);
    }

    bool IsMsgInputLock(const NS::Obj::Message& msg) noexcept
    {
        return NS::Obj::IsMsg<MsgInputLock>(msg);
    }

    bool SendMsgAskTackleTarget(NS::Obj::HitSensor& receiver, TackleTargetAnswer& outAnswer)
    {
        return NS::Obj::SendMsg(MsgAskTackleTarget{outAnswer}, receiver, nullptr);
    }

    bool IsMsgAskTackleTarget(const NS::Obj::Message& msg) noexcept
    {
        return NS::Obj::IsMsg<MsgAskTackleTarget>(msg);
    }

    bool SendMsgTackleFreeze(NS::Obj::Actor& receiver, const TackleFreezeDesc& desc)
    {
        return NS::Obj::SendMsgToActor(MsgTackleFreeze{desc}, receiver);
    }

    bool IsMsgTackleFreeze(const NS::Obj::Message& msg) noexcept
    {
        return NS::Obj::IsMsg<MsgTackleFreeze>(msg);
    }

    bool SendMsgTackleShake(NS::Obj::Actor& receiver, const TackleShakeDesc& desc)
    {
        return NS::Obj::SendMsgToActor(MsgTackleShake{desc}, receiver);
    }

    bool IsMsgTackleShake(const NS::Obj::Message& msg) noexcept
    {
        return NS::Obj::IsMsg<MsgTackleShake>(msg);
    }

    NS::Vector3 TackleShakeDesc::OffsetAt(int frame) const noexcept
    {
        if (shape == TackleShakeShape::Depth)
        {
            return axis * DepthShakeOffset(frame, length, amplitude, pushFrames, returnRatio, seed);
        }
        return axis * BodyShakeOffset(frame, length, amplitude, seed, firstSign, flipFrames);
    }

    bool TackleGhostDesc::WriteTo(NS::Obj::Model& model, int frame) const noexcept
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

    bool SendMsgTackleGhost(NS::Obj::Actor& receiver, const TackleGhostDesc& desc)
    {
        return NS::Obj::SendMsgToActor(MsgTackleGhost{desc}, receiver);
    }

    bool IsMsgTackleGhost(const NS::Obj::Message& msg) noexcept
    {
        return NS::Obj::IsMsg<MsgTackleGhost>(msg);
    }

    bool SendMsgTackleTremor(NS::Obj::Actor& receiver, const TackleTremorDesc& desc)
    {
        return NS::Obj::SendMsgToActor(MsgTackleTremor{desc}, receiver);
    }

    bool IsMsgTackleTremor(const NS::Obj::Message& msg) noexcept
    {
        return NS::Obj::IsMsg<MsgTackleTremor>(msg);
    }

    bool SendMsgTackleRelease(NS::Obj::Actor& receiver, const TackleReleaseDesc& desc)
    {
        return NS::Obj::SendMsgToActor(MsgTackleRelease{desc}, receiver);
    }

    bool IsMsgTackleRelease(const NS::Obj::Message& msg) noexcept
    {
        return NS::Obj::IsMsg<MsgTackleRelease>(msg);
    }
} // namespace GL::Level
