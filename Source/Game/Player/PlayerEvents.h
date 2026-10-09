#pragma once

#include "NSlib/Object/SubObjects/BodyEvents.h"

namespace GL::Player
{
    //! @brief 自機の通知
    //! @details 命の増減は持たない。増減を扱うのは Health
    //! 回転攻撃・持ち上げ・投げ・踏みつけ・空中ダイブ・バク宙・滑空・ダッシュ・しゃがみも持たない。その遊びが無い
    //! 体当たりの 2 つはこの作品だけの通知
    struct PlayerEvents
    {
        NS::Obj::BodyEvent onJump;            //!< 跳んだフレーム
        NS::Obj::BodyEvent onLedgeGrabbed;    //!< 縁を掴んだフレーム
        NS::Obj::BodyEvent onLedgeClimbing;   //!< よじ登りを始めたフレーム
        NS::Obj::BodyEvent onBodySlamStarted; //!< 体当たりが出たフレーム
        NS::Obj::BodyEvent onBodySlamEnded;   //!< 体当たりが終わったフレーム
    };
} // namespace GL::Player
