#include "Game/Level/ImpactResolver.h"
#include "Game/Player.h"
#include "NSlib/Core/OBB.h"
#include "NSlib/Object/Components/PlayerInput.h"
#include "NSlib/Object/Components/TransformComponent.h"
#include "NSlib/Object/ObjectJson.h"
#include "NSlib/Object/Scene/Scene.h"
#include "Tests/TestViewCamera.h"

#include <gtest/gtest.h>

namespace
{
    // 自機 (id 1) と、その前の置物 (id 2) だけの場面の文書
    nlohmann::json MakeSlamSceneJson()
    {
        nlohmann::json doc = NS::Obj::MakeSceneJson();
        nlohmann::json player = NS::Obj::MakeObjectJson();
        NS::Obj::SetObjectJsonClass(player, "Player");
        NS::Obj::SetObjectJsonId(player, 1);
        NS::Obj::SetObjectPosition(player, NS::Vector3{0.0f, 1.0f, 0.0f});
        NS::Obj::SceneJsonObjects(doc).push_back(std::move(player));
        nlohmann::json rock = NS::Obj::MakeObjectJson();
        NS::Obj::SetObjectJsonClass(rock, "MapObj");
        NS::Obj::SetObjectJsonId(rock, 2);
        NS::Obj::SetObjectPosition(rock, NS::Vector3{0.0f, 0.5f, 3.0f});
        NS::Obj::SceneJsonObjects(doc).push_back(std::move(rock));
        return doc;
    }

    // 床は文書に載らない当たりなので、場面ごとに足す
    void AddFloor(NS::Obj::Scene& scene)
    {
        NS::OBB floor{};
        floor.center = NS::Vector3{0.0f, -0.5f, 0.0f};
        floor.halfExtentX = 100.0f;
        floor.halfExtentY = 0.5f;
        floor.halfExtentZ = 100.0f;
        scene.Physics().AddBox(floor, NS::Phys::ObjectLayers::Terrain);
    }

    struct PreviewResult
    {
        int impactFrame = -1;
        NS::Game::Level::ImpactRecord record{};
    };

    // 編集中の場面の写しから別の場面を組み、入力を止めた自機へ突進を直接頼んで、当たるまで場面の 1 歩で進める
    PreviewResult RunPreview(const nlohmann::json& snapshot)
    {
        PreviewResult result;
        NS::Obj::Scene preview;
        preview.LoadJson(snapshot);
        AddFloor(preview);
        PlaceViewCamera(preview, NS::Vector3{}, NS::Vector3{0.0f, 0.0f, 1.0f});
        Player* player = NS::Obj::Cast<Player>(preview.Objects().FindByObjectId(1));
        if (player == nullptr)
        {
            return result;
        }
        player->Input().SetLocked(true);
        player->RequestBodySlam(1.0f, NS::Vector3{0.0f, 0.0f, 1.0f});
        for (int frame = 0; frame < 60; ++frame)
        {
            preview.OnUpdate();
            if (player->Resolver().LastImpact().sequence > 0)
            {
                result.impactFrame = frame;
                result.record = player->Resolver().LastImpact();
                break;
            }
        }
        return result;
    }
} // namespace

// 下見の前提。編集中の場面の写しから別の場面を立てて当たりまで進めても、編集中の場面は動かず、
// 同じ写しから 2 回立てると同じ当たりになる
TEST(PreviewScene, CopiedSceneReachesTheImpactWithoutTouchingTheEditedScene)
{
    NS::Obj::Scene edited;
    edited.LoadJson(MakeSlamSceneJson());
    AddFloor(edited);
    edited.SetSimulationEnabled(false);
    Player* editedPlayer = NS::Obj::Cast<Player>(edited.Objects().FindByObjectId(1));
    ASSERT_NE(editedPlayer, nullptr);
    const NS::Vector3 editedPosition = editedPlayer->Root().Position();
    const nlohmann::json snapshot = edited.ToJson();

    const PreviewResult first = RunPreview(snapshot);
    const PreviewResult second = RunPreview(snapshot);

    ASSERT_GE(first.impactFrame, 0);
    EXPECT_EQ(first.record.targetId, 2u);
    EXPECT_EQ(second.impactFrame, first.impactFrame);
    EXPECT_EQ(second.record.hitStopSteps, first.record.hitStopSteps);
    EXPECT_FLOAT_EQ(second.record.power, first.record.power);
    EXPECT_EQ(second.record.tier, first.record.tier);

    EXPECT_EQ(edited.ToJson(), snapshot);
    EXPECT_TRUE(editedPlayer->Root().Position() == editedPosition);
    EXPECT_EQ(editedPlayer->Resolver().LastImpact().sequence, 0u);
}
