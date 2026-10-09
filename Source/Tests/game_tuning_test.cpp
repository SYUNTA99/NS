#include "Game/Level/CourseDirector.h"
#include "Game/Level/Goal.h"
#include "Game/Level/ImpactResolver.h"
#include "Game/Level/LaunchEffects.h"
#include "Game/Level/MapObj.h"
#include "Game/Level/TargetMarker.h"
#include "Game/Player.h"
#include "Game/Player/ChargeEffects.h"
#include "Game/Player/ImpactEffects.h"
#include "Game/Player/PlayerParams.h"
#include "NSlib/Object/SubObjects/HitSensor.h"
#include "NSlib/Object/ObjectJson.h"
#include "NSlib/Object/Reflection/ObjectBuilder.h"
#include "NSlib/Object/Reflection/ReflectionJson.h"
#include "NSlib/Object/Scene/Scene.h"
#include "NSlib/Object/ScreenFade.h"
#include "NSlib/Windows/Clock.h"

#include <cmath>
#include <gtest/gtest.h>

TEST(GameTuning, ImpactTimingEditsChangeTheShapeAndSurviveSceneReload)
{
    NS::Obj::Scene scene;
    nlohmann::json doc = NS::Obj::MakeSceneJson();
    nlohmann::json entry = NS::Obj::MakePrototypeJson<Player>();
    NS::Obj::SetObjectJsonId(entry, 1);
    NS::Obj::SceneJsonObjects(doc).push_back(std::move(entry));
    scene.LoadJson(std::move(doc));
    Player* player = FindPlayer(scene.Objects());
    ASSERT_NE(player, nullptr);
    ASSERT_EQ(NS::Obj::ApplyJsonFields(player->ImpactVisuals(),
                                       {{"核の留まりの上限フレーム", 2}, {"中心の火花の開始フレーム", 5}}),
              0u);
    NS::Game::Level::ImpactRecord impact;
    impact.tier = NS::Game::Level::HitTier::Center;
    impact.hitStopSteps = 12;
    EXPECT_EQ(player->ImpactVisuals().ShapeFor(impact).holdLastFrame, 2);
    EXPECT_EQ(player->ImpactVisuals().ShapeFor(impact).sparkStartFrame, 5);
    const nlohmann::json saved = scene.ToJson();
    scene.LoadJson(saved);
    player = FindPlayer(scene.Objects());
    ASSERT_NE(player, nullptr);
    EXPECT_EQ(player->ImpactVisuals().ShapeFor(impact).holdLastFrame, 2);
    EXPECT_EQ(player->ImpactVisuals().ShapeFor(impact).sparkStartFrame, 5);
}

TEST(GameTuning, ChargeLayerLifetimeReadsTheEditedField)
{
    NS::Obj::Scene scene;
    Player* player = scene.SpawnTransient<Player>();
    ASSERT_NE(player, nullptr);
    ASSERT_EQ(NS::Obj::ApplyJsonFields(player->ChargeVisuals(), {{"丸まりの殻の寿命フレーム", 2}}), 0u);
    for (int i = 0; i < 3; ++i)
    {
        player->Update(true);
    }
    const NS::Game::Player::EffectLayerList& layers = player->ChargeVisuals().Layers();
    ASSERT_FALSE(layers.Records().empty());
    const NS::Game::Player::EffectLayerRecord& curl = layers.Records().front();
    EXPECT_EQ(curl.name, "charge.curl");
    ASSERT_TRUE(curl.endStep.has_value());
    EXPECT_EQ(*curl.endStep - curl.startStep, 2);
}

TEST(GameTuning, ChargeAssetBindingReadsItsInstanceAndSurvivesSaving)
{
    NS::Obj::Scene scene;
    Player* player = scene.SpawnTransient<Player>();
    ASSERT_NE(player, nullptr);
    ASSERT_EQ(NS::Obj::ApplyJsonFields(player->ChargeVisuals(), {{"丸まりの殻の資産", "custom.curl"}}), 0u);
    player->Update(true);
    const NS::Game::Player::EffectLayerList& layers = player->ChargeVisuals().Layers();
    ASSERT_FALSE(layers.Records().empty());
    EXPECT_EQ(layers.Records().front().name, "custom.curl");
    const nlohmann::json saved = NS::Obj::SerializeSubObjectFields(player->ChargeVisuals());
    NS::Game::Player::ChargeEffects restored;
    ASSERT_EQ(NS::Obj::ApplyJsonFields(restored, saved), 0u);
    EXPECT_EQ(NS::Obj::SerializeSubObjectFields(restored)["丸まりの殻の資産"], "custom.curl");
}

TEST(GameTuning, DefaultSpawnReadsThePlayerInstance)
{
    Player player;
    player.EnsureInit();
    const float before = player.DefaultSpawnPosition().y;
    ASSERT_EQ(NS::Obj::ApplyJsonFields(player.Params(), {{"補う床の上面", 2.5f}, {"補う足元の余白", 0.11f}}), 0u);
    EXPECT_NEAR(player.DefaultSpawnPosition().y - before, 2.1f, 0.0001f);
}

TEST(GameTuning, EffectHistoryRetentionBelongsToEachList)
{
    NS::Game::Player::EffectLayerList shortHistory{2};
    NS::Game::Player::EffectLayerList longHistory{6};
    const std::uint32_t shortId = shortHistory.Play(nullptr, "test", {});
    const std::uint32_t longId = longHistory.Play(nullptr, "test", {});
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

TEST(GameTuning, LaunchDustLifetimeReadsItsOwnSubObject)
{
    NS::Obj::Scene scene;
    NS::Game::Level::MapObj* rock = scene.SpawnTransient<NS::Game::Level::MapObj>();
    ASSERT_NE(rock, nullptr);
    NS::Obj::SubObject* subObject = rock->FindSubObj("LaunchEffects");
    ASSERT_NE(subObject, nullptr);
    ASSERT_EQ(NS::Obj::ApplyJsonFields(*subObject, {{"着地の粉の寿命フレーム", 2}}), 0u);
    NS::Game::Level::LaunchEffects* effects = NS::Obj::Cast<NS::Game::Level::LaunchEffects>(subObject);
    ASSERT_NE(effects, nullptr);
    effects->BeginTrail(NS::Game::Level::HitTier::Center, 1.0f, 1.0f, {0.0f, 0.0f, 1.0f});
    effects->NotifyLanding({}, {0.0f, 1.0f, 0.0f});
    effects->BeginStep();
    effects->BeginStep();
    const NS::Game::Player::EffectLayerRecord& dust = effects->Layers().Records().back();
    EXPECT_EQ(dust.name, "launch.landDust");
    ASSERT_TRUE(dust.endStep.has_value());
    EXPECT_EQ(*dust.endStep - dust.startStep, 2);
}

TEST(GameTuning, EditedPowerRangeAndTrailAngleKeepTheOutputFinite)
{
    NS::Game::Player::ImpactEffects effects;
    ASSERT_EQ(
        NS::Obj::ApplyJsonFields(
            effects,
            {{"火花の数を振る威力の下限", 1.0f}, {"火花の数を振る威力の上限", 1.0f}, {"反動の尾の最小横成分", 2.0f}}),
        0u);
    NS::Game::Level::ImpactRecord impact;
    impact.tier = NS::Game::Level::HitTier::Center;
    impact.power = 1.0f;
    EXPECT_EQ(effects.ShapeFor(impact).sparkCount, 30);
    const NS::Vector3 heading = effects.ReboundTrailHeading({0.0f, 1.0f, 10.0f}, {}, NS::Vector3{0.0f, 0.0f, -5.0f});
    EXPECT_TRUE(std::isfinite(heading.x));
    EXPECT_TRUE(std::isfinite(heading.y));
    EXPECT_TRUE(std::isfinite(heading.z));
    EXPECT_NEAR(heading.Length(), 1.0f, 0.001f);
}

TEST(GameTuning, EditableSparkDirectionAndMarkerWidthReachTheirShapes)
{
    NS::Game::Player::ImpactEffects effects;
    ASSERT_EQ(NS::Obj::ApplyJsonFields(effects, {{"外れの火花の横の割合", 1.0f}}), 0u);
    const NS::Vector3 heading = effects.MissSparkHeading(0.8f, 0.0f, {0.0f, 0.0f, 1.0f});
    EXPECT_NEAR(heading.x, 1.0f, 0.001f);
    EXPECT_NEAR(heading.z, 0.0f, 0.001f);
    NS::Game::Level::TargetMarker marker;
    ASSERT_EQ(NS::Obj::ApplyJsonFields(marker, {{"枠の縁の幅", 3.0f}}), 0u);
    const nlohmann::json fields = NS::Obj::SerializeSubObjectFields(marker);
    EXPECT_EQ(fields["枠の縁の幅"], 3.0f);
    NS::Game::Level::TargetMarkerDesc desc;
    desc.outlineWidth = 3.0f;
    NS::AABB bounds;
    bounds.Center = {0.0f, 0.0f, 0.5f};
    bounds.Extents = {0.1f, 0.1f, 0.1f};
    NS::Game::Level::LockOnFrameShape frame;
    ASSERT_TRUE(NS::Game::Level::BuildLockOnFrame(NS::Matrix::Identity, {1280, 720}, bounds, {}, desc, frame));
    ASSERT_FALSE(frame.corners.empty());
    ASSERT_EQ(frame.outline.size(), frame.corners.size());
    EXPECT_NEAR(frame.outline.front().width - frame.corners.front().width, 6.0f, 0.001f);
}
