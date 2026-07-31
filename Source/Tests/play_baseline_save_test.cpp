#include "Game/Level/BlockObject.h"

#include <gtest/gtest.h>
#include <Runtime/Math/Math.h>
#include <Runtime/Object/Components/TransformComponent.h>
#include <Runtime/Object/GameObject.h>
#include <Runtime/Object/Scene/Scene.h>
#include <Runtime/Object/Scene/SceneData.h>
#include <Runtime/Object/Scene/SceneJson.h>
#include <string>
#include <utility>

namespace SceneNs = NS::Object;
namespace LevelNs = NS::Game::Level;

// プレイ突入時に凍結した控えは、 プレイ中の live 移動を映さず突入時の姿を保つ
// プレイ中の保存はこの控えを書くため、 プレイの一時状態がレベルファイルへ焼き込まれない
TEST(PlayBaselineSave, FrozenBaselineIgnoresPlayMovement)
{
    NS::Object::Scene scene;

    SceneNs::SceneData level;
    SceneNs::ObjectData cube = LevelNs::MakeCellObject(0, 0, 0);
    SceneNs::SetObjectPosition(cube, NS::Math::Vector3{1.0f, 2.0f, 3.0f});
    level.objects.push_back(std::move(cube));
    SceneNs::EnsureUniqueObjectIds(level);
    scene.LoadFromData(std::move(level));

    // 編集突入時の姿を凍結する
    scene.BeginPlayBaseline();

    // プレイ中の変化相当。 live object を別位置へ動かす
    ASSERT_GT(scene.World().ObjectCount(), 0u);
    scene.World().ObjectAt(0)->Root().SetPosition(NS::Math::Vector3{50.0f, 60.0f, 70.0f});

    // 控えは突入時の位置を保ち、 動かした後の位置は映らない
    const SceneNs::SceneData& baseline = scene.PlayBaseline();
    ASSERT_EQ(baseline.objects.size(), 1u);
    const NS::Math::Vector3 frozen = SceneNs::ObjectPosition(baseline.objects[0]);
    EXPECT_FLOAT_EQ(frozen.x, 1.0f);
    EXPECT_FLOAT_EQ(frozen.y, 2.0f);
    EXPECT_FLOAT_EQ(frozen.z, 3.0f);

    // 保存経路の直列化も凍結の姿を書く
    const std::string json = SceneNs::SerializeSceneToJson(baseline);
    SceneNs::SceneData reloaded;
    ASSERT_TRUE(SceneNs::DeserializeSceneFromJson(reloaded, json));
    ASSERT_EQ(reloaded.objects.size(), 1u);
    const NS::Math::Vector3 saved = SceneNs::ObjectPosition(reloaded.objects[0]);
    EXPECT_FLOAT_EQ(saved.x, 1.0f);
    EXPECT_FLOAT_EQ(saved.y, 2.0f);
    EXPECT_FLOAT_EQ(saved.z, 3.0f);
}
