#include "Editor/LevelFilePaths.h"
#include "Game/Player/PlayerComponent.h"
#include "Game/Player/PlayerStateManagerComponent.h"
#include "Game/Player/States/IdlePlayerState.h"
#include "Runtime/Core/Math.h"
#include "Runtime/Object/AssetManager.h"
#include "Runtime/Object/Component.h"
#include "Runtime/Object/Components/MeshRendererComponent.h"
#include "Runtime/Object/Components/TransformComponent.h"
#include "Runtime/Object/GameObject.h"
#include "Runtime/Object/Reflection/ComponentEntry.h"
#include "Runtime/Object/Reflection/ObjectBuilder.h"
#include "Runtime/Object/Reflection/Reflection.h"
#include "Runtime/Object/Scene/SceneData.h"
#include "Runtime/Object/Scene/SceneJson.h"
#include "tuning_field_access.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <iterator>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace SceneNs = NS::Object;
namespace EditorNs = NS::Editor;
namespace PlayerNs = NS::Game::Player;

namespace
{
    bool LoadShippedScene(SceneNs::SceneData& outScene, std::string_view name)
    {
        const auto path = EditorNs::BuildLevelPath(name);
        if (!path.has_value())
            return false;
        return SceneNs::LoadSceneFromJsonFile(outScene, *path);
    }

    const SceneNs::ObjectData* FindPlayerObject(const SceneNs::SceneData& scene)
    {
        for (const SceneNs::ObjectData& object : scene.objects)
        {
            if (object.className == "Player")
                return &object;
        }
        return nullptr;
    }

    std::vector<std::string> ReflectedFieldNames(const SceneNs::Component& comp)
    {
        std::vector<std::string> names;
        const SceneNs::ReflectionInfo* info = comp.GetReflection();
        if (info == nullptr)
            return names;
        for (std::size_t i = 0; i < info->fieldCount; ++i)
            names.emplace_back(info->fields[i].name);
        return names;
    }

    class ShippedScene : public ::testing::TestWithParam<const char*>
    {};

    // 見た目は子の配置物へ分けてあるので、親の id から引き当てる
    const SceneNs::ObjectData* FindSkinObject(const SceneNs::SceneData& scene, std::uint32_t playerId)
    {
        for (const SceneNs::ObjectData& object : scene.objects)
        {
            if (object.parentId != playerId)
            {
                continue;
            }
            if (SceneNs::FindComponentEntry(object, "SkeletalAnimationComponent") != nullptr)
            {
                return &object;
            }
        }
        return nullptr;
    }

    float ReadEntryFloat(const nlohmann::json& entry, const char* name)
    {
        const nlohmann::json* fields = SceneNs::ComponentEntryFields(entry);
        if (fields == nullptr)
        {
            return 0.0f;
        }
        return fields->value(name, 0.0f);
    }

    float ReadEntryVec3Y(const nlohmann::json& entry, const char* name)
    {
        const nlohmann::json* fields = SceneNs::ComponentEntryFields(entry);
        if (fields == nullptr)
        {
            return 0.0f;
        }
        const auto found = fields->find(name);
        if (found == fields->end() || !found->is_array() || found->size() < 3u)
        {
            return 0.0f;
        }
        return (*found)[1].get<float>();
    }
} // namespace

TEST_P(ShippedScene, PlayerCarriesTheTwoNewComponents)
{
    SceneNs::SceneData scene;
    ASSERT_TRUE(LoadShippedScene(scene, GetParam()));
    const SceneNs::ObjectData* player = FindPlayerObject(scene);
    ASSERT_NE(player, nullptr);

    EXPECT_NE(SceneNs::FindComponentEntry(*player, "PlayerComponent"), nullptr)
        << GetParam()
        << " の自機に PlayerComponent が無い。未知の型名は警告だけ残して読み飛ばされる。"
           "型名が変わると読み込みは成功したまま保存済みの値が落ちる";
    EXPECT_NE(SceneNs::FindComponentEntry(*player, "PlayerStateManagerComponent"), nullptr)
        << GetParam() << " の自機に PlayerStateManagerComponent が無い。状態が 1 つも移らない";
}

TEST_P(ShippedScene, LoadedPlayerBuildsItsStateMachine)
{
    SceneNs::SceneData scene;
    ASSERT_TRUE(LoadShippedScene(scene, GetParam()));
    const SceneNs::ObjectData* object = FindPlayerObject(scene);
    ASSERT_NE(object, nullptr);

    std::unique_ptr<SceneNs::GameObject> live = SceneNs::BuildSceneObject(*object, nullptr);
    ASSERT_NE(live, nullptr);

    auto* player = live->FindComponent<PlayerNs::PlayerComponent>();
    auto* states = live->FindComponent<PlayerNs::PlayerStateManagerComponent>();
    ASSERT_NE(player, nullptr);
    ASSERT_NE(states, nullptr);

    live->OnStart();
    states->EnsureBuilt(*player);
    EXPECT_TRUE(states->IsBuilt());
    EXPECT_STREQ(states->CurrentName(), PlayerNs::IdlePlayerState::k_Name);
}

TEST_P(ShippedScene, EveryTuningFieldNameIsReflected)
{
    SceneNs::SceneData scene;
    ASSERT_TRUE(LoadShippedScene(scene, GetParam()));
    const SceneNs::ObjectData* object = FindPlayerObject(scene);
    ASSERT_NE(object, nullptr);

    std::unique_ptr<SceneNs::GameObject> live = SceneNs::BuildSceneObject(*object, nullptr);
    ASSERT_NE(live, nullptr);

    const std::vector<std::pair<const char*, const SceneNs::Component*>> targets{
        {"PlayerComponent", live->FindComponent<PlayerNs::PlayerComponent>()},
        {"PlayerStateManagerComponent", live->FindComponent<PlayerNs::PlayerStateManagerComponent>()},
    };

    for (const auto& [typeName, comp] : targets)
    {
        ASSERT_NE(comp, nullptr) << typeName;
        const nlohmann::json* entry = SceneNs::FindComponentEntry(*object, typeName);
        ASSERT_NE(entry, nullptr) << typeName;
        const auto fields = entry->find("fields");
        ASSERT_NE(fields, entry->end()) << typeName;

        const std::vector<std::string> reflected = ReflectedFieldNames(*comp);
        for (const auto& item : fields->items())
        {
            EXPECT_NE(std::find(reflected.begin(), reflected.end(), item.key()), reflected.end())
                << GetParam() << " の " << typeName << " に欄 " << item.key()
                << " があるが、この型は同じ名前を持たない。名前が違う欄は警告だけ残して捨てられ、値は既定のまま残る";
        }
    }
}

TEST_P(ShippedScene, PlayerComponentCarriesEveryTuningField)
{
    SceneNs::SceneData scene;
    ASSERT_TRUE(LoadShippedScene(scene, GetParam()));
    const SceneNs::ObjectData* object = FindPlayerObject(scene);
    ASSERT_NE(object, nullptr);

    const nlohmann::json* entry = SceneNs::FindComponentEntry(*object, "PlayerComponent");
    ASSERT_NE(entry, nullptr);
    const auto fields = entry->find("fields");
    ASSERT_NE(fields, entry->end());

    // 22 は今の同梱シーンが持つ欄数。保存はリフレクションの欄 30 件を全部書き出すので、開いて保存し直すと増える
    EXPECT_GE(fields->size(), 22u) << GetParam() << " の調整値の欄が減っている。落ちた欄は既定値で動く";
}

TEST_P(ShippedScene, LoadedPlayerKeepsTheTunedSlamValues)
{
    SceneNs::SceneData scene;
    ASSERT_TRUE(LoadShippedScene(scene, GetParam()));
    const SceneNs::ObjectData* object = FindPlayerObject(scene);
    ASSERT_NE(object, nullptr);

    std::unique_ptr<SceneNs::GameObject> live = SceneNs::BuildSceneObject(*object, nullptr);
    ASSERT_NE(live, nullptr);
    auto* player = live->FindComponent<PlayerNs::PlayerComponent>();
    ASSERT_NE(player, nullptr);
    live->OnStart();

    EXPECT_FLOAT_EQ(NsTest::ReadTuningField(*player, "突進距離"), 10.0f);
    EXPECT_FLOAT_EQ(NsTest::ReadTuningField(*player, "タップ距離"), 6.25f);
    EXPECT_FLOAT_EQ(NsTest::ReadTuningField(*player, "ジャンプ初速"), 12.0f);
    EXPECT_FLOAT_EQ(NsTest::ReadTuningField(*player, "コヨーテ時間"), 0.025f);
}

TEST_P(ShippedScene, PlayerShowsASkinnedModelOnAChild)
{
    SceneNs::SceneData scene;
    ASSERT_TRUE(LoadShippedScene(scene, GetParam()));
    const SceneNs::ObjectData* player = FindPlayerObject(scene);
    ASSERT_NE(player, nullptr);

    EXPECT_NE(SceneNs::FindComponentEntry(*player, "PlayerAnimatorComponent"), nullptr)
        << GetParam() << " の自機に PlayerAnimatorComponent が無い。絵が 1 本目のまま止まる";
    ASSERT_NE(SceneNs::FindComponentEntry(*player, "MeshRendererComponent"), nullptr)
        << GetParam()
        << " の自機に MeshRendererComponent の項目が無い。Player のコンストラクタが 1 つ積み、"
           "ApplyObjectComponents は書かれていない component を外さないので、項目が無いと有効のまま組まれる";

    std::unique_ptr<SceneNs::GameObject> live = SceneNs::BuildSceneObject(*player, nullptr);
    ASSERT_NE(live, nullptr);
    const auto* ownRenderer = live->FindComponent<NS::Object::MeshRendererComponent>();
    ASSERT_NE(ownRenderer, nullptr);
    EXPECT_FALSE(ownRenderer->IsActive())
        << GetParam()
        << " の自機が自分でも描いている。見た目は子の配置物が描くので、自機の箱と重なって 2 つ出る。"
           "メッシュ参照を空にしても cube へ落ちるだけで消えない";

    const SceneNs::ObjectData* skin = FindSkinObject(scene, player->objectId);
    ASSERT_NE(skin, nullptr) << GetParam() << " の自機の下に SkeletalAnimationComponent を持つ子が無い";

    EXPECT_NE(SceneNs::FindComponentEntry(*skin, "MeshRendererComponent"), nullptr)
        << GetParam()
        << " の見た目の子に MeshRendererComponent が無い。SkeletalAnimationComponent は"
           "同じ配置物の MeshRenderer にしかメッシュを差さないので、何も映らない";

    const nlohmann::json* animation = SceneNs::FindComponentEntry(*skin, "SkeletalAnimationComponent");
    ASSERT_NE(animation, nullptr);
    const nlohmann::json* fields = SceneNs::ComponentEntryFields(*animation);
    ASSERT_NE(fields, nullptr);
    const std::string modelRef = fields->value("モデル", std::string{});
    ASSERT_FALSE(modelRef.empty()) << GetParam() << " の見た目の子にモデルの参照が無い";

    const auto resolved = SceneNs::ResolveContentPath(modelRef);
    ASSERT_TRUE(resolved.has_value()) << modelRef << " は ContentRoot の外を指している";
    EXPECT_TRUE(std::filesystem::exists(*resolved))
        << modelRef << " が見当たらない。解決に失敗した参照は何もせず素通りするので、絵が出ないだけで気づけない";
}

TEST_P(ShippedScene, TheSkinnedModelUsesASkinningVertexShader)
{
    SceneNs::SceneData scene;
    ASSERT_TRUE(LoadShippedScene(scene, GetParam()));
    const SceneNs::ObjectData* player = FindPlayerObject(scene);
    ASSERT_NE(player, nullptr);
    const SceneNs::ObjectData* skin = FindSkinObject(scene, player->objectId);
    ASSERT_NE(skin, nullptr);

    const nlohmann::json* renderer = SceneNs::FindComponentEntry(*skin, "MeshRendererComponent");
    ASSERT_NE(renderer, nullptr);
    const nlohmann::json* fields = SceneNs::ComponentEntryFields(*renderer);
    ASSERT_NE(fields, nullptr);
    const std::string materialRef = fields->value("マテリアル", std::string{});

    const auto resolved = SceneNs::ResolveContentPath(materialRef);
    ASSERT_TRUE(resolved.has_value())
        << "見た目の子のマテリアルが " << materialRef
        << "。共有の player は骨なしの standard.vs.hlsl なので、骨の重みが読まれず絵が bind ポーズで固まる";
    ASSERT_TRUE(std::filesystem::exists(*resolved)) << materialRef << " が見当たらない";

    std::ifstream file(*resolved, std::ios::binary);
    ASSERT_TRUE(file.is_open()) << materialRef << " を開けない";
    const std::string text{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};

    SceneNs::MaterialFileDesc desc;
    std::string error;
    ASSERT_TRUE(SceneNs::ParseMaterialJson(text, desc, error)) << materialRef << " を読めない: " << error;

    EXPECT_EQ(desc.vertexShader.filename().generic_string(), "skinned.vs.hlsl")
        << materialRef
        << " の頂点シェーダが骨を回さない。クリップは進むのに絵が動かないので、原因が描画側だと気づきにくい";
}

TEST_P(ShippedScene, TheModelStandsOnTheBottomOfTheCapsule)
{
    SceneNs::SceneData scene;
    ASSERT_TRUE(LoadShippedScene(scene, GetParam()));
    const SceneNs::ObjectData* player = FindPlayerObject(scene);
    ASSERT_NE(player, nullptr);
    const SceneNs::ObjectData* skin = FindSkinObject(scene, player->objectId);
    ASSERT_NE(skin, nullptr);

    const nlohmann::json* capsule = SceneNs::FindComponentEntry(*player, "CapsuleColliderComponent");
    ASSERT_NE(capsule, nullptr);

    // 掛け方は CapsuleColliderComponent::CapsuleWorldMatrix の分解に揃える。ずれると試しだけ古くなる
    const NS::Core::Vector3 scale = SceneNs::ObjectScale(*player);
    const float halfHeight = ReadEntryFloat(*capsule, "半分の高さ") * std::abs(scale.y);
    const float radius = ReadEntryFloat(*capsule, "半径") * std::max(std::abs(scale.x), std::abs(scale.z));
    const float centerY = ReadEntryVec3Y(*capsule, "中心オフセット") * scale.y;
    const float bottom = centerY - (halfHeight + radius);

    const float feet = SceneNs::ObjectPosition(*skin).y * scale.y;

    EXPECT_NEAR(feet, bottom, 0.01f) << GetParam() << " の見た目の足元が当たりの底から " << (feet - bottom)
                                     << " m ずれている。正なら浮き、負なら地面へめり込む";
}

INSTANTIATE_TEST_SUITE_P(ScenePlayerLoad,
                         ShippedScene,
                         ::testing::Values("new_scene"),
                         [](const ::testing::TestParamInfo<const char*>& info) { return std::string{info.param}; });
