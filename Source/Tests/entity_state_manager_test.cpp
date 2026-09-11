#include <Game/Entity/EntityState.h>
#include <Game/Entity/EntityStateManagerComponent.h>
#include <Runtime/Object/GameObject.h>
#include <Runtime/Object/StateMachine.h>
#include <gtest/gtest.h>

#include <string>
#include <string_view>
#include <vector>

namespace
{
    class FakeStateManager;

    class IdleTestState final : public NS::Game::Entity::EntityState<FakeStateManager>
    {
    public:
        static constexpr const char* k_Name = "IdleTest";

        [[nodiscard]] const char* Name() const noexcept override { return k_Name; }
        void OnStep(FakeStateManager&, float) override {}
    };

    class WalkTestState final : public NS::Game::Entity::EntityState<FakeStateManager>
    {
    public:
        static constexpr const char* k_Name = "WalkTest";

        [[nodiscard]] const char* Name() const noexcept override { return k_Name; }
        void OnStep(FakeStateManager&, float) override {}
    };

    //! 基底だけを見るための最小の派生。派生が状態機械を持つ形をそのまま写している
    class FakeStateManager final : public NS::Game::Entity::EntityStateManagerComponent
    {
    public:
        bool BuildFromList(const std::string& list) { return m_machine.Build(*this, SplitStateNames(list)); }

        //! SplitStateNames は protected なので、派生からテストへ通す
        [[nodiscard]] static std::vector<std::string> Split(const std::string& list) { return SplitStateNames(list); }

        [[nodiscard]] const char* CurrentName() const noexcept override { return m_machine.CurrentName(); }
        [[nodiscard]] bool IsBuilt() const noexcept override { return m_machine.IsBuilt(); }
        bool ChangeByName(std::string_view name) override { return m_machine.Change(*this, name); }
        void ResetToFirst() noexcept override { m_machine.Reset(); }

    private:
        NS::Object::StateMachine<FakeStateManager> m_machine;
    };

    NS_STATE(IdleTestState, FakeStateManager)
    NS_STATE(WalkTestState, FakeStateManager)
} // namespace

TEST(EntityStateManagerTest, SplitsSemicolonSeparatedNames)
{
    const std::vector<std::string> names = FakeStateManager::Split("Idle;Walk;Fall");

    ASSERT_EQ(names.size(), 3u);
    EXPECT_EQ(names[0], "Idle");
    EXPECT_EQ(names[1], "Walk");
    EXPECT_EQ(names[2], "Fall");
}

TEST(EntityStateManagerTest, SplitDropsEmptyElements)
{
    const std::vector<std::string> names = FakeStateManager::Split("Idle;;Walk;");

    ASSERT_EQ(names.size(), 2u);
    EXPECT_EQ(names[0], "Idle");
    EXPECT_EQ(names[1], "Walk");
}

TEST(EntityStateManagerTest, SplitOfEmptyListIsEmpty)
{
    EXPECT_TRUE(FakeStateManager::Split("").empty());
}

TEST(EntityStateManagerTest, SplitWithoutSeparatorIsOneName)
{
    const std::vector<std::string> names = FakeStateManager::Split("Idle");

    ASSERT_EQ(names.size(), 1u);
    EXPECT_EQ(names[0], "Idle");
}

TEST(EntityStateManagerTest, IsCurrentFollowsTheStateMachine)
{
    NS::Object::GameObject obj;
    auto& manager = *obj.AddComponent<FakeStateManager>();
    ASSERT_TRUE(manager.BuildFromList("IdleTest;WalkTest"));
    ASSERT_TRUE(manager.IsBuilt());

    EXPECT_TRUE(manager.IsCurrent(IdleTestState::k_Name));
    EXPECT_FALSE(manager.IsCurrent(WalkTestState::k_Name));

    ASSERT_TRUE(manager.ChangeByName(WalkTestState::k_Name));

    EXPECT_TRUE(manager.IsCurrent(WalkTestState::k_Name));
    EXPECT_FALSE(manager.IsCurrent(IdleTestState::k_Name));
}
