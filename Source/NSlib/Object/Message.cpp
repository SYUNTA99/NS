#include "NSlib/Object/Message.h"

#include "NSlib/Object/Actor.h"
#include "NSlib/Object/SubObjects/HitSensor.h"

namespace NS::Obj
{
    bool SendMsg(const Message& msg, HitSensor& receiver, HitSensor* sender)
    {
        Actor* owner = receiver.Owner();
        if (owner == nullptr || !receiver.IsValid())
        {
            return false;
        }
        // 自分宛ては送らない。自分のセンサーどうしの重なりは調べ役も数えない
        if (sender != nullptr && sender->Owner() == owner)
        {
            return false;
        }
        return owner->ReceiveMsg(msg, sender, &receiver);
    }

    bool SendMsgToActor(const Message& msg, Actor& receiver)
    {
        return receiver.ReceiveMsg(msg, nullptr, nullptr);
    }
} // namespace NS::Obj
