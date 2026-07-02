#include <gtest/gtest.h>

#include <Framework/Scene/Component.h>
#include <Framework/Scene/ComponentRegistry.h>
#include <Framework/Scene/Components/SphereColliderComponent.h>
#include <Framework/Scene/GameObject.h>
#include <Framework/Scene/ReflectionJson.h>
#include <Game/Player.h>
#include <Game/PlayerTuning.h>

#include <cstddef>

namespace
{
    // 既定構成は mesh / movement / input / shadow の 4 つ
    constexpr std::size_t kDefaultComponentCount = 4;
} // namespace

TEST(PlayerTuningTest, AppliesValuesToExistingComponent)
{
    Player player(nullptr, nullptr);
    ApplyPlayerTuningText(player,
                          R"({"components":[{"type":"CharacterMovementComponent","fields":{"Max Speed":11.0}}]})");
    // 既存の同型へ値だけ適用し、構成は増えない
    EXPECT_EQ(player.Components().size(), kDefaultComponentCount);
    EXPECT_FLOAT_EQ(player.Movement().MaxSpeed(), 11.0f);
}

TEST(PlayerTuningTest, CreatesMissingComponentFromData)
{
    Player player(nullptr, nullptr);
    ApplyPlayerTuningText(player, R"({"components":[{"type":"SphereColliderComponent","fields":{"Radius":2.5}}]})");
    // 無い型は登録 factory で生成され、値も適用される
    EXPECT_EQ(player.Components().size(), kDefaultComponentCount + 1);
    auto* sphere = player.FindComponent<NS::Scene::SphereColliderComponent>();
    ASSERT_NE(sphere, nullptr);
    EXPECT_FLOAT_EQ(sphere->Radius(), 2.5f);
}

TEST(PlayerTuningTest, SkipsUnregisteredTypes)
{
    Player player(nullptr, nullptr);
    // editor 専用型と未知型は factory が弾くので構成へ入らない
    ApplyPlayerTuningText(
        player, R"({"components":[{"type":"EditorCameraComponent","fields":{}},{"type":"Bogus","fields":{}}]})");
    EXPECT_EQ(player.Components().size(), kDefaultComponentCount);
}

TEST(PlayerTuningTest, BrokenJsonKeepsDefaults)
{
    Player player(nullptr, nullptr);
    ApplyPlayerTuningText(player, "{broken");
    EXPECT_EQ(player.Components().size(), kDefaultComponentCount);
    EXPECT_FLOAT_EQ(player.Movement().MaxSpeed(), 8.0f);
}

TEST(PlayerTuningTest, SerializedComponentsRoundTripIntoFreshPlayer)
{
    // 保存側 SerializeComponent と読込側の噛み合わせを、ファイルを介さず往復で確かめる
    Player source(nullptr, nullptr);
    NS::Scene::Component* sphere = NS::Scene::CreateComponent("SphereColliderComponent", source);
    ASSERT_NE(sphere, nullptr);
    static_cast<NS::Scene::SphereColliderComponent*>(sphere)->SetRadius(3.5f);

    nlohmann::json components = nlohmann::json::array();
    for (const NS::Scene::Component* comp : source.Components())
        components.push_back(NS::Scene::SerializeComponent(*comp));
    nlohmann::json json;
    json["components"] = std::move(components);

    Player restored(nullptr, nullptr);
    ApplyPlayerTuningText(restored, json.dump());

    // 追加した球 collider が構成ごと復元される
    EXPECT_EQ(restored.Components().size(), kDefaultComponentCount + 1);
    auto* restoredSphere = restored.FindComponent<NS::Scene::SphereColliderComponent>();
    ASSERT_NE(restoredSphere, nullptr);
    EXPECT_FLOAT_EQ(restoredSphere->Radius(), 3.5f);
}
