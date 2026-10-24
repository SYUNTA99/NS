#include "Editor/EditorActors.h"
#include "Game/Level/CourseDirector.h"
#include "Game/Level/GoalActor.h"
#include "Game/Level/ImpactResolver.h"
#include "Game/Level/LaunchEffects.h"
#include "Game/Level/PropActor.h"
#include "Game/Level/TargetMarker.h"
#include "Game/Player/ChargeEffects.h"
#include "Game/Player/ImpactEffects.h"
#include "Game/Player/PlayerParams.h"
#include "Game/PlayerActor.h"
#include "NSlib/Core/Types.h"
#include "NSlib/CoreObject/ClassObjs/EffectKeeper.h"
#include "NSlib/CoreObject/ClassObjs/HitSensor.h"
#include "NSlib/CoreObject/ActorJson.h"
#include "NSlib/CoreObject/Reflection/ReflectionJson.h"
#include "NSlib/CoreObject/Scene/Scene.h"
#include "NSlib/Windows/Clock.h"

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
    ASSERT_EQ(NS::ApplyJsonFields(&player->ImpactVisuals(),
                                  {{"核の留まりの上限フレーム", 2}, {"中心の火花の開始フレーム", 5}}),
              0u);
    GL::Level::ImpactRecord impact;
    impact.tier = GL::Level::HitTier::Center;
    impact.hitStopSteps = 12;
    EXPECT_EQ(player->ImpactVisuals().ShapeFor(impact).holdLastFrame, 2);
    EXPECT_EQ(player->ImpactVisuals().ShapeFor(impact).sparkStartFrame, 5);
}

TEST(GameTuning, ChargeLayerLifetimeReadsTheEditedField)
{
    NS::Scene scene;
    PlayerActor* player = scene.SpawnActor<PlayerActor>("PlayerActor");
    ASSERT_NE(player, nullptr);
    ASSERT_EQ(NS::ApplyJsonFields(&player->ChargeVisuals(), {{"丸まりの殻の寿命フレーム", 2}}), 0u);
    for (int i = 0; i < 3; ++i)
    {
        player->Update(true);
    }
    const NS::EffectKeeper* keeper = player->GetEffectKeeper();
    ASSERT_NE(keeper, nullptr);
    ASSERT_FALSE(keeper->Records().empty());
    const NS::EffectRecord& curl = keeper->Records().front();
    EXPECT_EQ(curl.name, "charge.curl");
    ASSERT_TRUE(curl.endStep.has_value());
    EXPECT_EQ(*curl.endStep - curl.startStep, 2);
}

TEST(GameTuning, ChargeAssetBindingReadsItsInstanceAndSurvivesSaving)
{
    NS::Scene scene;
    PlayerActor* player = scene.SpawnActor<PlayerActor>("PlayerActor");
    ASSERT_NE(player, nullptr);
    ASSERT_EQ(NS::ApplyJsonFields(&player->ChargeVisuals(), {{"丸まりの殻の資産", "custom.curl"}}), 0u);
    player->Update(true);
    const NS::EffectKeeper* keeper = player->GetEffectKeeper();
    ASSERT_NE(keeper, nullptr);
    ASSERT_FALSE(keeper->Records().empty());
    EXPECT_EQ(keeper->Records().front().name, "custom.curl");
    const nlohmann::json saved = NS::SerializeClassObjFields(&player->ChargeVisuals());
    GL::Player::ChargeEffects restored;
    ASSERT_EQ(NS::ApplyJsonFields(&restored, saved), 0u);
    EXPECT_EQ(NS::SerializeClassObjFields(&restored)["丸まりの殻の資産"], "custom.curl");
}

TEST(GameTuning, EffectHistoryRetentionBelongsToEachKeeper)
{
    NS::EffectKeeper shortHistory{2};
    NS::EffectKeeper longHistory{6};
    const NS::uint32 shortId = shortHistory.Play(nullptr, "test", {});
    const NS::uint32 longId = longHistory.Play(nullptr, "test", {});
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

TEST(GameTuning, LaunchDustLifetimeReadsItsOwnClassObj)
{
    NS::Scene scene;
    GL::Level::PropActor* rock = scene.SpawnActor<GL::Level::PropActor>("PropActor");
    ASSERT_NE(rock, nullptr);
    GL::Level::LaunchEffects* effects = &rock->Effects();
    ASSERT_EQ(NS::ApplyJsonFields(effects, {{"着地の粉の寿命フレーム", 2}}), 0u);
    const NS::SphereCollision* sphere = NS::Cast<NS::SphereCollision>(rock->CollisionClassObj());
    ASSERT_NE(sphere, nullptr);
    effects->BeginTrail(
        rock,
        NS::GetFrameDelta(rock),
        GL::Level::TackleReleaseDesc{},
        rock->Flight(),
        &rock->Params(),
        sphere->WorldSphere(rock->Root().WorldMatrix()));
    effects->NotifyLanding(rock, {}, {0.0f, 1.0f, 0.0f});
    NS::EffectKeeper* keeper = rock->GetEffectKeeper();
    ASSERT_NE(keeper, nullptr);
    keeper->BeginStep(rock->GetEffectScene());
    keeper->BeginStep(rock->GetEffectScene());
    ASSERT_FALSE(keeper->Records().empty());
    const NS::EffectRecord& dust = keeper->Records().back();
    EXPECT_EQ(dust.name, "launch.landDust");
    ASSERT_TRUE(dust.endStep.has_value());
    EXPECT_EQ(*dust.endStep - dust.startStep, 2);
}

TEST(GameTuning, EditedPowerRangeAndTrailAngleKeepTheOutputFinite)
{
    GL::Player::ImpactEffects effects;
    ASSERT_EQ(
        NS::ApplyJsonFields(
            &effects,
            {{"火花の数を振る威力の下限", 1.0f}, {"火花の数を振る威力の上限", 1.0f}, {"反動の尾の最小横成分", 2.0f}}),
        0u);
    GL::Level::ImpactRecord impact;
    impact.tier = GL::Level::HitTier::Center;
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
    GL::Player::ImpactEffects effects;
    ASSERT_EQ(NS::ApplyJsonFields(&effects, {{"外れの火花の横の割合", 1.0f}}), 0u);
    const NS::Vector3 forward{0.0f, 0.0f, 1.0f};
    const NS::Vector3 heading =
        effects.MissSparkHeading(0.8f, 0.0f, GL::Level::MissSurfaceNormal(0.8f, 0.0f, 2.0f, forward), forward);
    EXPECT_NEAR(heading.x, 1.0f, 0.001f);
    EXPECT_NEAR(heading.z, 0.0f, 0.001f);
    GL::Level::TargetMarker marker;
    ASSERT_EQ(NS::ApplyJsonFields(&marker, {{"枠の縁の幅", 3.0f}}), 0u);
    const nlohmann::json fields = NS::SerializeClassObjFields(&marker);
    EXPECT_EQ(fields["枠の縁の幅"], 3.0f);
    GL::Level::TargetMarkerDesc desc;
    desc.outlineWidth = 3.0f;
    NS::AABB bounds;
    bounds.Center = {0.0f, 0.0f, 0.5f};
    bounds.Extents = {0.1f, 0.1f, 0.1f};
    GL::Level::LockOnFrameShape frame;
    ASSERT_TRUE(GL::Level::BuildLockOnFrame(NS::Matrix::Identity, {1280, 720}, bounds, {}, desc, frame));
    ASSERT_FALSE(frame.corners.empty());
    ASSERT_EQ(frame.outline.size(), frame.corners.size());
    EXPECT_NEAR(frame.outline.front().width - frame.corners.front().width, 6.0f, 0.001f);
}
