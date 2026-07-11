#include <gtest/gtest.h>

#include <Editor/PlayerTuning.h>
#include <Framework/Scene/AssetManager.h>
#include <Framework/Scene/Component.h>
#include <Framework/Scene/ComponentRegistry.h>
#include <Framework/Scene/Components/SphereColliderComponent.h>
#include <Framework/Scene/GameObject.h>
#include <Framework/Scene/ReflectionJson.h>
#include <Game/Blocks/BuildPlacedObject.h>
#include <Game/Level/LevelObjects.h>
#include <Game/Player.h>

#include <cstddef>
#include <filesystem>
#include <memory>
#include <string>
#include <variant>
#include <vector>

namespace LevelNs = NS::Game::Level;
namespace SceneNs = NS::Scene;

namespace
{
    // 既定構成は mesh / movement / input / shadow の 4 つ
    constexpr std::size_t kDefaultComponentCount = 4;

    SceneNs::ObjectData MakePlayer()
    {
        return LevelNs::MakePlayerObject(NS::Math::Vector3{}, NS::Math::Quaternion{});
    }

    // device を確立しない AssetManager でもファクトリは落ちない。 tuning 済データから live を組む窓口
    std::unique_ptr<NS::Scene::GameObject> BuildPlayer(const SceneNs::ObjectData& data)
    {
        NS::Scene::AssetManager assets{std::filesystem::path{"."}};
        const std::vector<std::string> materialPaths;
        return NS::Game::Blocks::BuildPlacedObject(data, assets, materialPaths);
    }

    const SceneNs::FieldValue* FindPlayerField(const SceneNs::ObjectData& player,
                                               const char* typeName,
                                               const char* fieldName)
    {
        const SceneNs::ComponentData* component = SceneNs::FindComponentData(player, typeName);
        if (component == nullptr)
            return nullptr;
        return SceneNs::FindField(*component, fieldName);
    }
} // namespace

TEST(PlayerTuningTest, MergeOverwritesExistingComponentData)
{
    SceneNs::ObjectData player = MakePlayer();
    MergePlayerTuningText(player,
                          R"({"components":[{"type":"CharacterMovementComponent","fields":{"Max Speed":11.0}}]})");

    // 既存の同型へ値だけ写し、構成は増えない
    EXPECT_EQ(player.components.size(), kDefaultComponentCount);
    const SceneNs::FieldValue* maxSpeed = FindPlayerField(player, "CharacterMovementComponent", "Max Speed");
    ASSERT_NE(maxSpeed, nullptr);
    ASSERT_TRUE(std::holds_alternative<float>(maxSpeed->value));
    EXPECT_FLOAT_EQ(std::get<float>(maxSpeed->value), 11.0f);
}

TEST(PlayerTuningTest, MergeAddsMissingComponentData)
{
    SceneNs::ObjectData player = MakePlayer();
    MergePlayerTuningText(player, R"({"components":[{"type":"SphereColliderComponent","fields":{"Radius":2.5}}]})");

    // 無い型は構成ごと追加される
    EXPECT_EQ(player.components.size(), kDefaultComponentCount + 1);
    const SceneNs::FieldValue* radius = FindPlayerField(player, "SphereColliderComponent", "Radius");
    ASSERT_NE(radius, nullptr);
    EXPECT_FLOAT_EQ(std::get<float>(radius->value), 2.5f);
}

TEST(PlayerTuningTest, MergeBrokenJsonKeepsDefaults)
{
    SceneNs::ObjectData player = MakePlayer();
    MergePlayerTuningText(player, "{broken");
    EXPECT_EQ(player.components.size(), kDefaultComponentCount);
}

TEST(PlayerTuningTest, TunedValuesReachBuiltPlayer)
{
    SceneNs::ObjectData data = MakePlayer();
    MergePlayerTuningText(data,
                          R"({"components":[{"type":"CharacterMovementComponent","fields":{"Max Speed":11.0}}]})");

    auto obj = BuildPlayer(data);
    ASSERT_NE(obj, nullptr);
    // プレイヤーのデータは Player の器で組まれる約束。実行時型情報は切っているため約束を前提に読む
    auto* player = static_cast<Player*>(obj.get());

    // 既存の同型へ値だけ写り、構成は増えない
    EXPECT_EQ(player->Components().size(), kDefaultComponentCount);
    EXPECT_FLOAT_EQ(player->Movement().MaxSpeed(), 11.0f);
}

TEST(PlayerTuningTest, TunedExtraComponentReachesBuiltPlayer)
{
    SceneNs::ObjectData data = MakePlayer();
    MergePlayerTuningText(data, R"({"components":[{"type":"SphereColliderComponent","fields":{"Radius":2.5}}]})");

    auto obj = BuildPlayer(data);
    ASSERT_NE(obj, nullptr);

    // 無い型は登録 factory で生成され、値も適用される
    EXPECT_EQ(obj->Components().size(), kDefaultComponentCount + 1);
    auto* sphere = obj->FindComponent<NS::Scene::SphereColliderComponent>();
    ASSERT_NE(sphere, nullptr);
    EXPECT_FLOAT_EQ(sphere->Radius(), 2.5f);
}

TEST(PlayerTuningTest, UnregisteredTypesSkippedOnBuild)
{
    SceneNs::ObjectData data = MakePlayer();
    // editor 専用型と未知型は factory が弾くので構成へ入らない
    data.components.push_back(SceneNs::ComponentData{"EditorCameraComponent", {}});
    data.components.push_back(SceneNs::ComponentData{"Bogus", {}});

    auto obj = BuildPlayer(data);
    ASSERT_NE(obj, nullptr);
    EXPECT_EQ(obj->Components().size(), kDefaultComponentCount);
}

TEST(PlayerTuningTest, SerializedComponentsRoundTripIntoBuiltPlayer)
{
    // 保存側 SerializeComponent とテンプレート取込 + live 組み立ての噛み合わせを、ファイルを介さず往復で確かめる
    Player source{};
    NS::Scene::Component* sphere = NS::Scene::CreateComponent("SphereColliderComponent", source);
    ASSERT_NE(sphere, nullptr);
    static_cast<NS::Scene::SphereColliderComponent*>(sphere)->SetRadius(3.5f);

    nlohmann::json components = nlohmann::json::array();
    for (const NS::Scene::Component* comp : source.Components())
        components.push_back(NS::Scene::SerializeComponent(*comp));
    nlohmann::json json;
    json["components"] = std::move(components);

    SceneNs::ObjectData data = MakePlayer();
    MergePlayerTuningText(data, json.dump());

    auto restored = BuildPlayer(data);
    ASSERT_NE(restored, nullptr);

    // 追加した球 collider が構成ごと復元される
    EXPECT_EQ(restored->Components().size(), kDefaultComponentCount + 1);
    auto* restoredSphere = restored->FindComponent<NS::Scene::SphereColliderComponent>();
    ASSERT_NE(restoredSphere, nullptr);
    EXPECT_FLOAT_EQ(restoredSphere->Radius(), 3.5f);
}
