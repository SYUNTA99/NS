#include "Game/Game.h"

#include <gtest/gtest.h>

// 隠したカーソルは 1 回目の Esc で出す。エディタの有無に依らない
TEST(NsGameEscape, FirstEscapeReleasesTheHiddenCursor)
{
    EXPECT_EQ(Game::ResolveEscape({.cursorVisible = false, .secondEscapeQuits = true}),
              Game::EscapeResponse::ReleaseCursor);
    EXPECT_EQ(Game::ResolveEscape({.cursorVisible = false, .secondEscapeQuits = false}),
              Game::EscapeResponse::ReleaseCursor);
}

// 出荷のゲームは出ている状態の 2 回目で終わる。エディタのプレイは 2 回目でもアプリを落とさない
TEST(NsGameEscape, SecondEscapeQuitsOnlyWhenAllowed)
{
    EXPECT_EQ(Game::ResolveEscape({.cursorVisible = true, .secondEscapeQuits = true}), Game::EscapeResponse::Quit);
    EXPECT_EQ(Game::ResolveEscape({.cursorVisible = true, .secondEscapeQuits = false}), Game::EscapeResponse::None);
}

// 出荷の Game.exe はエディタが居ないので、何も設定しなければ 2 回目の Esc で終わる
TEST(NsGameEscape, SecondEscapeQuitsByDefault)
{
    Game game;
    EXPECT_TRUE(game.SecondEscapeQuits());
    game.SetSecondEscapeQuits(false);
    EXPECT_FALSE(game.SecondEscapeQuits());
}
