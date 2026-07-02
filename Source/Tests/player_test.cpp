#include <gtest/gtest.h>

#include <Game/Player.h>

TEST(PlayerTest, ConstructsWithNullDependencies)
{
    Player player(nullptr, nullptr);
    // 4 つの Component (mesh / movement / input / shadow) が AddComponent 経由で登録されている
    EXPECT_EQ(player.Components().size(), 4u);
}

TEST(PlayerTest, ComponentAccessorsReturnInternalReferences)
{
    Player player(nullptr, nullptr);
    // priority 昇順 + 同 priority 内は declaration 順:
    //   [0] m_input    (Input,   0)
    //   [1] m_mesh     (Physics, 200) — Player.h で m_movement より前に宣言
    //   [2] m_movement (Physics, 200)
    //   [3] m_shadow   (Physics, 200) — 同 priority 内で最後に登録
    EXPECT_EQ(&player.InputComp(), player.Components()[0]);
    EXPECT_EQ(&player.MeshComp(), player.Components()[1]);
    EXPECT_EQ(&player.Movement(), player.Components()[2]);
    EXPECT_EQ(&player.Shadow(), player.Components()[3]);
}

TEST(PlayerTest, InputComponentResolvesMovementOnStart)
{
    Player player(nullptr, nullptr);
    player.OnStart();
    EXPECT_EQ(player.InputComp().Movement(), &player.Movement());
}
