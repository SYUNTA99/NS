#pragma once

#include "Game/Entity/EntityEvents.h"

namespace NS::Game::Player
{
    //! @brief 自機の通知
    //! @details 命の増減は持たない。増減を扱うのは HealthComponent
    //! 回転攻撃・持ち上げ・投げ・踏みつけ・空中ダイブ・バク宙・滑空・ダッシュ・しゃがみも持たない。その遊びが無い
    //! 体当たりの 2 つはこの作品だけの通知
    struct PlayerEvents
    {
        NS::Game::Entity::EntityEvent onJump;            //!< 跳んだ歩
        NS::Game::Entity::EntityEvent onLedgeGrabbed;    //!< 縁を掴んだ歩
        NS::Game::Entity::EntityEvent onLedgeClimbing;   //!< よじ登りを始めた歩
        NS::Game::Entity::EntityEvent onBodySlamStarted; //!< 体当たりが出た歩
        NS::Game::Entity::EntityEvent onBodySlamEnded;   //!< 体当たりが終わった歩
    };
} // namespace NS::Game::Player
