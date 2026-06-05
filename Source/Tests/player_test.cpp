#include <gtest/gtest.h>

#include <Game/Player.h>

TEST(PlayerTest, ConstructsWithNullDependencies)
{
    Player player(nullptr, nullptr, nullptr);
    // 3 つの Component (mesh / movement / input) が AddComponent 経由で登録されている
    EXPECT_EQ(player.Components().size(), 3u);
}

TEST(PlayerTest, ComponentAccessorsReturnInternalReferences)
{
    Player player(nullptr, nullptr, nullptr);
    // priority 昇順 + 同 priority 内は declaration 順:
    //   [0] m_input    (Input,   0)
    //   [1] m_mesh     (Physics, 200) — Player.h で m_movement より前に宣言
    //   [2] m_movement (Physics, 200)
    EXPECT_EQ(&player.InputComp(), player.Components()[0]);
    EXPECT_EQ(&player.MeshComp(), player.Components()[1]);
    EXPECT_EQ(&player.Movement(), player.Components()[2]);
}

TEST(PlayerTest, InputComponentReferencesMovement)
{
    Player player(nullptr, nullptr, nullptr);
    EXPECT_EQ(player.InputComp().Movement(), &player.Movement());
}
