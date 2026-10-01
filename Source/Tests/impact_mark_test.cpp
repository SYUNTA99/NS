#include "Game/Level/ImpactMark.h"
#include "Runtime/Object/Scene/Scene.h"
#include "Runtime/Platform/Clock.h"

#include <gtest/gtest.h>

#include <type_traits>

static_assert(std::is_base_of_v<NS::Obj::Actor, NS::Game::Level::ImpactMark>);
static_assert(!std::is_base_of_v<NS::Obj::Component, NS::Game::Level::ImpactMark>);

TEST(ImpactMark, TemporaryActorShrinksAndStops)
{
    NS::Obj::Scene scene;
    NS::Obj::Actor* spawned = NS::Game::Level::ImpactMark::SpawnAt(&scene, NS::Core::Vector3{2.0f, 3.0f, 4.0f});
    ASSERT_NE(spawned, nullptr);
    EXPECT_TRUE(spawned->IsTransient());
    EXPECT_EQ(spawned->Root().Position(), (NS::Core::Vector3{2.0f, 3.0f, 4.0f}));

    NS::Game::Level::ImpactMark* mark = static_cast<NS::Game::Level::ImpactMark*>(spawned);
    mark->SetLifeSeconds(NS::Platform::FrameTimer::FixedDelta());
    mark->Update();
    EXPECT_FALSE(mark->IsActiveSelf());
    ASSERT_NE(mark->ModelPart(), nullptr);
    EXPECT_FALSE(mark->ModelPart()->IsActive());
}

TEST(ImpactMark, ExpiredMarkLeavesTheObjectList)
{
    NS::Obj::Scene scene;
    const std::size_t before = scene.Objects().ObjectCount();
    NS::Obj::Actor* spawned = NS::Game::Level::ImpactMark::SpawnAt(&scene, NS::Core::Vector3{});
    ASSERT_NE(spawned, nullptr);
    static_cast<NS::Game::Level::ImpactMark*>(spawned)->SetLifeSeconds(NS::Platform::FrameTimer::FixedDelta());
    EXPECT_EQ(scene.Objects().ObjectCount(), before + 1);

    // 当てるたびに 1 体増えるので、消えた跡は更新の終わりに捨てる
    scene.OnUpdate();
    EXPECT_EQ(scene.Objects().ObjectCount(), before);
}
