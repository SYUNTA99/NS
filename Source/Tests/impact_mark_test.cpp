#include "Game/Level/ImpactMarkActor.h"
#include "NSlib/CoreObject/Scene/Scene.h"
#include "NSlib/Windows/Clock.h"

#include <gtest/gtest.h>

#include <type_traits>

static_assert(std::is_base_of_v<NS::SceneActor, GL::Level::ImpactMarkActor>);
static_assert(!std::is_base_of_v<NS::SubObject, GL::Level::ImpactMarkActor>);

TEST(ImpactMarkActor, TemporaryActorShrinksAndStops)
{
    NS::Scene scene;
    NS::SceneActor* spawned = GL::Level::ImpactMarkActor::SpawnAt(&scene, NS::Vector3{2.0f, 3.0f, 4.0f});
    ASSERT_NE(spawned, nullptr);
    EXPECT_TRUE(spawned->IsTransient());
    EXPECT_EQ(spawned->Root().Position(), (NS::Vector3{2.0f, 3.0f, 4.0f}));

    GL::Level::ImpactMarkActor* mark = static_cast<GL::Level::ImpactMarkActor*>(spawned);
    mark->SetLifeSeconds(NS::OS::FrameTimer::FixedDelta());
    mark->Update();
    EXPECT_FALSE(mark->IsAppeared());
    ASSERT_NE(mark->ModelSubObj(), nullptr);
    EXPECT_FALSE(mark->ModelSubObj()->IsActive());
}

TEST(ImpactMarkActor, ExpiredMarkLeavesTheActorList)
{
    NS::Scene scene;
    const std::size_t before = scene.Objects().ObjectCount();
    NS::SceneActor* spawned = GL::Level::ImpactMarkActor::SpawnAt(&scene, NS::Vector3{});
    ASSERT_NE(spawned, nullptr);
    static_cast<GL::Level::ImpactMarkActor*>(spawned)->SetLifeSeconds(NS::OS::FrameTimer::FixedDelta());
    EXPECT_EQ(scene.Objects().ObjectCount(), before + 1);

    // 当てるたびに 1 体増えるので、消えた跡は更新の終わりに捨てる
    scene.OnUpdate();
    EXPECT_EQ(scene.Objects().ObjectCount(), before);
}
