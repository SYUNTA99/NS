#pragma once

#include "NSlib/Object/SubObjects/BodyEvents.h"

namespace GL::Player
{
    //! @brief 自機の行動を始めた・終えた時の通知
    struct PlayerEvents
    {
        NS::Obj::BodyEvent onJump;            //!< 跳んだフレーム
        NS::Obj::BodyEvent onLedgeGrabbed;    //!< 縁を掴んだフレーム
        NS::Obj::BodyEvent onLedgeClimbing;   //!< よじ登りを始めたフレーム
        NS::Obj::BodyEvent onBodySlamStarted; //!< 体当たりが出たフレーム
        NS::Obj::BodyEvent onBodySlamEnded;   //!< 体当たりが終わったフレーム
    };
} // namespace GL::Player
