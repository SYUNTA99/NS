#include "Editor/EditorActors.h"
#include "Game/Level/ImpactMarks.h"
#include "Game/Level/LevelMessages.h"
#include "Game/Level/PropActor.h"
#include "NSlib/Core/OBB.h"
#include "NSlib/CoreObject/Scene/Scene.h"
#include "NSlib/CoreObject/Scene/SceneJson.h"

#include <gtest/gtest.h>

#include <vector>

namespace
{
    // 床の上 (上面 y=0) に置いた置物。球の中心は y=0.5
    GL::Level::PropActor* PlaceRockOnFloor(NS::Scene& scene)
    {
        nlohmann::json doc = NS::MakeSceneJson();
        nlohmann::json rock = NS::MakeActorJson();
        NS::SetActorJsonFile(rock, "PropActor");
        NS::SetActorJsonId(rock, 1);
        NS::Editor::SetActorJsonPosition(rock, NS::Vector3{0.0f, 0.5f, 0.0f});
        NS::SceneJsonActors(doc).push_back(std::move(rock));
        scene.ResetFromJson(doc);
        NS::OBB floor;
        floor.center = NS::Vector3{0.0f, -0.5f, 0.0f};
        floor.halfExtentX = 100.0f;
        floor.halfExtentY = 0.5f;
        floor.halfExtentZ = 100.0f;
        scene.Physics().AddBox(floor, NS::Phys::ObjectLayers::Terrain);
        return static_cast<GL::Level::PropActor*>(scene.Actors().FindByActorId(1));
    }
} // namespace

// 跡はアクターでなく置物の部品が持つ。シーンの一覧は増えない
TEST(ImpactMarks, ReleasedPropLeavesAMarkOnTheFloorBelowIt)
{
    NS::Scene scene;
    GL::Level::PropActor* rock = PlaceRockOnFloor(scene);
    ASSERT_NE(rock, nullptr);
    const std::size_t actors = scene.Actors().ActorCount();
    ASSERT_EQ(rock->Marks().LiveCount(), 0u);

    GL::Level::TackleReleaseDesc release;
    release.arc.distance = 5.0f;
    release.arc.apexHeight = 1.0f;
    ASSERT_TRUE(GL::Level::SendMsgTackleRelease(*rock, release));

    EXPECT_EQ(scene.Actors().ActorCount(), actors);
    ASSERT_EQ(rock->Marks().LiveCount(), 1u);
    // 跡は補間しないので、出たフレームの絵も出した位置にある (原点から滑って来ない)
    const std::vector<NS::Matrix> drawn = rock->Marks().MarkWorldMatrices();
    ASSERT_EQ(drawn.size(), 1u);
    const NS::Vector3 position = drawn[0].Translation();
    EXPECT_NEAR(position.x, 0.0f, 0.001f);
    EXPECT_NEAR(position.y, 0.02f, 0.001f);
    EXPECT_NEAR(position.z, 0.0f, 0.001f);
}

TEST(ImpactMarks, MarkShrinksAndLeavesAfterItsLife)
{
    NS::Scene scene;
    GL::Level::PropActor* rock = PlaceRockOnFloor(scene);
    ASSERT_NE(rock, nullptr);
    GL::Level::ImpactMarks& marks = rock->Marks();
    marks.Spawn(NS::Vector3{1.0f, 0.0f, 2.0f});
    const float fullWidth = marks.MarkWorldMatrices()[0].Right().Length();
    ASSERT_GT(fullWidth, 0.0f);

    marks.Step(marks.LifeSeconds() * 0.5f);
    ASSERT_EQ(marks.LiveCount(), 1u);
    EXPECT_NEAR(marks.MarkWorldMatrices()[0].Right().Length(), fullWidth * 0.5f, 0.001f);

    marks.Step(marks.LifeSeconds());
    EXPECT_EQ(marks.LiveCount(), 0u);
    EXPECT_TRUE(marks.MarkWorldMatrices().empty());
}

// 数は決まっている。埋まった後に出すと一番古い跡を置き換える
TEST(ImpactMarks, FullMarksReplaceTheOldest)
{
    NS::Scene scene;
    GL::Level::PropActor* rock = PlaceRockOnFloor(scene);
    ASSERT_NE(rock, nullptr);
    GL::Level::ImpactMarks& marks = rock->Marks();
    const int capacity = marks.Capacity();
    ASSERT_GT(capacity, 0);
    for (int i = 0; i <= capacity; ++i)
    {
        marks.Spawn(NS::Vector3{static_cast<float>(i), 0.0f, 0.0f});
    }
    EXPECT_EQ(marks.LiveCount(), static_cast<std::size_t>(capacity));
    for (const NS::Matrix& world : marks.MarkWorldMatrices())
    {
        EXPECT_NE(world.Translation().x, 0.0f);
    }
}
