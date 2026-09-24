#include "Game/Level/KillZone.h"
#include "Game/Level/Respawner.h"
#include "Game/Player.h"

#include <gtest/gtest.h>
#include <Runtime/Object/Scene/Scene.h>
#include <utility>

namespace LevelNs = NS::Game::Level;
namespace SceneNs = NS::Obj;

//! 即死体積が LateUpdate 帯で自分から重なりを判定し、プレイヤーを即死させることを検証する

TEST(KillZoneTest, KillsPlayerInsideVolume)
{
    SceneNs::Scene scene;
    nlohmann::json data = SceneNs::MakeSceneJson();
    // 体積の中心と同じ位置に置けば必ず重なる
    SceneNs::SceneJsonObjects(data).push_back(MakePlayerObject(NS::Core::Vector3{0.0f, -55.0f, 0.0f}, NS::Core::Quaternion{}));
    SceneNs::SceneJsonObjects(data).push_back(LevelNs::MakeKillZoneObject());
    scene.LoadJson(std::move(data));

    Player* player = FindPlayer(scene.Objects());
    ASSERT_NE(player, nullptr);
    ASSERT_FALSE(player->IsDead());

    // 同じ帯に居る respawner を切って判定だけを見る。有効なままだと同じ LateUpdate で復活する
    player->FindComponent<LevelNs::Respawner>()->SetActive(false);
    scene.Objects().UpdateObjects(SceneNs::TickPriority::LateUpdate);

    EXPECT_TRUE(player->IsDead());
}

TEST(KillZoneTest, DoesNotKillPlayerAboveVolume)
{
    SceneNs::Scene scene;
    nlohmann::json data = SceneNs::MakeSceneJson();
    // 上面 y=-50 より十分上に居れば重ならない
    SceneNs::SceneJsonObjects(data).push_back(MakePlayerObject(NS::Core::Vector3{}, NS::Core::Quaternion{}));
    SceneNs::SceneJsonObjects(data).push_back(LevelNs::MakeKillZoneObject());
    scene.LoadJson(std::move(data));

    scene.Objects().UpdateObjects(SceneNs::TickPriority::LateUpdate);

    Player* player = FindPlayer(scene.Objects());
    ASSERT_NE(player, nullptr);
    EXPECT_FALSE(player->IsDead());
}
