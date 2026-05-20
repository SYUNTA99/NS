#include <gtest/gtest.h>

#include <Game/Player.h>

TEST(PlayerTest, ConstructsWithNullDependencies)
{
    Player player(nullptr, nullptr, nullptr);
    // 3 つの Component (mesh / movement / input) が RegisterComponent 経由で登録されている。
    EXPECT_EQ(player.Components().size(), 3u);
}

TEST(PlayerTest, ComponentAccessorsReturnInternalReferences)
{
    Player player(nullptr, nullptr, nullptr);
    // input -> movement -> mesh の順で登録される (jump 入力を同 step 内で movement に
    // 反映するため)。
    EXPECT_EQ(&player.InputComp(), player.Components()[0]);
    EXPECT_EQ(&player.Movement(), player.Components()[1]);
    EXPECT_EQ(&player.MeshComp(), player.Components()[2]);
}

TEST(PlayerTest, InputComponentReferencesMovement)
{
    Player player(nullptr, nullptr, nullptr);
    EXPECT_EQ(player.InputComp().Movement(), &player.Movement());
}
