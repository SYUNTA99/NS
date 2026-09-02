#include "Game/Level/BlockObject.h"
#include "Game/Player.h"
#include "Runtime/Object/Components/TransformComponent.h"
#include "Runtime/Object/Reflection/ComponentEntry.h"
#include "Runtime/Object/Scene/Scene.h"

#include <cstdint>
#include <gtest/gtest.h>
#include <utility>

namespace LevelNs = NS::Game::Level;
namespace SceneNs = NS::Object;

namespace
{
    // 視覚 / 当たりを持たず接触クリアの意味だけを持つゴールを作る
    SceneNs::ObjectData MakeGoal()
    {
        SceneNs::ObjectData object{};
        object.components.push_back(SceneNs::MakeComponentEntry("GoalComponent"));
        return object;
    }
} // namespace

//! プレイ中は凍結スナップショットへ書き込まない。 600 tick (10 秒 @60Hz) を回した後の
//! CRC32 が突入直後と一致することで、 プレイ進行の経路が凍結を変更しないことを実行時にも見る
//! 受け取りを const& にする型の側の縛りと合わせて二重に確かめる
TEST(PlayBaselineCrc, TickDoesNotTouchPlayBaseline)
{
    SceneNs::Scene scene;
    SceneNs::SceneData level;
    level.objects.push_back(MakePlayerObject(NS::Core::Vector3{5.0f, 1.0f, -3.0f}, NS::Core::Quaternion{}));
    level.objects.push_back(LevelNs::MakeCellObject(0, 0, 0));
    SceneNs::ObjectData rotated = LevelNs::MakeCellObject(1, 0, 0);
    SceneNs::SetObjectRotation(rotated,
                               NS::Core::Quaternion::CreateFromYawPitchRoll(NS::Core::k_Pi * 0.5f, 0.0f, 0.0f));
    level.objects.push_back(rotated);
    level.objects.push_back(MakeGoal());
    scene.LoadFromData(std::move(level));

    (void)scene.BeginPlayBaseline();
    const std::uint32_t frozen = scene.PlayBaseline().ComputeCrc32();
    for (int i = 0; i < 600; ++i)
        scene.OnUpdate();

    EXPECT_EQ(scene.PlayBaseline().ComputeCrc32(), frozen) << "プレイ進行が凍結スナップショットを変更";
}
