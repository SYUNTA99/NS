#include "Editor/EditorObjects.h"
#include "Game/Player.h"
#include "Runtime/Object/Components/TransformComponent.h"
#include "Runtime/Object/Reflection/ComponentEntry.h"
#include "Runtime/Object/Scene/Scene.h"

#include <gtest/gtest.h>
#include <utility>

namespace SceneNs = NS::Obj;

namespace
{
    // 視覚 / 当たりを持たず接触クリアの意味だけを持つゴールを作る
    nlohmann::json MakeGoal()
    {
        nlohmann::json object = SceneNs::MakeObjectJson();
        SceneNs::ObjectJsonComponents(object).push_back(SceneNs::MakeComponentEntry("Goal"));
        return object;
    }
} // namespace

//! プレイ中は凍結スナップショットへ書き込まない。600 tick (10 秒 @60Hz) を回した後も突入直後の写しと
//! 等しいことで、プレイ進行の経路が凍結を変更しないことを実行時にも見る
//! 受け取りを const& にする型の側の縛りと合わせて二重に確かめる
TEST(PlayBaseline, TickDoesNotTouchPlayBaseline)
{
    SceneNs::Scene scene;
    nlohmann::json level = SceneNs::MakeSceneJson();
    nlohmann::json& objects = SceneNs::SceneJsonObjects(level);
    objects.push_back(MakePlayerObject(NS::Core::Vector3{5.0f, 1.0f, -3.0f}, NS::Core::Quaternion{}));
    objects.push_back(NS::Editor::MakeCellObject(0, 0, 0));
    nlohmann::json rotated = NS::Editor::MakeCellObject(1, 0, 0);
    SceneNs::SetObjectRotation(rotated,
                               NS::Core::Quaternion::CreateFromYawPitchRoll(NS::Core::k_Pi * 0.5f, 0.0f, 0.0f));
    objects.push_back(rotated);
    objects.push_back(MakeGoal());
    scene.LoadJson(std::move(level));

    (void)scene.BeginPlayBaseline();
    const nlohmann::json frozen = scene.PlayBaseline();
    for (int i = 0; i < 600; ++i)
    {
        scene.OnUpdate();
    }

    EXPECT_TRUE(scene.PlayBaseline() == frozen) << "プレイ進行が凍結スナップショットを変更";
}
