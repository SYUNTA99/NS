#include "Editor/EditorObjects.h"
#include "Game/Level/LaunchedBody.h"

#include <Runtime/Core/Math.h>
#include <Runtime/Object/Components/TransformComponent.h>
#include <Runtime/Object/GameObject.h>
#include <Runtime/Object/Reflection/ComponentEntry.h>
#include <Runtime/Object/Scene/Scene.h>
#include <Runtime/Object/Scene/SceneData.h>
#include <Runtime/Object/Scene/SceneJson.h>
#include <cmath>
#include <gtest/gtest.h>
#include <string>
#include <string_view>
#include <utility>

namespace SceneNs = NS::Obj;
namespace LevelNs = NS::Game::Level;

namespace
{
    void SetFloatField(NS::Obj::Component& comp, std::string_view name, float value)
    {
        const NS::Obj::ReflectionInfo* info = comp.GetReflection();
        for (std::size_t i = 0; i < info->fieldCount; ++i)
        {
            if (std::string_view{info->fields[i].name} == name)
            {
                info->fields[i].set(&comp, &value);
                return;
            }
        }
    }

    float GetFloatField(const NS::Obj::Component& comp, std::string_view name)
    {
        const NS::Obj::ReflectionInfo* info = comp.GetReflection();
        for (std::size_t i = 0; i < info->fieldCount; ++i)
        {
            if (std::string_view{info->fields[i].name} == name)
            {
                float value = 0.0f;
                info->fields[i].get(&comp, &value);
                return value;
            }
        }
        return 0.0f;
    }
} // namespace

// プレイ突入時に凍結した控えは、プレイ中の live 移動を映さず突入時の姿を保つ
// プレイ中の保存はこの控えを書くため、プレイの一時状態がレベルファイルへ書き込まれない
TEST(PlayBaselineSave, FrozenBaselineIgnoresPlayMovement)
{
    NS::Obj::Scene scene;

    SceneNs::SceneData level;
    SceneNs::ObjectData cube = NS::Editor::MakeCellObject(0, 0, 0);
    SceneNs::SetObjectPosition(cube, NS::Core::Vector3{1.0f, 2.0f, 3.0f});
    level.objects.push_back(std::move(cube));
    SceneNs::EnsureUniqueObjectIds(level);
    scene.LoadFromData(std::move(level));

    // プレイ突入時の姿を凍結する
    scene.BeginPlayBaseline();

    // プレイ中の変化相当。live object を別位置へ動かす
    ASSERT_GT(scene.Objects().ObjectCount(), 0u);
    scene.Objects().ObjectAt(0)->Root().SetPosition(NS::Core::Vector3{50.0f, 60.0f, 70.0f});

    // 控えは突入時の位置を保ち、動かした後の位置は映らない
    const SceneNs::SceneData& baseline = scene.PlayBaseline();
    ASSERT_EQ(baseline.objects.size(), 1u);
    const NS::Core::Vector3 frozen = SceneNs::ObjectPosition(baseline.objects[0]);
    EXPECT_FLOAT_EQ(frozen.x, 1.0f);
    EXPECT_FLOAT_EQ(frozen.y, 2.0f);
    EXPECT_FLOAT_EQ(frozen.z, 3.0f);

    // 保存経路の直列化も凍結の姿を書く
    const std::string json = SceneNs::SerializeSceneToJson(baseline);
    SceneNs::SceneData reloaded;
    ASSERT_TRUE(SceneNs::DeserializeSceneFromJson(reloaded, json));
    ASSERT_EQ(reloaded.objects.size(), 1u);
    const NS::Core::Vector3 saved = SceneNs::ObjectPosition(reloaded.objects[0]);
    EXPECT_FLOAT_EQ(saved.x, 1.0f);
    EXPECT_FLOAT_EQ(saved.y, 2.0f);
    EXPECT_FLOAT_EQ(saved.z, 3.0f);
}

TEST(PlayBaselineSave, HandEditedFieldWrittenToBaselineSurvivesReload)
{
    NS::Obj::Scene scene;

    SceneNs::SceneData level;
    SceneNs::ObjectData rock;
    SceneNs::SetObjectPosition(rock, NS::Core::Vector3{1.0f, 2.0f, 3.0f});
    rock.components.push_back(SceneNs::MakeComponentEntry("LaunchedBody"));
    level.objects.push_back(std::move(rock));
    SceneNs::EnsureUniqueObjectIds(level);
    const std::uint32_t rockId = level.objects[0].objectId;
    scene.LoadFromData(std::move(level));
    scene.BeginPlayBaseline();

    NS::Obj::GameObject* live = scene.Objects().FindObject(NS::Obj::ObjectRef{rockId});
    ASSERT_NE(live, nullptr);
    LevelNs::LaunchedBody* launched = live->FindComponent<LevelNs::LaunchedBody>();
    ASSERT_NE(launched, nullptr);

    live->Root().SetPosition(NS::Core::Vector3{50.0f, 60.0f, 70.0f});
    SetFloatField(*launched, "跳ね返り", 0.9f);
    scene.WritePlayBaselineField(*launched, "跳ね返り");

    SceneNs::SceneData copy = scene.PlayBaseline();
    ASSERT_EQ(copy.objects.size(), 1u);
    const NS::Core::Vector3 frozen = SceneNs::ObjectPosition(copy.objects[0]);
    EXPECT_FLOAT_EQ(frozen.x, 1.0f);
    EXPECT_FLOAT_EQ(frozen.y, 2.0f);
    EXPECT_FLOAT_EQ(frozen.z, 3.0f);

    scene.LoadFromData(std::move(copy));
    NS::Obj::GameObject* rebuilt = scene.Objects().FindObject(NS::Obj::ObjectRef{rockId});
    ASSERT_NE(rebuilt, nullptr);
    LevelNs::LaunchedBody* rebuiltLaunched = rebuilt->FindComponent<LevelNs::LaunchedBody>();
    ASSERT_NE(rebuiltLaunched, nullptr);
    EXPECT_FLOAT_EQ(GetFloatField(*rebuiltLaunched, "跳ね返り"), 0.9f);
}

// プレイ中に live の回転を直接動かした分が凍結側へ写り、編集へ戻った時に残る
TEST(PlayBaselineSave, HandEditedRotationUpdatesFrozenQuaternion)
{
    NS::Obj::Scene scene;

    SceneNs::SceneData level;
    level.objects.push_back(NS::Editor::MakeCellObject(0, 0, 0));
    SceneNs::EnsureUniqueObjectIds(level);
    const std::uint32_t blockId = level.objects[0].objectId;
    scene.LoadFromData(std::move(level));
    scene.BeginPlayBaseline();

    NS::Obj::GameObject* live = scene.Objects().FindObject(NS::Obj::ObjectRef{blockId});
    ASSERT_NE(live, nullptr);
    SceneNs::TransformComponent* transform = live->FindComponent<SceneNs::TransformComponent>();
    ASSERT_NE(transform, nullptr);

    const NS::Core::Quaternion edited =
        NS::Core::Quaternion::CreateFromYawPitchRoll(NS::Core::Vector3{NS::Core::DegreesToRadians(90.0f), 0.0f, 0.0f});
    live->Root().SetRotation(edited);
    scene.WritePlayBaselineField(*transform, SceneNs::k_RotationFieldName);

    SceneNs::SceneData copy = scene.PlayBaseline();
    scene.LoadFromData(std::move(copy));
    NS::Obj::GameObject* rebuilt = scene.Objects().FindObject(NS::Obj::ObjectRef{blockId});
    ASSERT_NE(rebuilt, nullptr);
    const NS::Core::Quaternion restored = rebuilt->Root().Rotation();
    EXPECT_NEAR(std::abs(restored.Dot(edited)), 1.0f, 1e-4f);
}
