#include "Editor/LevelFilePaths.h"
#include "Game/Player/PlayerComponent.h"
#include "Game/Player/PlayerStateManagerComponent.h"
#include "Game/Player/PlayerStatsManagerComponent.h"
#include "Runtime/Object/Component.h"
#include "Runtime/Object/GameObject.h"
#include "Runtime/Object/Reflection/ComponentEntry.h"
#include "Runtime/Object/Reflection/ObjectBuilder.h"
#include "Runtime/Object/Reflection/Reflection.h"
#include "Runtime/Object/Scene/SceneData.h"
#include "Runtime/Object/Scene/SceneJson.h"

#include <algorithm>
#include <gtest/gtest.h>
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
    constexpr const char* k_StateList = "Idle;Walk;Fall;LedgeHanging;LedgeClimbing;BodySlam";

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
} // namespace

TEST_P(ShippedScene, PlayerCarriesTheThreeNewComponents)
{
    SceneNs::SceneData scene;
    ASSERT_TRUE(LoadShippedScene(scene, GetParam()));
    const SceneNs::ObjectData* player = FindPlayerObject(scene);
    ASSERT_NE(player, nullptr);

    EXPECT_NE(SceneNs::FindComponentEntry(*player, "PlayerComponent"), nullptr)
        << GetParam()
        << " の自機に PlayerComponent が無い。未知の型名は黙って読み飛ばされるので、"
           "書き換え漏れはこのテストでしか出ない";
    EXPECT_NE(SceneNs::FindComponentEntry(*player, "PlayerStatsManagerComponent"), nullptr)
        << GetParam() << " の自機に PlayerStatsManagerComponent が無い。調整値が既定へ化ける";
    EXPECT_NE(SceneNs::FindComponentEntry(*player, "PlayerStateManagerComponent"), nullptr)
        << GetParam() << " の自機に PlayerStateManagerComponent が無い。状態が 1 つも移らない";
}

TEST_P(ShippedScene, StateListIsTheSixStateOrderStartingAtIdle)
{
    SceneNs::SceneData scene;
    ASSERT_TRUE(LoadShippedScene(scene, GetParam()));
    const SceneNs::ObjectData* player = FindPlayerObject(scene);
    ASSERT_NE(player, nullptr);

    const nlohmann::json* entry = SceneNs::FindComponentEntry(*player, "PlayerStateManagerComponent");
    ASSERT_NE(entry, nullptr);
    EXPECT_EQ(SceneNs::FieldString(*entry, "状態一覧", ""), k_StateList);
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
    auto* stats = live->FindComponent<PlayerNs::PlayerStatsManagerComponent>();
    ASSERT_NE(player, nullptr);
    ASSERT_NE(states, nullptr);
    ASSERT_NE(stats, nullptr);

    live->OnStart();
    states->EnsureBuilt(*player);
    EXPECT_TRUE(states->IsBuilt());
    EXPECT_STREQ(states->CurrentName(), "Idle");
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
        {"PlayerStatsManagerComponent", live->FindComponent<PlayerNs::PlayerStatsManagerComponent>()},
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
                << " があるが、この型は同じ名前を持たない。名前が違う欄は黙って捨てられ、値は既定のまま残る";
        }
    }
}

TEST_P(ShippedScene, StatsManagerCarriesEveryTuningField)
{
    SceneNs::SceneData scene;
    ASSERT_TRUE(LoadShippedScene(scene, GetParam()));
    const SceneNs::ObjectData* object = FindPlayerObject(scene);
    ASSERT_NE(object, nullptr);

    const nlohmann::json* entry = SceneNs::FindComponentEntry(*object, "PlayerStatsManagerComponent");
    ASSERT_NE(entry, nullptr);
    const auto fields = entry->find("fields");
    ASSERT_NE(fields, entry->end());

    const std::size_t expected = std::string_view{GetParam()} == "collision_feel" ? 12u : 17u;
    EXPECT_EQ(fields->size(), expected) << GetParam() << " の調整値の欄が減っている。落ちた欄は既定値で動く";
}

INSTANTIATE_TEST_SUITE_P(ScenePlayerLoad,
                         ShippedScene,
                         ::testing::Values("collision_feel", "new_scene"),
                         [](const ::testing::TestParamInfo<const char*>& info) { return std::string{info.param}; });
