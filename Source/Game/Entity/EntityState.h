#pragma once

#include "Runtime/Object/StateMachine.h"

namespace NS::Game::Entity
{
    //! @brief 登場人物の 1 状態
    //! @details 自機も敵も同じ型で状態を書く。中身は NS::Object::State そのままで、足した物は無い
    template <typename TOwner> using EntityState = NS::Object::State<TOwner>;

    // 状態に持たせない物
    // 入ってからの経過秒: 所有者側のタイマーが数えているので、二重に持つと正が 2 つになる
    // 直前の状態と状態の添字: 呼ぶ側が 1 つも無い
    // 型で現在状態を問う関数: 登録名の比較で足りる
} // namespace NS::Game::Entity
