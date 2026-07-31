#include "Game/Level/BlockObject.h"

#include <filesystem>
#include <gtest/gtest.h>
#include <memory>
#include <Runtime/Math/Math.h>
#include <Runtime/Object/AssetManager.h>
#include <Runtime/Object/Components/TransformComponent.h>
#include <Runtime/Object/GameObject.h>
#include <Runtime/Object/Reflection/ComponentEntry.h>
#include <Runtime/Object/Reflection/ObjectBuilder.h>
#include <Runtime/Object/Scene/SceneData.h>
#include <Runtime/Object/Scene/SceneJson.h>
#include <string>

namespace SceneNs = NS::Object;
namespace LevelNs = NS::Game::Level;

namespace
{
    SceneNs::ObjectData MakeRotatedCube(const NS::Math::Vector3& eulerDegrees)
    {
        SceneNs::ObjectData object = LevelNs::MakeCellObject(0, 0, 0);
        nlohmann::json& transform = SceneNs::EnsureTransformComponent(object);
        SceneNs::SetField(transform, "Rotation (deg)", eulerDegrees);
        return object;
    }
} // namespace

// 編集中の capture→rebuild 往復を重ねても root 回転が初回 rebuild 後の値と bit 一致する (ドリフトゼロ)
// pitch を singularity 付近に置き、 Euler を経由する旧経路なら誤差が積もる条件で確かめる
TEST(SceneRotationDrift, RepeatedCaptureRebuildKeepsExactQuaternion)
{
    // device 無しでも汎用構築は落ちない。 参照は文字列のまま持つ
    NS::Object::AssetManager assets{std::filesystem::path{"."}};

    const SceneNs::ObjectData object = MakeRotatedCube(NS::Math::Vector3{89.9f, 40.0f, 20.0f});
    std::unique_ptr<NS::Object::GameObject> live = SceneNs::BuildSceneObject(object, &assets);
    ASSERT_NE(live, nullptr);

    const NS::Math::Quaternion reference = live->Root().Rotation();

    for (int i = 0; i < 20; ++i)
    {
        const SceneNs::ObjectData captured = SceneNs::CaptureObjectData(*live);
        live = SceneNs::BuildSceneObject(captured, &assets);
        ASSERT_NE(live, nullptr);
    }

    const NS::Math::Quaternion after = live->Root().Rotation();
    EXPECT_EQ(after.x, reference.x);
    EXPECT_EQ(after.y, reference.y);
    EXPECT_EQ(after.z, reference.z);
    EXPECT_EQ(after.w, reference.w);
}

// version 2 の手書きファイル (Euler の "Rotation (deg)" のみ・ quat 控え欄なし) が従来どおり読める
TEST(SceneRotationDrift, LegacyEulerFileLoadsToExpectedQuaternion)
{
    // JSON のキーに ) と " が隣接するため、 生文字列は衝突しない独自区切りで囲う
    const std::string json = R"lvl({
        "version": 2,
        "nextObjectId": 2,
        "objects": [
            {
                "id": 1,
                "components": [
                    { "type": "TransformComponent", "fields": {
                        "Position": [0.0, 0.0, 0.0],
                        "Rotation (deg)": [0.0, 90.0, 0.0],
                        "Scale": [1.0, 1.0, 1.0]
                    } }
                ]
            }
        ]
    })lvl";

    SceneNs::SceneData dst;
    ASSERT_TRUE(SceneNs::DeserializeSceneFromJson(dst, json));
    ASSERT_EQ(dst.objects.size(), 1u);

    // yaw 90 度は Y 軸まわりの回転で quaternion (0, sin45, 0, cos45)
    const NS::Math::Quaternion q = SceneNs::ObjectRotation(dst.objects[0]);
    EXPECT_NEAR(q.x, 0.0f, 1e-5f);
    EXPECT_NEAR(q.y, 0.70710677f, 1e-5f);
    EXPECT_NEAR(q.z, 0.0f, 1e-5f);
    EXPECT_NEAR(q.w, 0.70710677f, 1e-5f);
}

// faithful 捕捉はメモリ上に quat 控えを乗せるが、 保存文字列には Euler だけが載り version は 2 のまま
TEST(SceneRotationDrift, SaveDropsQuatFieldKeepsEuler)
{
    NS::Object::AssetManager assets{std::filesystem::path{"."}};

    const SceneNs::ObjectData object = MakeRotatedCube(NS::Math::Vector3{30.0f, 45.0f, 60.0f});
    std::unique_ptr<NS::Object::GameObject> live = SceneNs::BuildSceneObject(object, &assets);
    ASSERT_NE(live, nullptr);

    SceneNs::SceneData data;
    data.objects.push_back(SceneNs::CaptureObjectData(*live));
    data.objects[0].objectId = 1;

    // メモリ上の控えには exact quaternion が乗っている
    const nlohmann::json* transform = SceneNs::FindComponentEntry(data.objects[0], "TransformComponent");
    ASSERT_NE(transform, nullptr);
    EXPECT_TRUE(SceneNs::HasField(*transform, "Rotation (quat)"));

    // 保存文字列は Euler だけ・ quat 控えは落ちる
    const std::string json = SceneNs::SerializeSceneToJson(data);
    EXPECT_EQ(json.find("Rotation (quat)"), std::string::npos);
    EXPECT_NE(json.find("Rotation (deg)"), std::string::npos);

    // version 2 の門を通って読み戻せる
    SceneNs::SceneData reloaded;
    EXPECT_TRUE(SceneNs::DeserializeSceneFromJson(reloaded, json));
}
