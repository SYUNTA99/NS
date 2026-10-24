#include "NSlib/CoreObject/Message/Message.h"

#include "NSlib/CoreObject/ClassObjs/HitSensor.h"
#include "NSlib/CoreObject/SceneActor.h"

namespace NS
{
    bool SendMsg(const Message& msg, HitSensor* receiver, HitSensor* sender)
    {
        NS_ASSERT(Scene, receiver != nullptr, "SendMsg: 渡したアクターか部品が無い");
        SceneActor* owner = receiver->Host();
        if (owner == nullptr || !receiver->IsValid())
        {
            return false;
        }
        // 自分宛ては送らない。自分のセンサーどうしの重なりは調べ役も数えない
        if (sender != nullptr && sender->Host() == owner)
        {
            return false;
        }
        return owner->ReceiveMsg(msg, sender, receiver);
    }

    bool SendMsgToActor(const Message& msg, SceneActor* receiver)
    {
        NS_ASSERT(Scene, receiver != nullptr, "SendMsgToActor: 渡したアクターか部品が無い");
        return receiver->ReceiveMsg(msg, nullptr, nullptr);
    }
} // namespace NS
