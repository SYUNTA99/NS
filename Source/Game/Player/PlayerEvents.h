#pragma once

#include "NSlib/CoreObject/ClassObjs/BodyEvents.h"

namespace GL
{
    namespace Player
    {
        //! 自機の行動を始めた・終えた時の通知
        struct PlayerEvents
        {
            NS::BodyEvent onJump;            // 跳んだフレーム
            NS::BodyEvent onLedgeGrabbed;    // 縁を掴んだフレーム
            NS::BodyEvent onLedgeClimbing;   // よじ登りを始めたフレーム
            NS::BodyEvent onBodySlamStarted; // 体当たりが出たフレーム
            NS::BodyEvent onBodySlamEnded;   // 体当たりが終わったフレーム
        };
    } // namespace Player
} // namespace GL
