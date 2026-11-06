#include "Editor/EditorActors.h"
#include "Game/Level/GameScene.h"
#include "Game/Level/ImpactResolver.h"
#include "Game/Level/LevelMessages.h"
#include "Game/Level/MapObjParams.h"
#include "Game/Player/PlayerParams.h"
#include "Game/Player/States/BrakePlayerState.h"
#include "Game/Player/States/FallPlayerState.h"
#include "Game/Player/States/IdlePlayerState.h"
#include "Game/Player/States/ReboundPlayerState.h"
#include "Game/Player/States/TackleHitPlayerState.h"
#include "Game/Player/States/WalkPlayerState.h"
#include "Game/PlayerActor.h"
#include "NSlib/Core/Types.h"
#include "NSlib/CoreObject/Action/ActorAction.h"
#include "NSlib/CoreObject/ClassObjs/Body.h"
#include "NSlib/CoreObject/ClassObjs/Collider.h"
#include "NSlib/CoreObject/ClassObjs/HitReaction.h"
#include "NSlib/CoreObject/ClassObjs/Model.h"
#include "NSlib/CoreObject/ClassObjs/PlayerInput.h"
#include "NSlib/CoreObject/ClassObjs/Transform.h"
#include "NSlib/CoreObject/ActorJson.h"
#include "NSlib/CoreObject/Reflection/ReflectionJson.h"
#include "NSlib/CoreObject/Scene/Scene.h"
#include "NSlib/Core/Windows/Clock.h"
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
        if (player->States().IsCurrent<GL::Player::TackleHitPlayerState>())
        {
            return "TackleHit";
        }
        if (player->States().IsCurrent<GL::Player::ReboundPlayerState>())
        {
            return "Rebound";
        }
        return "Other";
    }

    void ExpectSameVector(const NS::Vector3& actual, const NS::Vector3& expected)
    {
        EXPECT_NEAR(actual.x, expected.x, 0.00001f);
        EXPECT_NEAR(actual.y, expected.y, 0.00001f);
        EXPECT_NEAR(actual.z, expected.z, 0.00001f);
    }

    // 移す前の式が出した止めの長さ stopSteps の当たりを写した段の束。形は止めの間潰れ、明けに伸びて戻る
    NS::HitReactionSet MakeLegacyHitSet(std::string name, int stopSteps)
    {
        const float n = static_cast<float>(stopSteps);
        NS::HitShapeItem shape;
        shape.along.count = 3;
        shape.along.keys[0] = NS::Curve::Key{0.0f, 0.7f};
        shape.along.keys[1] = NS::Curve::Key{n - 1.0f, 0.7f};
        shape.along.keys[2] = NS::Curve::Key{n, 1.0f};
        shape.height.count = 5;
        shape.height.keys[0] = NS::Curve::Key{0.0f, 1.1f};
        shape.height.keys[1] = NS::Curve::Key{n - 1.0f, 1.1f};
        shape.height.keys[2] = NS::Curve::Key{n, 1.2f};
        shape.height.keys[3] = NS::Curve::Key{n + 3.0f, 0.9f};
        shape.height.keys[4] = NS::Curve::Key{n + 6.0f, 1.0f};
        NS::HitReactionSet set;
        set.name = std::move(name);
        set.stopFrames = stopSteps;
        set.entries.push_back(NS::HitReactionEntry{.item = shape, .length = stopSteps + 6});
        return set;
    }

    // 自機の .actor の段の束 2 つを MakeLegacyHitSet に替え、当たる直前の動作を外す (予測の線を引かない)
    void UseLegacyHitSets(nlohmann::json& actorFile, int stopSteps)
    {
        std::vector<NS::HitReactionSet> sets = NS::ReadHitReactionSets("PlayerActor", actorFile);
        for (const GL::Level::HitTier tier : {GL::Level::HitTier::Center, GL::Level::HitTier::Wide})
        {
            const std::string name = GL::Level::TackleSetName("体当たり", tier);
            bool replaced = false;
            for (NS::HitReactionSet& set : sets)
            {
                if (set.name == name)
                {
                    set = MakeLegacyHitSet(name, stopSteps);
                    replaced = true;
                }
            }
            EXPECT_TRUE(replaced) << name;
        }
        NS::WriteHitReactionSets(actorFile, sets);
        NS::WriteActorActions(actorFile, {});
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
    const NS::Vector3 position = player->Root().Position();

    for (int frame = 0; frame < 30; ++frame)
    {
        scene.OnUpdate();
    }

    ExpectSameVector(player->Root().Position(), position);
}

