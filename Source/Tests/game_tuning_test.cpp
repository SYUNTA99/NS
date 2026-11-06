#include "Editor/EditorActors.h"
#include "Game/Level/GameScene.h"
#include "Game/Level/GoalActor.h"
#include "Game/Level/ImpactResolver.h"
#include "Game/Player/ImpactEffects.h"
#include "Game/Player/PlayerParams.h"
#include "Game/PlayerActor.h"
#include "NSlib/Core/Types.h"
#include "NSlib/Core/Windows/Clock.h"
#include "NSlib/CoreObject/ActorJson.h"
#include "NSlib/CoreObject/ClassObjs/EffectObj.h"
#include "NSlib/CoreObject/ClassObjs/HitSensor.h"
#include "NSlib/CoreObject/Reflection/ReflectionJson.h"
#include "NSlib/CoreObject/Scene/Scene.h"

#include <cmath>
#include <gtest/gtest.h>

TEST(GameTuning, ImpactTimingEditsChangeTheShape)
{
    NS::Scene scene;
    nlohmann::json doc = NS::MakeSceneJson();
    nlohmann::json entry = NS::Editor::MakePrototypeJson<PlayerActor>();
    NS::SetActorJsonId(entry, 1);
    NS::SceneJsonActors(doc).push_back(std::move(entry));
    scene.ResetFromJson(std::move(doc));
    PlayerActor* player = FindPlayer(scene.Actors());
    ASSERT_NE(player, nullptr);

    ASSERT_EQ(
        NS::ApplyJsonFields(&player->ImpactVisuals(), {{"核の留まりの上限フレーム", 2}, {"中心の火花の開始フレーム", 5}}),
        0u);
    GL::Level::ImpactRecord impact;
    impact.tier = GL::Level::HitTier::Center;
    impact.hitStopSteps = 12;
    EXPECT_EQ(player->ImpactVisuals().ShapeFor(impact).holdLastFrame, 2);
    EXPECT_EQ(player->ImpactVisuals().ShapeFor(impact).sparkStartFrame, 5);
}

TEST(GameTuning, EffectHistoryRetentionBelongsToEachKeeper)
{
    NS::Effect asset{"11111111111111111111111111111111"};
    asset.Replace({}, NS::Gfx::EffectResourceDesc{asset.Id(), "test", {}, 1});
    NS::EffectObj shortHistory{2};
    NS::EffectObj longHistory{6};
    const NS::uint32 shortId = shortHistory.Play(nullptr, &asset, {});
    const NS::uint32 longId = longHistory.Play(nullptr, &asset, {});
    shortHistory.Stop(nullptr, shortId);
    longHistory.Stop(nullptr, longId);
    for (int frame = 0; frame < 3; ++frame)
    {
        shortHistory.BeginStep(nullptr);
        longHistory.BeginStep(nullptr);
    }
    EXPECT_TRUE(shortHistory.Records().empty());
    EXPECT_EQ(longHistory.Records().size(), 1u);
}

TEST(GameTuning, EditedTrailAngleKeepsTheOutputFinite)
{
    GL::Player::ImpactEffects effects;
    ASSERT_EQ(NS::ApplyJsonFields(&effects, {{"反動の尾の最小横成分", 2.0f}}), 0u);
    const NS::Vector3 heading = effects.ReboundTrailHeading({0.0f, 1.0f, 10.0f}, {}, NS::Vector3{0.0f, 0.0f, -5.0f});
    EXPECT_TRUE(std::isfinite(heading.x));
    EXPECT_TRUE(std::isfinite(heading.y));
    EXPECT_TRUE(std::isfinite(heading.z));
    EXPECT_NEAR(heading.Length(), 1.0f, 0.001f);
}

TEST(GameTuning, EditableSparkDirectionReachesItsShape)
{
    GL::Player::ImpactEffects effects;

    ASSERT_EQ(NS::ApplyJsonFields(&effects, {{"外れの火花の横の割合", 1.0f}}), 0u);
    const NS::Vector3 forward{0.0f, 0.0f, 1.0f};
    const NS::Vector3 heading =
        effects.MissSparkHeading(0.8f, 0.0f, GL::Level::MissSurfaceNormal(0.8f, 0.0f, 2.0f, forward), forward);
    EXPECT_NEAR(heading.x, 1.0f, 0.001f);
    EXPECT_NEAR(heading.z, 0.0f, 0.001f);
}
