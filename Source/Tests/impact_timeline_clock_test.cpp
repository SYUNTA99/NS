#include "Game/Level/HitTimeline.h"
#include "Game/Level/ImpactResolver.h"
#include "Game/Level/MapObj.h"
#include "Game/Player.h"
#include "Game/Player/ImpactEffects.h"
#include "Runtime/Core/OBB.h"
#include "Runtime/Object/Components/Body.h"
#include "Runtime/Object/Components/CameraManager.h"
#include "Runtime/Object/Components/CameraModifier.h"
#include "Runtime/Object/Components/HitReaction.h"
#include "Runtime/Object/Components/TransformComponent.h"
#include "Runtime/Object/ObjectJson.h"
#include "Runtime/Object/Scene/Scene.h"
#include "Runtime/Platform/Input.h"
#include "Tests/TestHitTimelines.h"
#include "Tests/TestViewCamera.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <string_view>
#include <vector>

// 当たりの返りの時間を、タイムラインの事象が置いたフレームで決める事を縛る
// 横軸は検知のフレームを 0 にしたフレーム数

namespace
{
    using namespace NS::Game::Level;

    // 自機 (id 1) と、その前の置物 (id 2) の場面。mass が正なら置物の質量を書く
    Player* PlaceClockScene(NS::Obj::Scene& scene, float rockX, float rockZ, float mass)
    {
        nlohmann::json doc = NS::Obj::MakeSceneJson();
        nlohmann::json player = NS::Obj::MakeObjectJson();
        NS::Obj::SetObjectJsonClass(player, "Player");
        NS::Obj::SetObjectJsonId(player, 1);
        NS::Obj::SetObjectPosition(player, NS::Core::Vector3{0.0f, 1.0f, 0.0f});
        NS::Obj::SceneJsonObjects(doc).push_back(std::move(player));
        nlohmann::json rock = NS::Obj::MakeObjectJson();
        NS::Obj::SetObjectJsonClass(rock, "MapObj");
        NS::Obj::SetObjectJsonId(rock, 2);
        NS::Obj::SetObjectPosition(rock, NS::Core::Vector3{rockX, 0.5f, rockZ});
        if (mass > 0.0f)
        {
            NS::Obj::ObjectJsonParts(rock)["Params"]["質量"] = mass;
        }
        NS::Obj::SceneJsonObjects(doc).push_back(std::move(rock));
        scene.LoadJson(doc);
        NS::Core::OBB floor{};
        floor.center = NS::Core::Vector3{0.0f, -0.5f, 0.0f};
        floor.halfExtentX = 100.0f;
        floor.halfExtentY = 0.5f;
        floor.halfExtentZ = 100.0f;
        scene.Physics().AddBox(floor, NS::Phys::ObjectLayers::Terrain);
        PlaceViewCamera(scene, NS::Core::Vector3{}, NS::Core::Vector3{0.0f, 0.0f, 1.0f});
        return NS::Obj::Cast<Player>(scene.Objects().FindByObjectId(1));
    }

    MapObj* RockOf(NS::Obj::Scene& scene)
    {
        return NS::Obj::Cast<MapObj>(scene.Objects().FindByObjectId(2));
    }

    // 検知のフレームを 0 にした 1 フレームの姿
    struct ClockFrame
    {
        bool hitStopping = false;
        bool awaitingRebound = false;
        bool canMoveBody = true;
        bool freezeBegan = false;
        bool released = false;
        bool rebounding = false;
        bool rockFrozen = false;
        bool rockFlying = false;
        NS::Core::Vector3 shape{1.0f, 1.0f, 1.0f};
        bool shapeAnimating = false;
        int flashRemaining = 0;
        float padLeft = 0.0f;
        bool shaking = false;
        float zoom = 1.0f;
        int hitLayers = 0;    // 出した当たりの核の数
        int flightLayers = 0; // 出した反動の尾の数
    };

    int CountLayers(Player& player, std::string_view name)
    {
        int count = 0;
        for (const NS::Game::Player::EffectLayerRecord& record : player.ImpactVisuals().Layers().Records())
        {
            if (record.name == name)
            {
                ++count;
            }
        }
        return count;
    }

    // 突進を出して当たりまで回し、検知のフレームから frames フレームぶんの姿を並べる。当たらなければ空
    std::vector<ClockFrame> RunHit(Player& player, MapObj& rock, float charge01, int frames)
    {
        std::vector<ClockFrame> trace;
        player.RequestBodySlam(charge01, NS::Core::Vector3{0.0f, 0.0f, 1.0f});
        bool detected = false;
        for (int frame = 0; frame < 60 + frames && static_cast<int>(trace.size()) < frames; ++frame)
        {
            player.Update(false);
            rock.Update();
            if (!detected && player.Resolver().LastImpact().sequence == 0)
            {
                continue;
            }
            detected = true;
            ClockFrame now;
            now.hitStopping = player.Resolver().IsHitStopping();
            now.awaitingRebound = player.Resolver().IsAwaitingRebound();
            now.canMoveBody = player.CanMoveBody();
            now.freezeBegan = player.Resolver().FreezeBeganThisStep();
            now.released = player.Resolver().ReleasedThisStep();
            now.rebounding = player.IsRebounding();
            now.rockFrozen = rock.IsFrozen();
            now.rockFlying = rock.IsFlying();
            now.shape = player.Resolver().ShapeFactors();
            now.shapeAnimating = player.Resolver().IsShapeAnimating();
            now.flashRemaining = player.HitReactionPart()->FlashFramesRemaining();
            now.padLeft = NS::Platform::Input::Get().Gamepad(0).Vibration().left;
            if (const NS::Obj::CameraManager* cameras = player.GetCameraManager())
            {
                now.shaking = cameras->FindModifier<NS::Obj::CameraShakeModifier>() != nullptr;
                now.zoom = cameras->ZoomRoll().zoom;
            }
            now.hitLayers = CountLayers(player, "impact.core");
            now.flightLayers = CountLayers(player, "rebound.trail");
            trace.push_back(now);
        }
        return trace;
    }

    int CountStopFrames(const std::vector<ClockFrame>& trace)
    {
        int count = 0;
        for (const ClockFrame& frame : trace)
        {
            if (frame.hitStopping)
            {
                ++count;
            }
        }
        return count;
    }

    // 止め [1, 5]、明けの 6 に相手を飛ばし、8 に反動
    HitTimeline MakeSplitReleaseTimeline()
    {
        HitTimeline timeline;
        timeline.events = {
            {HitStopEvent{}, 1, 5, HitDirection::Any},
            {TargetFreezeEvent{}, 1, 5, HitDirection::Any},
            {TargetLaunchEvent{}, 6, 1, HitDirection::Any},
            {ReboundEvent{}, 8, 1, HitDirection::Any},
        };
        return timeline;
    }
} // namespace

TEST(ImpactTimelineClock, EventsStartTheStopReleaseAndReboundOnTheirFrames)
{
    const ScopedHitTimelineDirectory directory("Clock");
    ScopedHitTimelineDirectory::SetBothTiers(MakeSplitReleaseTimeline());
    NS::Obj::Scene scene;
    Player* player = PlaceClockScene(scene, 0.0f, 0.6f, 0.0f);
    ASSERT_NE(player, nullptr);
    const std::vector<ClockFrame> trace = RunHit(*player, *RockOf(scene), 1.0f, 10);
    ASSERT_EQ(trace.size(), 10u);
    for (int k = 0; k < 10; ++k)
    {
        SCOPED_TRACE(k);
        const ClockFrame& frame = trace[static_cast<std::size_t>(k)];
        EXPECT_EQ(frame.hitStopping, k >= 1 && k <= 5);
        EXPECT_EQ(frame.awaitingRebound, k >= 6 && k <= 7);
        EXPECT_EQ(frame.canMoveBody, k == 0 || k >= 8);
        EXPECT_EQ(frame.freezeBegan, k == 1);
        EXPECT_EQ(frame.released, k == 6);
        EXPECT_EQ(frame.rockFrozen, k >= 1 && k <= 5);
        EXPECT_EQ(frame.rockFlying, k >= 6);
        EXPECT_EQ(frame.rebounding, k >= 8);
    }
    EXPECT_EQ(player->Resolver().LastImpact().hitStopSteps, 5);
}

// R-1: 止めの長さを決めるのはタイムラインだけ。12 を 8 にすると止めが 8 になる
TEST(ImpactTimelineClock, TheStopIsAsLongAsTheTimelineSays)
{
    for (const int steps : {12, 8})
    {
        SCOPED_TRACE(steps);
        const ScopedHitTimelineDirectory directory("StopLength");
        ScopedHitTimelineDirectory::SetBothTiers(MakeLegacyHitTimeline(steps));
        NS::Obj::Scene scene;
        Player* player = PlaceClockScene(scene, 0.0f, 0.6f, 0.0f);
        ASSERT_NE(player, nullptr);
        const std::vector<ClockFrame> trace = RunHit(*player, *RockOf(scene), 1.0f, steps + 4);
        EXPECT_EQ(CountStopFrames(trace), steps);
        EXPECT_EQ(player->Resolver().LastImpact().hitStopSteps, steps);
    }
}

// R-3b: 止めの長さは溜めと相手の質量で変わらない
TEST(ImpactTimelineClock, TheStopIgnoresChargeAndMass)
{
    for (const float charge : {0.0f, 1.0f})
    {
        for (const float mass : {0.5f, 8.0f})
        {
            SCOPED_TRACE(charge);
            SCOPED_TRACE(mass);
            const ScopedHitTimelineDirectory directory("StopFixed");
            ScopedHitTimelineDirectory::SetBothTiers(MakeLegacyHitTimeline(12));
            NS::Obj::Scene scene;
            Player* player = PlaceClockScene(scene, 0.0f, 0.6f, mass);
            ASSERT_NE(player, nullptr);
            const std::vector<ClockFrame> trace = RunHit(*player, *RockOf(scene), charge, 16);
            ASSERT_FALSE(trace.empty());
            EXPECT_EQ(CountStopFrames(trace), 12);
        }
    }
}

// 引けない段の当たりは、止めも形も出さず、検知のフレームに反動と相手を飛ばすだけを出す
TEST(ImpactTimelineClock, AMissingTimelineReleasesOnTheDetectionFrame)
{
    const ScopedHitTimelineDirectory directory("Missing");
    NS::Obj::Scene scene;
    Player* player = PlaceClockScene(scene, 0.0f, 0.6f, 0.0f);
    ASSERT_NE(player, nullptr);
    const std::vector<ClockFrame> trace = RunHit(*player, *RockOf(scene), 1.0f, 4);
    ASSERT_EQ(trace.size(), 4u);
    EXPECT_TRUE(trace[0].rebounding);
    EXPECT_TRUE(trace[0].rockFlying);
    EXPECT_EQ(CountStopFrames(trace), 0);
    for (const ClockFrame& frame : trace)
    {
        EXPECT_FALSE(frame.freezeBegan);
        EXPECT_FALSE(frame.shapeAnimating);
    }
    EXPECT_EQ(player->Resolver().LastImpact().hitStopSteps, 0);
}

// 同じフレームに始まる事象はファイルの並びの順に起きる
TEST(ImpactTimelineClock, EventsOnTheSameFrameRunInFileOrder)
{
    for (const bool launchFirst : {true, false})
    {
        SCOPED_TRACE(launchFirst);
        HitTimeline timeline;
        timeline.events = {{HitStopEvent{}, 1, 2, HitDirection::Any}, {ReboundEvent{}, 4, 1, HitDirection::Any}};
        const HitEvent launch{TargetLaunchEvent{}, 3, 1, HitDirection::Any};
        const HitEvent freeze{TargetFreezeEvent{}, 3, 5, HitDirection::Any};
        if (launchFirst)
        {
            timeline.events.push_back(launch);
            timeline.events.push_back(freeze);
        }
        else
        {
            timeline.events.push_back(freeze);
            timeline.events.push_back(launch);
        }
        const ScopedHitTimelineDirectory directory("Order");
        ScopedHitTimelineDirectory::SetBothTiers(timeline);
        NS::Obj::Scene scene;
        Player* player = PlaceClockScene(scene, 0.0f, 0.6f, 0.0f);
        ASSERT_NE(player, nullptr);
        const std::vector<ClockFrame> trace = RunHit(*player, *RockOf(scene), 1.0f, 4);
        ASSERT_EQ(trace.size(), 4u);
        // 後に起きた方が残る。止めの後に飛ばすと飛び、飛ばした後に止めると止まる
        EXPECT_EQ(trace[3].rockFrozen, launchFirst);
    }
}

// 形は形の事象の曲線が答え、事象の長さを過ぎると元の形へ戻る
TEST(ImpactTimelineClock, TheShapeFollowsItsCurvesForItsLength)
{
    HitTimeline timeline;
    ShapeEvent shape;
    shape.along.count = 1;
    shape.along.keys[0] = NS::Obj::Curve::Key{0.0f, 1.0f};
    shape.height.count = 2;
    shape.height.keys[0] = NS::Obj::Curve::Key{0.0f, 2.0f};
    shape.height.keys[1] = NS::Obj::Curve::Key{3.0f, 1.25f};
    timeline.events = {{HitStopEvent{}, 1, 2, HitDirection::Any},
                       {shape, 1, 4, HitDirection::Any},
                       {ReboundEvent{}, 3, 1, HitDirection::Any}};
    const ScopedHitTimelineDirectory directory("Shape");
    ScopedHitTimelineDirectory::SetBothTiers(timeline);
    NS::Obj::Scene scene;
    Player* player = PlaceClockScene(scene, 0.0f, 0.6f, 0.0f);
    ASSERT_NE(player, nullptr);
    const std::vector<ClockFrame> trace = RunHit(*player, *RockOf(scene), 1.0f, 7);
    ASSERT_EQ(trace.size(), 7u);
    EXPECT_FALSE(trace[0].shapeAnimating);
    EXPECT_FLOAT_EQ(trace[1].shape.y, 2.0f);
    EXPECT_FLOAT_EQ(trace[2].shape.y, 1.75f);
    EXPECT_FLOAT_EQ(trace[4].shape.y, 1.25f);
    EXPECT_TRUE(trace[4].shapeAnimating);
    EXPECT_FALSE(trace[5].shapeAnimating);
    EXPECT_FLOAT_EQ(trace[5].shape.y, 1.0f);
}

// 外れの向きは面の上の位置の絶対値の大きい方の軸。等しい時は左右
TEST(ImpactTimelineClock, TheMissDirectionIsTheLargerAxisOfTheFacePosition)
{
    EXPECT_EQ(HitDirectionOf(0.8f, 0.1f), HitDirection::Right);
    EXPECT_EQ(HitDirectionOf(-0.8f, 0.1f), HitDirection::Left);
    EXPECT_EQ(HitDirectionOf(0.1f, 0.9f), HitDirection::Up);
    EXPECT_EQ(HitDirectionOf(0.1f, -0.9f), HitDirection::Down);
    EXPECT_EQ(HitDirectionOf(0.5f, -0.5f), HitDirection::Right);
    EXPECT_EQ(HitDirectionOf(-0.5f, 0.5f), HitDirection::Left);
}

// 向きの付いた事象は、その向きの当たりでだけ起きる
TEST(ImpactTimelineClock, DirectedEventsRunOnlyForTheirDirection)
{
    // 先に向きを知る。横へずらした相手は外れになる
    HitDirection direction = HitDirection::Any;
    {
        const ScopedHitTimelineDirectory directory("Direction");
        ScopedHitTimelineDirectory::SetBothTiers(MakeLegacyHitTimeline(2));
        NS::Obj::Scene scene;
        Player* player = PlaceClockScene(scene, 0.75f, 0.6f, 0.0f);
        ASSERT_NE(player, nullptr);
        ASSERT_FALSE(RunHit(*player, *RockOf(scene), 1.0f, 1).empty());
        direction = player->Resolver().LastImpact().direction;
        ASSERT_NE(direction, HitDirection::Any);
    }
    for (const bool matching : {true, false})
    {
        SCOPED_TRACE(matching);
        HitDirection rowDirection = direction;
        if (!matching)
        {
            rowDirection = HitDirection::Up;
            if (direction == HitDirection::Up)
            {
                rowDirection = HitDirection::Down;
            }
        }
        HitTimeline timeline;
        timeline.events = {{TargetLaunchEvent{}, 1, 1, rowDirection}, {ReboundEvent{}, 1, 1, HitDirection::Any}};
        const ScopedHitTimelineDirectory directory("Direction");
        ScopedHitTimelineDirectory::SetBothTiers(timeline);
        NS::Obj::Scene scene;
        Player* player = PlaceClockScene(scene, 0.75f, 0.6f, 0.0f);
        ASSERT_NE(player, nullptr);
        const std::vector<ClockFrame> trace = RunHit(*player, *RockOf(scene), 1.0f, 3);
        ASSERT_EQ(trace.size(), 3u);
        EXPECT_EQ(trace[2].rockFlying, matching);
    }
}

// 走っている途中に次の当たりが起きたら、前のタイムラインの事象を止めて始め直す
TEST(ImpactTimelineClock, ANewHitAbortsTheRunningTimeline)
{
    HitTimeline timeline;
    ShapeEvent shape;
    shape.along.count = 1;
    shape.along.keys[0] = NS::Obj::Curve::Key{0.0f, 1.0f};
    shape.height.count = 1;
    shape.height.keys[0] = NS::Obj::Curve::Key{0.0f, 2.0f};
    timeline.events = {{HitStopEvent{}, 1, 1, HitDirection::Any},
                       {shape, 1, 50, HitDirection::Any},
                       {FlashEvent{}, 1, 50, HitDirection::Any},
                       {TargetLaunchEvent{}, 2, 1, HitDirection::Any}};
    const ScopedHitTimelineDirectory directory("Abort");
    ScopedHitTimelineDirectory::SetBothTiers(timeline);
    NS::Obj::Scene scene;
    Player* player = PlaceClockScene(scene, 0.0f, 0.6f, 0.0f);
    ASSERT_NE(player, nullptr);
    MapObj* rock = RockOf(scene);
    const std::vector<ClockFrame> first = RunHit(*player, *rock, 1.0f, 4);
    ASSERT_EQ(first.size(), 4u);
    ASSERT_TRUE(first[3].shapeAnimating);
    ASSERT_GT(first[3].flashRemaining, 0);

    // 2 回目は形の事象の無いタイムラインで当てる。前の形が残っていれば、新しい時計の上でまた動き出す
    HitTimeline noShape;
    noShape.events = {{HitStopEvent{}, 1, 1, HitDirection::Any}, {TargetLaunchEvent{}, 2, 1, HitDirection::Any}};
    ScopedHitTimelineDirectory::SetBothTiers(noShape);

    // 飛んだ相手を自機の前へ戻し、置いて当て直す
    rock->Root().SetPosition(player->Root().Position() + NS::Core::Vector3{0.0f, -0.5f, 0.6f});
    player->Body().SetGrounded(true);
    player->RequestBodySlam(1.0f, NS::Core::Vector3{0.0f, 0.0f, 1.0f});
    bool rehit = false;
    for (int frame = 0; frame < 20 && !rehit; ++frame)
    {
        player->Update(false);
        rock->Update();
        rehit = player->Resolver().LastImpact().sequence == 2;
        if (rehit)
        {
            // 前の当たりの返りも止まる。2 回目のタイムラインには白が無い
            EXPECT_FALSE(player->Resolver().IsShapeAnimating());
            EXPECT_EQ(player->HitReactionPart()->FlashFramesRemaining(), 0);
        }
    }
    ASSERT_TRUE(rehit);
    player->Update(false);
    rock->Update();
    EXPECT_TRUE(player->Resolver().FreezeBeganThisStep());
    EXPECT_FALSE(player->Resolver().IsShapeAnimating());
}

// 白・振動・寄り・揺れ・当たりと飛びの絵は、それぞれの事象が置いたフレームに始まる
TEST(ImpactTimelineClock, ReturnsStartOnTheirOwnFrames)
{
    FlashEvent flash;
    flash.alpha = 0.5f;
    PadVibrationEvent pad;
    pad.left.count = 2;
    pad.left.keys[0] = NS::Obj::Curve::Key{0.0f, 0.8f};
    pad.left.keys[1] = NS::Obj::Curve::Key{4.0f, 0.0f};
    ZoomRollEvent zoom;
    zoom.zoom = 1.2f;
    zoom.returnFrames = 2;
    HitTimeline timeline;
    timeline.events = {
        {HitStopEvent{}, 1, 5, HitDirection::Any},
        {TargetFreezeEvent{}, 1, 5, HitDirection::Any},
        {HitEffectEvent{}, 2, 1, HitDirection::Any},
        {pad, 2, 4, HitDirection::Any},
        {zoom, 2, 5, HitDirection::Any},
        {flash, 3, 4, HitDirection::Any},
        {CameraShakeEvent{}, 4, 6, HitDirection::Any},
        {TargetLaunchEvent{}, 6, 1, HitDirection::Any},
        {ReboundEvent{}, 6, 1, HitDirection::Any},
        {FlightEffectEvent{}, 7, 1, HitDirection::Any},
    };
    const ScopedHitTimelineDirectory directory("Returns");
    ScopedHitTimelineDirectory::SetBothTiers(timeline);
    (void)NS::Platform::Input::Get().Gamepad(0).SetVibration(0.0f, 0.0f);
    NS::Obj::Scene scene;
    Player* player = PlaceClockScene(scene, 0.0f, 0.6f, 0.0f);
    ASSERT_NE(player, nullptr);
    const std::vector<ClockFrame> trace = RunHit(*player, *RockOf(scene), 1.0f, 10);
    ASSERT_EQ(trace.size(), 10u);
    for (int k = 0; k < 10; ++k)
    {
        SCOPED_TRACE(k);
        const ClockFrame& frame = trace[static_cast<std::size_t>(k)];
        EXPECT_EQ(frame.hitLayers, static_cast<int>(k >= 2));
        EXPECT_EQ(frame.flightLayers, static_cast<int>(k >= 7));
        EXPECT_EQ(frame.shaking, k >= 4);
        float expectedZoom = 1.0f;
        if (k >= 2)
        {
            expectedZoom = 1.2f;
        }
        EXPECT_FLOAT_EQ(frame.zoom, expectedZoom);
        // 白は始めたフレームに 4、そこから 1 ずつ減る
        int expectedFlash = 0;
        if (k >= 3)
        {
            expectedFlash = std::max(4 - (k - 3), 0);
        }
        EXPECT_EQ(frame.flashRemaining, expectedFlash);
        // 振動は始めたフレームから曲線をなぞり、長さの 4 フレームを過ぎると 0
        float expectedPad = 0.0f;
        if (k >= 2 && k < 6)
        {
            expectedPad = 0.8f * (1.0f - static_cast<float>(k - 2) / 4.0f);
        }
        EXPECT_NEAR(frame.padLeft, expectedPad, 1e-6f);
    }
    // 当たりの記録は検知のフレームに、タイムラインの返りの始めの値を持つ
    const ImpactRecord& impact = player->Resolver().LastImpact();
    EXPECT_EQ(impact.flashStart, 4);
    EXPECT_FLOAT_EQ(impact.zoomStart, 1.2f);
    EXPECT_FLOAT_EQ(impact.padStart.left, 0.8f);
    EXPECT_GT(impact.cameraShake, 0.0f);
}

// 揺れの最初の振れは 強さ × 威力 × 質量の効き。横と縦の重みは向きだけを決める
TEST(ImpactTimelineClock, TheShakeIsItsStrengthScaledByTheHit)
{
    struct Row
    {
        float strength;
        float side;
        float up;
    };
    std::vector<float> shakes;
    for (const Row row : {Row{0.1f, 0.0f, 1.0f}, Row{0.2f, 0.0f, 1.0f}, Row{0.2f, 3.0f, 4.0f}})
    {
        CameraShakeEvent shake;
        shake.strength = row.strength;
        shake.sideWeight = row.side;
        shake.upWeight = row.up;
        HitTimeline timeline = MakeLegacyHitTimeline(2);
        timeline.events.push_back({shake, 1, 2, HitDirection::Any});
        const ScopedHitTimelineDirectory directory("Shake");
        ScopedHitTimelineDirectory::SetBothTiers(timeline);
        NS::Obj::Scene scene;
        Player* player = PlaceClockScene(scene, 0.0f, 0.6f, 2.0f);
        ASSERT_NE(player, nullptr);
        ASSERT_FALSE(RunHit(*player, *RockOf(scene), 1.0f, 1).empty());
        shakes.push_back(player->Resolver().LastImpact().cameraShake);
    }
    ASSERT_EQ(shakes.size(), 3u);
    EXPECT_GT(shakes[0], 0.0f);
    EXPECT_NEAR(shakes[1], shakes[0] * 2.0f, 1e-6f);
    EXPECT_NEAR(shakes[2], shakes[1], 1e-6f);
}
