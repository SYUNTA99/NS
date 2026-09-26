#include "Editor/LevelFilePaths.h"
#include "Game/Level/CollisionInput.h"
#include "Game/Level/ImpactResolver.h"
#include "Game/Level/TargetMarker.h"
#include "Game/Player.h"
#include "Game/Player/PlayerComponent.h"
#include "Game/Player/PlayerStateManager.h"
#include "Game/Player/States/IdlePlayerState.h"
#include "Runtime/Object/Component.h"
#include "Runtime/Object/Components/MeshRenderer.h"
#include "Runtime/Object/GameObject.h"
#include "Runtime/Object/Reflection/ComponentEntry.h"
#include "Runtime/Object/Reflection/ObjectBuilder.h"
#include "Runtime/Object/Reflection/Reflection.h"
#include "Runtime/Object/Reflection/ReflectionJson.h"
#include "Runtime/Object/Scene/SceneJson.h"
#include "Runtime/Object/Transform.h"
#include "tuning_field_access.h"

#include <algorithm>
#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace SceneNs = NS::Obj;
namespace EditorNs = NS::Editor;
namespace PlayerNs = NS::Game::Player;
namespace LevelNs = NS::Game::Level;

namespace
{
    bool LoadShippedScene(nlohmann::json& outScene, std::string_view name)
    {
        const std::optional<std::string> path = EditorNs::BuildLevelPath(name);
        if (!path.has_value())
            return false;
        return SceneNs::LoadSceneFromJsonFile(outScene, *path);
    }

    const nlohmann::json* FindPlayerObject(const nlohmann::json& scene)
    {
        for (const nlohmann::json& object : SceneNs::SceneJsonObjects(scene))
        {
            if (SceneNs::ObjectJsonClass(object) == "Player")
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
} // namespace

TEST_P(ShippedScene, PlayerCarriesTheTwoNewComponents)
{
    nlohmann::json scene = SceneNs::MakeSceneJson();
    ASSERT_TRUE(LoadShippedScene(scene, GetParam()));
    const nlohmann::json* player = FindPlayerObject(scene);
    ASSERT_NE(player, nullptr);

    EXPECT_NE(SceneNs::FindComponentEntry(*player, "PlayerComponent"), nullptr)
        << GetParam()
        << " の自機に PlayerComponent が無い。未知の型名は警告だけ残して読み飛ばされる。"
           "型名が変わると読み込みは成功したまま保存済みの値が落ちる";
    EXPECT_NE(SceneNs::FindComponentEntry(*player, "PlayerStateManager"), nullptr)
        << GetParam() << " の自機に PlayerStateManager が無い。状態が 1 つも移らない";
}

TEST_P(ShippedScene, LoadedPlayerBuildsItsStateMachine)
{
    nlohmann::json scene = SceneNs::MakeSceneJson();
    ASSERT_TRUE(LoadShippedScene(scene, GetParam()));
    const nlohmann::json* object = FindPlayerObject(scene);
    ASSERT_NE(object, nullptr);

    std::unique_ptr<SceneNs::GameObject> live = SceneNs::ObjectFromJson(*object, nullptr);
    ASSERT_NE(live, nullptr);

    PlayerNs::PlayerComponent* player = live->FindComponent<PlayerNs::PlayerComponent>();
    PlayerNs::PlayerStateManager* states = live->FindComponent<PlayerNs::PlayerStateManager>();
    ASSERT_NE(player, nullptr);
    ASSERT_NE(states, nullptr);

    live->OnStart();
    states->EnsureBuilt(*player);
    EXPECT_TRUE(states->IsBuilt());
    EXPECT_TRUE(states->IsCurrent<PlayerNs::IdlePlayerState>());
}

TEST_P(ShippedScene, EveryTuningFieldNameIsReflected)
{
    nlohmann::json scene = SceneNs::MakeSceneJson();
    ASSERT_TRUE(LoadShippedScene(scene, GetParam()));
    const nlohmann::json* object = FindPlayerObject(scene);
    ASSERT_NE(object, nullptr);

    std::unique_ptr<SceneNs::GameObject> live = SceneNs::ObjectFromJson(*object, nullptr);
    ASSERT_NE(live, nullptr);

    const std::vector<std::pair<const char*, const SceneNs::Component*>> targets{
        {"PlayerComponent", live->FindComponent<PlayerNs::PlayerComponent>()},
        {"PlayerStateManager", live->FindComponent<PlayerNs::PlayerStateManager>()},
        {"CollisionInput", live->FindComponent<LevelNs::CollisionInput>()},
        {"ImpactResolver", live->FindComponent<LevelNs::ImpactResolver>()},
        {"TargetMarker", live->FindComponent<LevelNs::TargetMarker>()},
    };

    for (const std::pair<const char*, const SceneNs::Component*>& target : targets)
    {
        ASSERT_NE(target.second, nullptr) << target.first;
        const nlohmann::json* entry = SceneNs::FindComponentEntry(*object, target.first);
        ASSERT_NE(entry, nullptr) << target.first;
        const nlohmann::json::const_iterator fields = entry->find("fields");
        ASSERT_NE(fields, entry->end()) << target.first;

        const std::vector<std::string> reflected = ReflectedFieldNames(*target.second);
        for (nlohmann::json::const_iterator item = fields->begin(); item != fields->end(); ++item)
        {
            EXPECT_NE(std::find(reflected.begin(), reflected.end(), item.key()), reflected.end())
                << GetParam() << " の " << target.first << " に欄 " << item.key()
                << " があるが、この型は同じ名前を持たない。名前が違う欄は警告だけ残して捨てられ、値は既定のまま残る";
        }
    }
}

TEST_P(ShippedScene, PlayerComponentCarriesEveryTuningField)
{
    nlohmann::json scene = SceneNs::MakeSceneJson();
    ASSERT_TRUE(LoadShippedScene(scene, GetParam()));
    const nlohmann::json* object = FindPlayerObject(scene);
    ASSERT_NE(object, nullptr);

    const nlohmann::json* entry = SceneNs::FindComponentEntry(*object, "PlayerComponent");
    ASSERT_NE(entry, nullptr);
    const nlohmann::json::const_iterator fields = entry->find("fields");
    ASSERT_NE(fields, entry->end());

    // 22 は今の同梱シーンが持つ欄数。保存はリフレクションの欄 30 件を全部書き出すので、開いて保存し直すと増える
    EXPECT_GE(fields->size(), 22u) << GetParam() << " の調整値の欄が減っている。落ちた欄は既定値で動く";
}

TEST_P(ShippedScene, LoadedPlayerKeepsTheTunedSlamValues)
{
    nlohmann::json scene = SceneNs::MakeSceneJson();
    ASSERT_TRUE(LoadShippedScene(scene, GetParam()));
    const nlohmann::json* object = FindPlayerObject(scene);
    ASSERT_NE(object, nullptr);

    std::unique_ptr<SceneNs::GameObject> live = SceneNs::ObjectFromJson(*object, nullptr);
    ASSERT_NE(live, nullptr);
    PlayerNs::PlayerComponent* player = live->FindComponent<PlayerNs::PlayerComponent>();
    ASSERT_NE(player, nullptr);
    live->OnStart();

    EXPECT_FLOAT_EQ(NsTest::ReadTuningField(*player, "突進距離"), 10.0f);
    EXPECT_FLOAT_EQ(NsTest::ReadTuningField(*player, "タップ距離"), 6.25f);
    EXPECT_FLOAT_EQ(NsTest::ReadTuningField(*player, "ジャンプ初速"), 12.0f);
    EXPECT_FLOAT_EQ(NsTest::ReadTuningField(*player, "コヨーテ時間"), 0.025f);
}

// 自機の見た目は根のスケール 1 で描く。1 でないと玉が楕円に伸び、差し替えたモデルも同じ比で伸びる
// 色は灰色で、コードの既定とシーンの欄を揃える。保存済みの欄が既定を上書きするので、片方だけ直すと実機に出ない
TEST_P(ShippedScene, PlayerLooksUseUnitScaleAndTheDefaultGray)
{
    nlohmann::json scene = SceneNs::MakeSceneJson();
    ASSERT_TRUE(LoadShippedScene(scene, GetParam()));
    const nlohmann::json* object = FindPlayerObject(scene);
    ASSERT_NE(object, nullptr);

    std::unique_ptr<SceneNs::GameObject> shipped = SceneNs::ObjectFromJson(*object, nullptr);
    std::unique_ptr<SceneNs::GameObject> prototype =
        SceneNs::ObjectFromJson(MakePlayerObject(NS::Core::Vector3{}, NS::Core::Quaternion{}), nullptr);
    ASSERT_NE(shipped, nullptr);
    ASSERT_NE(prototype, nullptr);

    const NS::Core::Vector3 unit{1.0f, 1.0f, 1.0f};
    EXPECT_EQ(shipped->Root().Scale(), unit) << GetParam() << " の自機の根のスケールが 1 でない";
    EXPECT_EQ(prototype->Root().Scale(), unit) << "MakePlayerObject の根のスケールが 1 でない";

    const SceneNs::MeshRenderer* shippedMesh = shipped->FindComponent<SceneNs::MeshRenderer>();
    const SceneNs::MeshRenderer* prototypeMesh = prototype->FindComponent<SceneNs::MeshRenderer>();
    ASSERT_NE(shippedMesh, nullptr);
    ASSERT_NE(prototypeMesh, nullptr);
    const nlohmann::json shippedColor = SceneNs::SerializeComponent(*shippedMesh)["fields"]["基本色"];
    const nlohmann::json defaultColor = SceneNs::SerializeComponent(*prototypeMesh)["fields"]["基本色"];
    EXPECT_EQ(shippedColor, defaultColor) << GetParam() << " の自機の色がコードの既定と違う";
    ASSERT_EQ(defaultColor.size(), 3u);
    EXPECT_EQ(defaultColor[0], defaultColor[1]) << "既定の色が灰色でない";
    EXPECT_EQ(defaultColor[1], defaultColor[2]) << "既定の色が灰色でない";
}

INSTANTIATE_TEST_SUITE_P(ScenePlayerLoad,
                         ShippedScene,
                         ::testing::Values("new_scene"),
                         [](const ::testing::TestParamInfo<const char*>& info) { return std::string{info.param}; });
