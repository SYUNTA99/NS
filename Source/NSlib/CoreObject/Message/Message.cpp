#include "NSlib/CoreObject/Message/Message.h"

#include "NSlib/CoreObject/ClassObjs/HitSensor.h"
#include "NSlib/CoreObject/SceneActor.h"

namespace NS
{
    bool SendMsg(const Message& msg, HitSensor& receiver, HitSensor* sender)
    {
        SceneActor* owner = receiver.Host();
        if (owner == nullptr || !receiver.IsValid())
        {
            return false;
        }
        // 自分宛ては送らない。自分のセンサーどうしの重なりは調べ役も数えない
        if (sender != nullptr && sender->Host() == owner)
        {
            return false;
        }
        return owner->ReceiveMsg(msg, sender, &receiver);
    }

    bool SendMsgToActor(const Message& msg, SceneActor& receiver)
    {
        return receiver.ReceiveMsg(msg, nullptr, nullptr);
    }
} // namespace NS
