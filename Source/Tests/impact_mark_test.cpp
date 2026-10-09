#include "Game/Level/ImpactMark.h"
#include "NSlib/Object/Scene/Scene.h"
#include "NSlib/Windows/Clock.h"

#include <gtest/gtest.h>

#include <type_traits>

static_assert(std::is_base_of_v<NS::Obj::Actor, GL::Level::ImpactMark>);
static_assert(!std::is_base_of_v<NS::Obj::SubObject, GL::Level::ImpactMark>);

TEST(ImpactMark, TemporaryActorShrinksAndStops)
{
    NS::Obj::Scene scene;
    NS::Obj::Actor* spawned = GL::Level::ImpactMark::SpawnAt(&scene, NS::Vector3{2.0f, 3.0f, 4.0f});
    ASSERT_NE(spawned, nullptr);
    EXPECT_TRUE(spawned->IsTransient());
    EXPECT_EQ(spawned->Root().Position(), (NS::Vector3{2.0f, 3.0f, 4.0f}));

    GL::Level::ImpactMark* mark = static_cast<GL::Level::ImpactMark*>(spawned);
    mark->SetLifeSeconds(NS::OS::FrameTimer::FixedDelta());
    mark->Update();
    EXPECT_FALSE(mark->IsActiveSelf());
    ASSERT_NE(mark->ModelSubObj(), nullptr);
    EXPECT_FALSE(mark->ModelSubObj()->IsActive());
}

TEST(ImpactMark, ExpiredMarkLeavesTheActorList)
{
    NS::Obj::Scene scene;
    const std::size_t before = scene.Objects().ObjectCount();
    NS::Obj::Actor* spawned = GL::Level::ImpactMark::SpawnAt(&scene, NS::Vector3{});
    ASSERT_NE(spawned, nullptr);
    static_cast<GL::Level::ImpactMark*>(spawned)->SetLifeSeconds(NS::OS::FrameTimer::FixedDelta());
    EXPECT_EQ(scene.Objects().ObjectCount(), before + 1);

    // 当てるたびに 1 体増えるので、消えた跡は更新の終わりに捨てる
    scene.OnUpdate();
    EXPECT_EQ(scene.Objects().ObjectCount(), before);
}
