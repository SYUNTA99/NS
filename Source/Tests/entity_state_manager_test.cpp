#include <Game/Entity/EntityState.h>
#include <Game/Entity/EntityStateManager.h>
#include <Runtime/Object/GameObject.h>
#include <Runtime/Object/StateMachine.h>
#include <gtest/gtest.h>

namespace
{
    class FakeStateManager;

    class IdleTestState final : public NS::Game::Entity::EntityState<IdleTestState, FakeStateManager>
    {
    public:
        void OnStep(FakeStateManager&, float) override {}
    };

    class WalkTestState final : public NS::Game::Entity::EntityState<WalkTestState, FakeStateManager>
    {
    public:
        void OnStep(FakeStateManager&, float) override {}
    };

    //! 基底だけを見るための最小の派生。派生が状態機械を持つ形をそのまま写している
    class FakeStateManager final : public NS::Game::Entity::EntityStateManager
    {
    public:
        void Build() { m_machine.Build<IdleTestState, WalkTestState>(*this); }

        [[nodiscard]] bool IsBuilt() const noexcept override { return m_machine.IsBuilt(); }
        void ResetToFirst() noexcept override { m_machine.Reset(); }

    private:
        bool ChangeToState(NS::Obj::StateId id) override { return m_machine.Change(*this, id); }

        [[nodiscard]] NS::Obj::StateId CurrentStateId() const noexcept override { return m_machine.CurrentId(); }

        NS::Obj::StateMachine<FakeStateManager> m_machine;
    };
} // namespace

TEST(EntityStateManagerTest, ChangeAndIsCurrentTakeTheStateType)
{
    NS::Obj::GameObject obj;
    FakeStateManager& manager = *obj.AddComponent<FakeStateManager>();
    manager.Build();
    ASSERT_TRUE(manager.IsBuilt());

    EXPECT_TRUE(manager.IsCurrent<IdleTestState>());
    EXPECT_FALSE(manager.IsCurrent<WalkTestState>());

    ASSERT_TRUE(manager.Change<WalkTestState>());

    EXPECT_TRUE(manager.IsCurrent<WalkTestState>());
    EXPECT_FALSE(manager.IsCurrent<IdleTestState>());
}

// 組む前の IsCurrent が真を返すと、初期状態を仮定した分岐が組む前に通る
TEST(EntityStateManagerTest, NotBuiltManagerIsNoState)
{
    NS::Obj::GameObject obj;
    FakeStateManager& manager = *obj.AddComponent<FakeStateManager>();

    EXPECT_FALSE(manager.IsBuilt());
    EXPECT_FALSE(manager.IsCurrent<IdleTestState>());
    EXPECT_FALSE(manager.Change<WalkTestState>());
}
