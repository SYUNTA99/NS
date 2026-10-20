#include "Game/Level/ImpactResolver.h"
#include "Game/PlayerActor.h"
#include "NSlib/Core/OBB.h"
#include "NSlib/CoreObject/ClassObjs/PlayerInput.h"
#include "NSlib/CoreObject/ClassObjs/Transform.h"
#include "NSlib/CoreObject/ObjectJson.h"
#include "NSlib/CoreObject/Scene/Scene.h"
#include "Tests/TestViewCamera.h"

#include <gtest/gtest.h>

namespace
{
    // 自機 (id 1) と、その前の置物 (id 2) だけの場面の文書
    nlohmann::json MakeSlamSceneJson()
    {
        nlohmann::json doc = NS::MakeSceneJson();
        nlohmann::json player = NS::MakeObjectJson();
        NS::SetObjectJsonActorFile(player, "PlayerActor");
        NS::SetObjectJsonId(player, 1);
        NS::SetObjectPosition(player, NS::Vector3{0.0f, 1.0f, 0.0f});
        NS::SceneJsonObjects(doc).push_back(std::move(player));
        nlohmann::json rock = NS::MakeObjectJson();
        NS::SetObjectJsonActorFile(rock, "PropActor");
        NS::SetObjectJsonId(rock, 2);
        NS::SetObjectPosition(rock, NS::Vector3{0.0f, 0.5f, 3.0f});
        NS::SceneJsonObjects(doc).push_back(std::move(rock));
        return doc;
    }

    // 床は文書に載らない当たりなので、場面ごとに足す
    void AddFloor(NS::Scene& scene)
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
        GL::Level::ImpactRecord record{};
    };

    // 編集中の場面の写しから別の場面を組み、入力を止めた自機へ突進を直接頼んで、当たるまで場面の 1 歩で進める
    PreviewResult RunPreview(const nlohmann::json& snapshot)
    {
        PreviewResult result;
        NS::Scene preview;
        preview.ResetFromJson(snapshot);
        AddFloor(preview);
        PlaceViewCamera(preview, NS::Vector3{}, NS::Vector3{0.0f, 0.0f, 1.0f});
        PlayerActor* player = NS::Cast<PlayerActor>(preview.Objects().FindByObjectId(1));
        if (player == nullptr)
        {
            return result;
        }
        player->Input().SetLocked(true);
        player->RequestBodySlam(1.0f, NS::Vector3{0.0f, 0.0f, 1.0f});
        for (int frame = 0; frame < 60; ++frame)
        {
            preview.OnUpdate();
            if (player->Resolver().Answer().lastImpact.sequence > 0)
            {
                result.impactFrame = frame;
                result.record = player->Resolver().Answer().lastImpact;
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
    NS::Scene edited;
    edited.ResetFromJson(MakeSlamSceneJson());
    AddFloor(edited);
    edited.SetSimulationEnabled(false);
    PlayerActor* editedPlayer = NS::Cast<PlayerActor>(edited.Objects().FindByObjectId(1));
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
    EXPECT_EQ(editedPlayer->Resolver().Answer().lastImpact.sequence, 0u);
}
