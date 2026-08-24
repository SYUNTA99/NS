#include <Game/Entity/EntityStatsManagerComponent.h>
#include <Runtime/Object/GameObject.h>
#include <gtest/gtest.h>

#include <array>
#include <cstddef>

namespace
{
    struct FakeStats
    {
        float walkSpeed = 4.0f;
    };

    //! 基底だけを見るための最小の派生。組を型付きの配列で持つ形をそのまま写している
    template <std::size_t N> class FakeStatsManager final : public NS::Game::Entity::EntityStatsManagerComponent
    {
    public:
        [[nodiscard]] std::size_t StatsCount() const noexcept override { return m_sets.size(); }

        [[nodiscard]] int ChangedCount() const noexcept { return m_changedCount; }

    protected:
        void OnStatsChanged() noexcept override { ++m_changedCount; }

    private:
        std::array<FakeStats, N> m_sets{};
        int m_changedCount = 0;
    };

    using OneSetManager = FakeStatsManager<1>;
    using TwoSetManager = FakeStatsManager<2>;
} // namespace

TEST(EntityStatsManagerTest, StartsAtTheFirstSet)
{
    NS::Object::GameObject obj;
    auto& manager = *obj.AddComponent<OneSetManager>();

    EXPECT_EQ(manager.StatsCount(), 1u);
    EXPECT_EQ(manager.CurrentIndex(), 0u);
    EXPECT_EQ(manager.ChangedCount(), 0);
}

TEST(EntityStatsManagerTest, OutOfRangeIndexIsRejected)
{
    NS::Object::GameObject obj;
    auto& manager = *obj.AddComponent<OneSetManager>();

    EXPECT_FALSE(manager.Change(1));
    EXPECT_EQ(manager.CurrentIndex(), 0u);
    EXPECT_EQ(manager.ChangedCount(), 0);
}

TEST(EntityStatsManagerTest, ChangeToTheCurrentIndexDoesNotNotify)
{
    NS::Object::GameObject obj;
    auto& manager = *obj.AddComponent<OneSetManager>();

    EXPECT_TRUE(manager.Change(0));
    EXPECT_EQ(manager.CurrentIndex(), 0u);
    EXPECT_EQ(manager.ChangedCount(), 0);
}

TEST(EntityStatsManagerTest, ChangeMovesToTheSecondSet)
{
    NS::Object::GameObject obj;
    auto& manager = *obj.AddComponent<TwoSetManager>();

    ASSERT_TRUE(manager.Change(1));

    EXPECT_EQ(manager.CurrentIndex(), 1u);
    EXPECT_EQ(manager.ChangedCount(), 1);
}
