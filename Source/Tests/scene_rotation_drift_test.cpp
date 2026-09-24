#include "Editor/EditorObjects.h"

#include <Runtime/Core/Math.h>
#include <Runtime/Object/AssetManager.h>
#include <Runtime/Object/Components/TransformComponent.h>
#include <Runtime/Object/GameObject.h>
#include <Runtime/Object/Reflection/ComponentEntry.h>
#include <Runtime/Object/Reflection/ObjectBuilder.h>
#include <Runtime/Object/Scene/SceneData.h>
#include <Runtime/Object/Scene/SceneJson.h>
#include <cmath>
#include <gtest/gtest.h>
#include <memory>
#include <string>

namespace SceneNs = NS::Obj;
namespace LevelNs = NS::Game::Level;

namespace
{
    SceneNs::ObjectData MakeRotatedCube(const NS::Core::Vector3& eulerDegrees)
    {
        SceneNs::ObjectData object = NS::Editor::MakeCellObject(0, 0, 0);
        SceneNs::SetObjectRotation(object, NS::Core::EulerDegreesToQuaternion(eulerDegrees));
        return object;
    }
} // namespace

// 編集中の capture→rebuild 往復を重ねても root 回転が初回 rebuild 後の値と bit 一致する
// pitch を特異点付近に置き、Euler を経由する旧経路なら誤差が積もる条件で確かめる
TEST(SceneRotationDrift, RepeatedCaptureRebuildKeepsExactQuaternion)
{
    // device 無しの AssetManager でも組み立ては落ちない。mesh も material も解決できず空のまま
    NS::Obj::AssetManager assets{std::string{"."}};

    const SceneNs::ObjectData object = MakeRotatedCube(NS::Core::Vector3{89.9f, 40.0f, 20.0f});
    std::unique_ptr<NS::Obj::GameObject> live = SceneNs::BuildSceneObject(object, &assets);
    ASSERT_NE(live, nullptr);

    const NS::Core::Quaternion reference = live->Root().Rotation();

    for (int i = 0; i < 20; ++i)
    {
        const SceneNs::ObjectData captured = SceneNs::CaptureObjectData(*live);
        live = SceneNs::BuildSceneObject(captured, &assets);
        ASSERT_NE(live, nullptr);
    }

    const NS::Core::Quaternion after = live->Root().Rotation();
    EXPECT_EQ(after.x, reference.x);
    EXPECT_EQ(after.y, reference.y);
    EXPECT_EQ(after.z, reference.z);
    EXPECT_EQ(after.w, reference.w);
}

// 手書きファイルの 4 要素の回転をそのまま読む
TEST(SceneRotationDrift, RotationFieldLoadsToExpectedQuaternion)
{
    const std::string json = R"lvl({
        "version": 4,
        "nextObjectId": 2,
        "objects": [
            {
                "id": 1,
                "components": [
                    { "type": "TransformComponent", "fields": {
                        "位置": [0.0, 0.0, 0.0],
                        "回転": [0.0, 0.70710677, 0.0, 0.70710677],
                        "スケール": [1.0, 1.0, 1.0]
                    } }
                ]
            }
        ]
    })lvl";

    SceneNs::SceneData dst;
    ASSERT_TRUE(SceneNs::DeserializeSceneFromJson(dst, json));
    ASSERT_EQ(dst.objects.size(), 1u);

    // yaw 90 度は Y 軸まわりの回転で quaternion (0, sin45, 0, cos45)
    const NS::Core::Quaternion q = SceneNs::ObjectRotation(dst.objects[0]);
    EXPECT_NEAR(q.x, 0.0f, 1e-5f);
    EXPECT_NEAR(q.y, 0.70710677f, 1e-5f);
    EXPECT_NEAR(q.z, 0.0f, 1e-5f);
    EXPECT_NEAR(q.w, 0.70710677f, 1e-5f);
}

// 捕捉から保存・再読込までの間に、度の欄が生き残っていないことまで見る
TEST(SceneRotationDrift, SaveWritesRotationAsQuaternionOnly)
{
    // 1 つ目の試しと同じく device 無し
    NS::Obj::AssetManager assets{std::string{"."}};

    const SceneNs::ObjectData object = MakeRotatedCube(NS::Core::Vector3{30.0f, 45.0f, 60.0f});
    std::unique_ptr<NS::Obj::GameObject> live = SceneNs::BuildSceneObject(object, &assets);
    ASSERT_NE(live, nullptr);

    SceneNs::SceneData data;
    data.objects.push_back(SceneNs::CaptureObjectData(*live));
    data.objects[0].objectId = 1;

    const nlohmann::json* transform = SceneNs::FindComponentEntry(data.objects[0], "TransformComponent");
    ASSERT_NE(transform, nullptr);
    EXPECT_TRUE(SceneNs::HasField(*transform, "回転"));
    EXPECT_FALSE(SceneNs::HasField(*transform, "回転 (度)"));

    // 現行 version の形式検査を通って読み戻せる
    const std::string json = SceneNs::SerializeSceneToJson(data);
    SceneNs::SceneData reloaded;
    ASSERT_TRUE(SceneNs::DeserializeSceneFromJson(reloaded, json));
    ASSERT_EQ(reloaded.objects.size(), 1u);

    // BoxCollider も「回転 (度)」を持つので、保存文字列全体でなく transform の項目だけを見る
    const nlohmann::json* reloadedTransform = SceneNs::FindComponentEntry(reloaded.objects[0], "TransformComponent");
    ASSERT_NE(reloadedTransform, nullptr);
    EXPECT_TRUE(SceneNs::HasField(*reloadedTransform, "回転"));
    EXPECT_FALSE(SceneNs::HasField(*reloadedTransform, "回転 (度)"));

    // 読込が 4 要素を受けないと無回転のまま素通りするので、向きまで突き合わせる
    const NS::Core::Quaternion restored = SceneNs::ObjectRotation(reloaded.objects[0]);
    EXPECT_NEAR(std::abs(restored.Dot(live->Root().Rotation())), 1.0f, 1e-5f);
}
