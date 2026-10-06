#include "Game/Level/LevelMessages.h"

#include "NSlib/Object/Actor.h"
#include "NSlib/Object/Components/HitSensor.h"

namespace NS::Game::Level
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
} // namespace NS::Game::Level
