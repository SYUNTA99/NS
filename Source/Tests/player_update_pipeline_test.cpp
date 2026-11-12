#include "Editor/EditorActors.h"
#include "Game/Level/GameScene.h"
#include "Game/Level/LevelMessages.h"
#include "Game/Player/PlayerParams.h"
#include "Game/Player/States/BrakePlayerState.h"
#include "Game/Player/States/FallPlayerState.h"
#include "Game/Player/States/IdlePlayerState.h"
#include "Game/Player/States/WalkPlayerState.h"
#include "Game/PlayerActor.h"
#include "NSlib/Core/Types.h"
#include "NSlib/Core/Windows/Clock.h"
#include "NSlib/CoreObject/Action/ActorActionInfo.h"
#include "NSlib/CoreObject/ActorJson.h"
#include "NSlib/CoreObject/ClassObjs/Body.h"
#include "NSlib/CoreObject/ClassObjs/Collider.h"
#include "NSlib/CoreObject/ClassObjs/HitReaction.h"
#include "NSlib/CoreObject/ClassObjs/Model.h"
#include "NSlib/CoreObject/ClassObjs/PlayerInput.h"
#include "NSlib/CoreObject/ClassObjs/Transform.h"
#include "NSlib/CoreObject/Reflection/ReflectionJson.h"
#include "NSlib/CoreObject/Scene/Scene.h"
#include "Tests/TestActorFile.h"
#include "Tests/TestSceneSetup.h"
#include "Tests/TestViewCamera.h"

#include <gtest/gtest.h>

#include <string>
#include <string_view>
#include <vector>

namespace
{
    PlayerActor* PlacePipelinePlayer(NS::Scene& scene)
    {
        nlohmann::json doc = NS::MakeSceneJson();
        AddActorJson(doc, "PlayerActor", 1, NS::Vector3{0.0f, 1.0f, 0.0f});
        scene.ResetFromJson(doc);
        AddTerrainBox(scene, NS::Vector3{0.0f, -0.5f, 0.0f}, 100.0f, 0.5f, 100.0f);
        PlaceViewCamera(scene, NS::Vector3{}, NS::Vector3{0.0f, 0.0f, 1.0f});
        return NS::Cast<PlayerActor>(scene.Actors().FindByActorId(1));
    }

    // 見本の列に書く状態の綴り。表に無い状態は Other
    std::string_view StateName(const PlayerActor* player)
    {
        NS_ASSERT(Game, player != nullptr, "StateName: 渡したアクターか部品が無い");
        if (player->States().IsCurrent<GL::Player::IdlePlayerState>())
        {
            return "Idle";
        }
        if (player->States().IsCurrent<GL::Player::WalkPlayerState>())
        {
            return "Walk";
        }
        if (player->States().IsCurrent<GL::Player::BrakePlayerState>())
        {
            return "Brake";
        }
        if (player->States().IsCurrent<GL::Player::FallPlayerState>())
        {
            return "Fall";
        }
        return "Other";
    }

    void ExpectSameVector(const NS::Vector3& actual, const NS::Vector3& expected)
    {
        EXPECT_NEAR(actual.x, expected.x, 0.00001f);
        EXPECT_NEAR(actual.y, expected.y, 0.00001f);
        EXPECT_NEAR(actual.z, expected.z, 0.00001f);
    }
} // namespace

// 編集の休止の持ち主は世界の駆動だけ。身体と入力の部品を起こしたままでも、止めた世界では自機が動かない
TEST(PlayerUpdatePipeline, StoppedWorldHoldsThePlayerWithItsPartsAwake)
{
    NS::Scene scene;
    PlayerActor* player = PlacePipelinePlayer(scene);
    ASSERT_NE(player, nullptr);
    scene.SetSimulationEnabled(false);
    player->Input().SetDesiredMove(NS::Vector3{1.0f, 0.0f, 0.0f}, 1.0f);
    // 自機の更新が実機の入力で歩きを消しても、回っていればこの速さで動く
    player->Body().SetVelocity(NS::Vector3{0.0f, 2.0f, 0.0f});
    const NS::Vector3 position = player->TransformObj().Position();

    for (int frame = 0; frame < 30; ++frame)
    {
        scene.OnUpdate();
    }

    ExpectSameVector(player->TransformObj().Position(), position);
}
