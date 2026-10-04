#include "Game/Level/HitTimeline.h"
#include "Game/Level/ImpactResolver.h"
#include "Game/Level/MapObj.h"
#include "Game/Player.h"
#include "Game/Player/ImpactEffects.h"
#include "Runtime/Core/OBB.h"
#include "Runtime/Object/Components/Body.h"
#include "Runtime/Object/Components/CameraManager.h"
#include "Runtime/Object/Components/CameraModifier.h"
#include "Runtime/Object/Components/Collider.h"
#include "Runtime/Object/Components/HitReaction.h"
#include "Runtime/Object/Components/Model.h"
#include "Runtime/Object/Components/TransformComponent.h"
#include "Runtime/Object/ITickable.h"
#include "Runtime/Object/IUse/IUseCamera.h"
#include "Runtime/Object/ObjectJson.h"
#include "Runtime/Object/Scene/Scene.h"
#include "Runtime/Object/UpdatePhase.h"
#include "Runtime/Platform/Clock.h"
#include "Runtime/Platform/Input.h"
#include "Tests/TestHitTimelines.h"
#include "Tests/TestViewCamera.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <optional>
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
    std::vector<ClockFrame> RunHit(Player& player, MapObj& rock, float charge01, int frames, float overcharge01 = 0.0f)
    {
        std::vector<ClockFrame> trace;
        player.RequestBodySlam(charge01, NS::Core::Vector3{0.0f, 0.0f, 1.0f}, 0.0f, overcharge01);
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

// 止めの長さを決めるのはタイムラインだけ。12 を 8 にすると止めが 8 になる
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

// 止めの長さは溜めと相手の質量で変わらない
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

// 段階的な明けの事象は置いたフレームに世界を遅くする。使った止めの種類は当たりの記録に残る
TEST(ImpactTimelineClock, GradualReleaseSlowsTheWorldFromItsFrame)
{
    HitTimeline timeline = MakeSplitReleaseTimeline();
    GradualReleaseEvent release;
    release.startSpeed = 0.25f;
    timeline.events.push_back({release, 6, 1, HitDirection::Any});
    const ScopedHitTimelineDirectory directory("GradualRelease");
    ScopedHitTimelineDirectory::SetBothTiers(timeline);
    NS::Obj::Scene scene;
    Player* player = PlaceClockScene(scene, 0.0f, 0.6f, 0.0f);
    ASSERT_NE(player, nullptr);
    MapObj* rock = RockOf(scene);
    ASSERT_EQ(RunHit(*player, *rock, 1.0f, 6).size(), 6u);
    EXPECT_FLOAT_EQ(scene.WorldSpeed(), 1.0f);
    player->Update(false);
    rock->Update();
    EXPECT_FLOAT_EQ(scene.WorldSpeed(), 0.25f);
    EXPECT_TRUE(player->Resolver().LastImpact().localStop);
    EXPECT_TRUE(player->Resolver().LastImpact().gradualRelease);
    // やり直しで当たりを打ち切ると、遅い世界も持ち越さない
    player->ResetState();
    EXPECT_FLOAT_EQ(scene.WorldSpeed(), 1.0f);
}

// 欄「紫の時だけ」の段階的な明けは、溜めすぎて放した突進の当たりだけで世界を遅くする。赤の溜めきりでは遅くしない
TEST(ImpactTimelineClock, PurpleOnlyGradualReleaseSkipsTheRedHit)
{
    HitTimeline timeline = MakeSplitReleaseTimeline();
    GradualReleaseEvent release;
    release.startSpeed = 0.25f;
    release.overchargedOnly = true;
    timeline.events.push_back({release, 6, 1, HitDirection::Any});
    const ScopedHitTimelineDirectory directory("PurpleRelease");
    ScopedHitTimelineDirectory::SetBothTiers(timeline);
    for (int purple = 0; purple < 2; ++purple)
    {
        SCOPED_TRACE(purple);
        NS::Obj::Scene scene;
        Player* player = PlaceClockScene(scene, 0.0f, 0.6f, 0.0f);
        ASSERT_NE(player, nullptr);
        MapObj* rock = RockOf(scene);
        float overcharge = 0.0f;
        if (purple == 1)
        {
            overcharge = 0.4f;
        }
        ASSERT_EQ(RunHit(*player, *rock, 1.0f, 6, overcharge).size(), 6u);
        player->Update(false);
        rock->Update();
        EXPECT_FLOAT_EQ(player->Resolver().LastImpact().overcharge01, overcharge);
        if (purple == 1)
        {
            EXPECT_FLOAT_EQ(scene.WorldSpeed(), 0.25f);
            EXPECT_TRUE(player->Resolver().LastImpact().gradualRelease);
        }
        else
        {
            EXPECT_FLOAT_EQ(scene.WorldSpeed(), 1.0f);
            EXPECT_FALSE(player->Resolver().LastImpact().gradualRelease);
        }
    }
}

TEST(ImpactTimelineClock, TheRecordNamesOnlyTheStopsTheTimelineUsed)
{
    HitTimeline timeline;
    timeline.events = {{TargetLaunchEvent{}, 1, 1, HitDirection::Any}, {ReboundEvent{}, 1, 1, HitDirection::Any}};
    const ScopedHitTimelineDirectory directory("NoStop");
    ScopedHitTimelineDirectory::SetBothTiers(timeline);
    NS::Obj::Scene scene;
    Player* player = PlaceClockScene(scene, 0.0f, 0.6f, 0.0f);
    ASSERT_NE(player, nullptr);
    ASSERT_FALSE(RunHit(*player, *RockOf(scene), 1.0f, 2).empty());
    EXPECT_FALSE(player->Resolver().LastImpact().localStop);
    EXPECT_FALSE(player->Resolver().LastImpact().gradualRelease);
}

// トラウマの揺れは始まりのフレームにトラウマを足し、続けて当てると前のトラウマに足される
TEST(ImpactTimelineClock, TraumaEventAddsTraumaThatStacksOnTheNextHit)
{
    CameraTraumaEvent trauma;
    trauma.trauma = 0.3f;
    trauma.decayPerSecond = 0.0f;
    HitTimeline timeline;
    timeline.events = {{HitStopEvent{}, 1, 1, HitDirection::Any},
                       {trauma, 1, 1, HitDirection::Any},
                       {TargetLaunchEvent{}, 2, 1, HitDirection::Any}};
    const ScopedHitTimelineDirectory directory("Trauma");
    ScopedHitTimelineDirectory::SetBothTiers(timeline);
    NS::Obj::Scene scene;
    // 正面の当たりにする。外れは相手を脇へ押し、置き直した後も脇へ飛び続ける相手は 2 回目の突進の道から外れる
    Player* player = PlaceClockScene(scene, 0.0f, 0.6f, 0.0f);
    ASSERT_NE(player, nullptr);
    MapObj* rock = RockOf(scene);
    ASSERT_EQ(RunHit(*player, *rock, 1.0f, 4).size(), 4u);
    const NS::Obj::CameraManager* cameras = player->GetCameraManager();
    ASSERT_NE(cameras, nullptr);
    const float added = player->Resolver().LastImpact().cameraTrauma;
    EXPECT_GT(added, 0.0f);
    EXPECT_NEAR(cameras->Trauma(), added, 1.0e-5f);
    // 次の当たりは前の当たりの返りを止めるが、トラウマは残して足す
    player->HitReactionPart()->Stop();
    rock->Root().SetPosition(player->Root().Position() + NS::Core::Vector3{0.0f, -0.5f, 0.6f});
    player->Body().SetGrounded(true);
    player->RequestBodySlam(1.0f, NS::Core::Vector3{0.0f, 0.0f, 1.0f});
    bool rehit = false;
    for (int frame = 0; frame < 30 && !rehit; ++frame)
    {
        player->Update(false);
        rock->Update();
        rehit = player->Resolver().LastImpact().sequence == 2;
    }
    ASSERT_TRUE(rehit);
    player->Update(false);
    rock->Update();
    EXPECT_GT(cameras->Trauma(), added);
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

// 外れの火花は外した側へ 7 割、相手の表面に沿って滑る向きへ 3 割で流す。面の真ん中は向きが決まらない
TEST(ImpactTimelineClock, MissSparksFlowTowardTheSideThatWasMissed)
{
    const NS::Core::Vector3 forward{0.0f, 0.0f, 1.0f};
    // 右の縁: 外した側 +X、滑る向き (0.6, 0, 0.8)。0.7 × (1, 0, 0) + 0.3 × (0.6, 0, 0.8) を正規化
    const NS::Core::Vector3 right = NS::Game::Player::ImpactEffects::MissSparkHeading(0.8f, 0.0f, forward);
    EXPECT_NEAR(right.x, 0.9648f, 0.0005f);
    EXPECT_NEAR(right.y, 0.0f, 0.0005f);
    EXPECT_NEAR(right.z, 0.2631f, 0.0005f);
    const NS::Core::Vector3 top = NS::Game::Player::ImpactEffects::MissSparkHeading(0.0f, 0.8f, forward);
    EXPECT_NEAR(top.y, 0.9648f, 0.0005f);
    EXPECT_NEAR(top.z, 0.2631f, 0.0005f);
    const NS::Core::Vector3 middle = NS::Game::Player::ImpactEffects::MissSparkHeading(0.0f, 0.0f, forward);
    EXPECT_FLOAT_EQ(middle.Length(), 0.0f);
}

// 外れの核は当たりの絵の頭と次のフレームだけ写り、その次のフレームに薄れずに消える。火花は外した側へ流す
TEST(ImpactTimelineClock, MissCoreIsCutAfterTwoFramesAndSparksFollowTheFace)
{
    HitTimeline timeline;
    timeline.events = {{HitStopEvent{}, 1, 4, HitDirection::Any},
                       {HitEffectEvent{}, 1, 1, HitDirection::Any},
                       {TargetLaunchEvent{}, 5, 1, HitDirection::Any},
                       {ReboundEvent{}, 5, 1, HitDirection::Any}};
    const ScopedHitTimelineDirectory directory("MissCore");
    ScopedHitTimelineDirectory::SetBothTiers(timeline);
    NS::Obj::Scene scene;
    Player* player = PlaceClockScene(scene, 0.75f, 0.6f, 0.0f);
    ASSERT_NE(player, nullptr);
    ASSERT_EQ(RunHit(*player, *RockOf(scene), 1.0f, 8).size(), 8u);
    const ImpactRecord& impact = player->Resolver().LastImpact();
    ASSERT_EQ(impact.tier, HitTier::Wide);

    const NS::Game::Player::EffectLayerRecord* core = nullptr;
    const NS::Game::Player::EffectLayerRecord* sparks = nullptr;
    for (const NS::Game::Player::EffectLayerRecord& record : player->ImpactVisuals().Layers().Records())
    {
        if (record.name == "impact.core")
        {
            core = &record;
        }
        if (record.name == "impact.sparks")
        {
            sparks = &record;
        }
    }
    ASSERT_NE(core, nullptr);
    ASSERT_TRUE(core->endStep.has_value());
    EXPECT_EQ(core->endStep.value() - core->startStep, 2);
    EXPECT_FALSE(core->rootStopStep.has_value());

    ASSERT_NE(sparks, nullptr);
    ASSERT_TRUE(sparks->rotation.has_value());
    const NS::Core::Vector3 heading =
        NS::Core::Vector3::Transform(NS::Core::Vector3{0.0f, 1.0f, 0.0f}, sparks->rotation.value());
    const NS::Core::Vector3 expected =
        NS::Game::Player::ImpactEffects::MissSparkHeading(impact.faceU, impact.faceV, player->BodySlamDirection());
    ASSERT_GT(expected.Length(), 0.5f);
    EXPECT_NEAR(heading.Dot(expected), 1.0f, 1.0e-4f);
}

namespace
{
    // 高さ 720 画素の画面の上の pixels 画素を、カメラから見た at の奥行きでの世界の長さへ直す
    float MetersForPixels(const NS::Obj::CameraPose& pose, const NS::Core::Vector3& at, float pixels)
    {
        NS::Core::Vector3 forward = pose.target - pose.position;
        forward.Normalize();
        float depth = (at - pose.position).Dot(forward);
        // カメラより後ろの物は、奥行きの代わりにカメラとの距離で測る
        if (depth <= 0.0f)
        {
            depth = (at - pose.position).Length();
        }
        return pixels * 2.0f * depth * std::tan(pose.fovY.value * 0.5f) / 720.0f;
    }

    // 置物の当たりの球の半径。置物の当たりは球で作る
    float RockRadius(const MapObj& rock)
    {
        return static_cast<const NS::Obj::SphereCollision*>(rock.CollisionPart())->WorldSphere().radius;
    }
} // namespace

// 衝撃の震えは、事象の始まり (止めの明け) から長さの間だけ、自機と相手の描く所へ震えを渡す
// 経過はゲームのフレーム数で数え、振れ幅は画素の欄を毎フレームその物とカメラの距離で世界の長さへ直す
TEST(ImpactTimelineClock, TremorRunsFromTheReleaseThroughItsLength)
{
    ImpactTremorEvent tremor;
    tremor.amplitudePixels = 3.0f;
    tremor.reachFrames = 4;
    HitTimeline timeline;
    timeline.events = {{HitStopEvent{}, 1, 6, HitDirection::Any},
                       {TargetFreezeEvent{}, 1, 6, HitDirection::Any},
                       {TargetLaunchEvent{}, 7, 1, HitDirection::Any},
                       {ReboundEvent{}, 7, 1, HitDirection::Any},
                       {tremor, 7, 10, HitDirection::Any}};
    const ScopedHitTimelineDirectory directory("Tremor");
    ScopedHitTimelineDirectory::SetBothTiers(timeline);
    NS::Obj::Scene scene;
    Player* player = PlaceClockScene(scene, 0.0f, 0.6f, 0.0f);
    ASSERT_NE(player, nullptr);
    MapObj* rock = RockOf(scene);
    ASSERT_NE(rock, nullptr);

    player->RequestBodySlam(1.0f, NS::Core::Vector3{0.0f, 0.0f, 1.0f});
    int clock = -1;
    NS::Core::Vector3 otherContact{};
    NS::Core::Vector3 previousSelfRoot = player->Root().Position();
    for (int frame = 0; frame < 120 && clock < 20; ++frame)
    {
        const NS::Core::Vector3 selfRootBefore = player->Root().Position();
        player->Update(false);
        rock->Update();
        if (clock < 0 && player->Resolver().LastImpact().sequence == 0)
        {
            continue;
        }
        ++clock;
        SCOPED_TRACE(clock);
        previousSelfRoot = selfRootBefore;
        const NS::Gfx::TremorCB& self = player->ModelPart()->Tremor();
        const NS::Gfx::TremorCB& other = rock->ModelPart()->Tremor();
        if (clock < 7 || clock >= 17)
        {
            EXPECT_FLOAT_EQ(self.amplitude, 0.0f);
            EXPECT_FLOAT_EQ(other.amplitude, 0.0f);
            continue;
        }
        const std::optional<NS::Obj::CameraPose> pose = NS::Obj::CameraViewPose(*player);
        ASSERT_TRUE(pose.has_value());
        NS::Core::Vector3 forward = pose->target - pose->position;
        forward.Normalize();
        const NS::Core::Vector3 selfRoot = player->Root().Position();
        const NS::Core::Vector3 otherRoot = rock->Root().Position();
        for (const NS::Gfx::TremorCB* each : {&self, &other})
        {
            // 経過は始まりのフレームを 0 にしたゲームのフレーム数。1 か所は 長さ − 届くフレーム数 で止まる
            EXPECT_FLOAT_EQ(each->elapsedFrames, static_cast<float>(clock - 7));
            EXPECT_FLOAT_EQ(each->ringFrames, 6.0f);
            // 揺らす向きは画面の平面の中
            EXPECT_NEAR(each->right.Length(), 1.0f, 1.0e-5f);
            EXPECT_NEAR(each->up.Length(), 1.0f, 1.0e-5f);
            EXPECT_NEAR(each->right.Dot(forward), 0.0f, 1.0e-5f);
            EXPECT_NEAR(each->up.Dot(forward), 0.0f, 1.0e-5f);
            EXPECT_NEAR(each->right.Dot(each->up), 0.0f, 1.0e-5f);
        }
        // 一番遠い所 (体の差し渡し) へ届くフレーム数で遅れを決める
        EXPECT_NEAR(self.framesPerMeter, 4.0f / (2.0f * player->Collider().CapsuleRadius()), 1.0e-4f);
        EXPECT_NEAR(other.framesPerMeter, 4.0f / (2.0f * RockRadius(*rock)), 1.0e-4f);
        EXPECT_NEAR(self.amplitude, MetersForPixels(*pose, selfRoot, 3.0f), 1.0e-5f);
        EXPECT_NEAR(other.amplitude, MetersForPixels(*pose, otherRoot, 3.0f), 1.0e-5f);
        // 衝突点は根からのずれで渡し、体と一緒に動かす。始まりは自機の玉が相手の表面に触れた点
        if (clock == 7)
        {
            otherContact = other.contactOffset;
            EXPECT_GT(otherContact.Length(), 0.0f);
            const NS::Core::Vector3 surface = player->Resolver().LastImpact().surfacePoint;
            EXPECT_NEAR((previousSelfRoot + self.contactOffset - surface).Length(), 0.0f, 1.0e-4f);
        }
        else
        {
            EXPECT_TRUE(other.contactOffset == otherContact);
        }
    }
    EXPECT_GE(clock, 20);
}

// 止めの間の横揺れは、自機と相手を画面の横 (床に沿う向き) へ逆向きに揺らし、横揺れの長さの終わりで 0 にする
// 揺らすのは描く形だけで、相手の根は食い込んだ所に留まる
TEST(ImpactTimelineClock, BodyShakeSwingsBothBodiesOppositeAlongTheScreenSide)
{
    BodyShakeEvent shake;
    shake.amplitudePixels = 10.0f;
    HitTimeline timeline;
    timeline.events = {{HitStopEvent{}, 1, 6, HitDirection::Any},
                       {TargetFreezeEvent{}, 1, 6, HitDirection::Any},
                       {shake, 1, 6, HitDirection::Any},
                       {TargetLaunchEvent{}, 7, 1, HitDirection::Any},
                       {ReboundEvent{}, 7, 1, HitDirection::Any}};
    const ScopedHitTimelineDirectory directory("BodyShake");
    ScopedHitTimelineDirectory::SetBothTiers(timeline);
    NS::Obj::Scene scene;
    Player* player = PlaceClockScene(scene, 0.0f, 0.6f, 0.0f);
    ASSERT_NE(player, nullptr);
    MapObj* rock = RockOf(scene);
    ASSERT_NE(rock, nullptr);
    const NS::Obj::CameraManager* cameras = player->GetCameraManager();
    ASSERT_NE(cameras, nullptr);
    const NS::Core::Vector3 forward = cameras->ForwardHorizontal();

    player->RequestBodySlam(1.0f, NS::Core::Vector3{0.0f, 0.0f, 1.0f});
    int clock = -1;
    float previousSelf = 0.0f;
    NS::Core::Vector3 frozenRoot{};
    for (int frame = 0; frame < 90 && clock < 9; ++frame)
    {
        player->Update(false);
        rock->Update();
        if (clock < 0 && player->Resolver().LastImpact().sequence == 0)
        {
            continue;
        }
        ++clock;
        SCOPED_TRACE(clock);
        const NS::Core::Vector3 self = player->ModelPart()->DrawOffset();
        const NS::Core::Vector3 other = rock->ModelPart()->DrawOffset();
        if (clock >= 1 && clock <= 5)
        {
            // 画面の横の向きだけに、逆向きに揺れる
            EXPECT_GT(self.Length(), 0.0f);
            EXPECT_NEAR(self.Dot(forward), 0.0f, 1.0e-5f);
            EXPECT_NEAR(self.y, 0.0f, 1.0e-6f);
            EXPECT_LT(self.Dot(other), 0.0f);
            const float side = self.Dot(NS::Core::Vector3{forward.z, 0.0f, -forward.x});
            if (clock > 1)
            {
                EXPECT_LT(side * previousSelf, 0.0f);
            }
            previousSelf = side;
            if (clock == 1)
            {
                frozenRoot = rock->Root().Position();
            }
            else
            {
                EXPECT_TRUE(rock->Root().Position() == frozenRoot);
            }
        }
        if (clock >= 6)
        {
            EXPECT_FLOAT_EQ(self.Length(), 0.0f);
            EXPECT_FLOAT_EQ(other.Length(), 0.0f);
        }
    }
    EXPECT_GE(clock, 9);
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

namespace
{
    // 触れる前の 3 フレームから縮む形と、止め 2 フレームのタイムライン
    HitTimeline MakeBeforeContactTimeline()
    {
        ShapeEvent shrink;
        shrink.along.count = 1;
        shrink.along.keys[0] = NS::Obj::Curve::Key{0.0f, 1.0f};
        shrink.height.count = 1;
        shrink.height.keys[0] = NS::Obj::Curve::Key{0.0f, 0.5f};
        HitTimeline timeline;
        timeline.events = {{shrink, -3, 5, HitDirection::Any},
                           {HitStopEvent{}, 1, 2, HitDirection::Any},
                           {ReboundEvent{}, 3, 1, HitDirection::Any},
                           {TargetLaunchEvent{}, 3, 1, HitDirection::Any}};
        return timeline;
    }
} // namespace

// マイナスに置いた事象は、触れる前の予測のフレームで始まる。当たったら同じ時計を 0 から続ける
TEST(ImpactTimelineClock, NegativeEventsStartBeforeContact)
{
    const ScopedHitTimelineDirectory directory("BeforeContact");
    ScopedHitTimelineDirectory::SetBothTiers(MakeBeforeContactTimeline());
    NS::Obj::Scene scene;
    Player* player = PlaceClockScene(scene, 0.0f, 4.0f, 0.0f);
    ASSERT_NE(player, nullptr);
    MapObj* rock = RockOf(scene);
    player->RequestBodySlam(1.0f, NS::Core::Vector3{0.0f, 0.0f, 1.0f});
    std::vector<bool> shaping;
    int detectedAt = -1;
    for (int frame = 0; frame < 60 && detectedAt < 0; ++frame)
    {
        player->Update(false);
        rock->Update();
        shaping.push_back(player->Resolver().IsShapeAnimating());
        if (player->Resolver().LastImpact().sequence != 0)
        {
            detectedAt = frame;
        }
    }
    ASSERT_GE(detectedAt, 4) << "当たるまでに縮みの 3 フレームが入る間合いが要る";
    for (int frame = 0; frame <= detectedAt; ++frame)
    {
        SCOPED_TRACE(frame);
        EXPECT_EQ(shaping[static_cast<std::size_t>(frame)], frame >= detectedAt - 3);
    }
    EXPECT_FLOAT_EQ(player->Resolver().ShapeFactors().y, 0.5f);
    // 形の事象は -3 から 5 フレームなので、検知の次のフレーム (1) が最後
    player->Update(false);
    rock->Update();
    EXPECT_TRUE(player->Resolver().IsShapeAnimating());
    player->Update(false);
    rock->Update();
    EXPECT_FALSE(player->Resolver().IsShapeAnimating());
}

// 予測した相手が線から外れたら、触れる前に始めた事象を止める
TEST(ImpactTimelineClock, AMissedPredictionStopsTheEarlyEvents)
{
    const ScopedHitTimelineDirectory directory("BeforeContactMiss");
    ScopedHitTimelineDirectory::SetBothTiers(MakeBeforeContactTimeline());
    NS::Obj::Scene scene;
    Player* player = PlaceClockScene(scene, 0.0f, 4.0f, 0.0f);
    ASSERT_NE(player, nullptr);
    MapObj* rock = RockOf(scene);
    player->RequestBodySlam(1.0f, NS::Core::Vector3{0.0f, 0.0f, 1.0f});
    bool started = false;
    for (int frame = 0; frame < 60 && !started; ++frame)
    {
        player->Update(false);
        rock->Update();
        started = player->Resolver().IsShapeAnimating();
    }
    ASSERT_TRUE(started);
    ASSERT_EQ(player->Resolver().LastImpact().sequence, 0u);
    // 相手を線の横へ退ける
    rock->Root().SetPosition(rock->Root().Position() + NS::Core::Vector3{10.0f, 0.0f, 0.0f});
    player->Update(false);
    rock->Update();
    EXPECT_FALSE(player->Resolver().IsShapeAnimating());
    EXPECT_FLOAT_EQ(player->Resolver().ShapeFactors().y, 1.0f);
    EXPECT_EQ(player->Resolver().LastImpact().sequence, 0u);
}

namespace
{
    // 自機以外の段で回った歩を数える
    class CountingTicker final : public NS::Obj::ITickable
    {
    public:
        void OnTick() override { ++ticks; }
        int ticks = 0;
    };

    // 縮み [-4, 4)・自機以外の止め [-2, 0)・止め [1, 2]・明け 3 に反動と相手を飛ばす
    HitTimeline MakeOthersStopTimeline()
    {
        ShapeEvent shrink;
        shrink.along.count = 1;
        shrink.along.keys[0] = NS::Obj::Curve::Key{0.0f, 0.9f};
        HitTimeline timeline;
        timeline.events = {{shrink, -4, 8, HitDirection::Any},
                           {OthersStopEvent{}, -2, 2, HitDirection::Any},
                           {HitStopEvent{}, 1, 2, HitDirection::Any},
                           {ReboundEvent{}, 3, 1, HitDirection::Any},
                           {TargetLaunchEvent{}, 3, 1, HitDirection::Any}};
        return timeline;
    }

    // シーンを 1 歩ずつ回して突進を当て、各歩の「自機以外が回ったか」と自機の水平の進みを並べる。最後の要素が検知の歩
    struct OthersStep
    {
        bool othersRan = false;
        float playerAdvance = 0.0f;
    };

    std::vector<OthersStep> RunUntilDetection(NS::Obj::Scene& scene, Player& player, CountingTicker& others)
    {
        std::vector<OthersStep> steps;
        player.RequestBodySlam(1.0f, NS::Core::Vector3{0.0f, 0.0f, 1.0f});
        for (int frame = 0; frame < 60 && player.Resolver().LastImpact().sequence == 0; ++frame)
        {
            const int before = others.ticks;
            const float z = player.Root().Position().z;
            scene.OnUpdate();
            steps.push_back(
                OthersStep{.othersRan = others.ticks != before, .playerAdvance = player.Root().Position().z - z});
        }
        return steps;
    }
} // namespace

// 真ん中で触れる 2 フレーム前から触れるまで、自機以外の世界が進まず、自機は突進の速さのまま進む
TEST(ImpactTimelineClock, OthersStopHoldsTheWorldButNotThePlayerBeforeContact)
{
    const ScopedHitTimelineDirectory directory("OthersStop");
    ScopedHitTimelineDirectory::SetBothTiers(MakeOthersStopTimeline());
    NS::Obj::Scene scene;
    Player* player = PlaceClockScene(scene, 0.0f, 6.0f, 0.0f);
    ASSERT_NE(player, nullptr);
    CountingTicker others;
    scene.Objects().AddTicker(&others, NS::Obj::UpdatePhase::Triggers);
    for (int frame = 0; frame < 30; ++frame)
    {
        scene.OnUpdate();
    }
    const std::vector<OthersStep> steps = RunUntilDetection(scene, *player, others);
    scene.Objects().RemoveTicker(&others);
    ASSERT_GE(steps.size(), 5u);
    ASSERT_NE(player->Resolver().LastImpact().sequence, 0u);
    const std::size_t detect = steps.size() - 1;
    const float slamStep = 20.0f * NS::Platform::FrameTimer::FixedDelta();
    for (std::size_t i = 0; i <= detect; ++i)
    {
        SCOPED_TRACE(static_cast<int>(i) - static_cast<int>(detect));
        const bool held = i + 2 == detect || i + 1 == detect;
        EXPECT_EQ(steps[i].othersRan, !held);
        if (held)
        {
            EXPECT_NEAR(steps[i].playerAdvance, slamStep, slamStep * 0.05f);
        }
    }
    EXPECT_FALSE(scene.IsHoldingOthers());
}

// 外れの予測では止めない。外れのタイムラインに触れる前の事象を置かなければ、線の先が外れの相手でも世界は回る
TEST(ImpactTimelineClock, OthersStopDoesNotRunForAMissPrediction)
{
    const ScopedHitTimelineDirectory directory("OthersStopMiss");
    HitTimelineLibrary::Get().Set("center", MakeOthersStopTimeline());
    HitTimelineLibrary::Get().Set("miss", MakeLegacyHitTimeline(4));
    NS::Obj::Scene scene;
    Player* player = PlaceClockScene(scene, 0.75f, 6.0f, 0.0f);
    ASSERT_NE(player, nullptr);
    CountingTicker others;
    scene.Objects().AddTicker(&others, NS::Obj::UpdatePhase::Triggers);
    for (int frame = 0; frame < 30; ++frame)
    {
        scene.OnUpdate();
    }
    const std::vector<OthersStep> steps = RunUntilDetection(scene, *player, others);
    scene.Objects().RemoveTicker(&others);
    ASSERT_NE(player->Resolver().LastImpact().sequence, 0u);
    ASSERT_EQ(player->Resolver().LastImpact().tier, HitTier::Wide);
    for (const OthersStep& step : steps)
    {
        EXPECT_TRUE(step.othersRan);
    }
}

// 予測した相手が線から外れたら、自機以外の止めも解く
TEST(ImpactTimelineClock, AMissedPredictionReleasesTheOthers)
{
    const ScopedHitTimelineDirectory directory("OthersStopDrop");
    ScopedHitTimelineDirectory::SetBothTiers(MakeOthersStopTimeline());
    NS::Obj::Scene scene;
    Player* player = PlaceClockScene(scene, 0.0f, 6.0f, 0.0f);
    ASSERT_NE(player, nullptr);
    MapObj* rock = RockOf(scene);
    for (int frame = 0; frame < 30; ++frame)
    {
        scene.OnUpdate();
    }
    player->RequestBodySlam(1.0f, NS::Core::Vector3{0.0f, 0.0f, 1.0f});
    for (int frame = 0; frame < 60 && !scene.IsHoldingOthers(); ++frame)
    {
        scene.OnUpdate();
    }
    ASSERT_TRUE(scene.IsHoldingOthers());
    ASSERT_EQ(player->Resolver().LastImpact().sequence, 0u);
    rock->Root().SetPosition(rock->Root().Position() + NS::Core::Vector3{10.0f, 0.0f, 0.0f});
    scene.OnUpdate();
    EXPECT_FALSE(scene.IsHoldingOthers());
    EXPECT_FALSE(player->Resolver().IsShapeAnimating());
}

// 沈む揺れは止めの頭に始まり、底で反動の頭までこらえ、反動の頭から跳ね返る。底の深さは威力で頭打ちに増える
TEST(ImpactTimelineClock, SinkShakeHoldsTheBottomUntilTheReboundFrame)
{
    CameraSinkEvent sink;
    sink.maxPixels = 9.0f;
    sink.powerBase = 1.0f;
    HitTimeline timeline;
    timeline.events = {{HitStopEvent{}, 1, 12, HitDirection::Any},
                       {sink, 1, 33, HitDirection::Any},
                       {ReboundEvent{}, 13, 1, HitDirection::Any},
                       {TargetLaunchEvent{}, 13, 1, HitDirection::Any}};
    const ScopedHitTimelineDirectory directory("SinkShake");
    ScopedHitTimelineDirectory::SetBothTiers(timeline);
    NS::Obj::Scene scene;
    Player* player = PlaceClockScene(scene, 0.0f, 4.0f, 0.0f);
    ASSERT_NE(player, nullptr);
    MapObj* rock = RockOf(scene);
    player->RequestBodySlam(1.0f, NS::Core::Vector3{0.0f, 0.0f, 1.0f});
    std::vector<float> pixels;
    bool detected = false;
    for (int frame = 0; frame < 120 && pixels.size() < 20; ++frame)
    {
        player->Update(false);
        rock->Update();
        // カメラの効果はカメラの段で 1 フレーム進む
        scene.Objects().ExecutePhase(NS::Obj::UpdatePhase::Camera);
        detected = detected || player->Resolver().LastImpact().sequence != 0;
        if (!detected)
        {
            continue;
        }
        float now = 0.0f;
        if (const NS::Obj::CameraManager* cameras = player->GetCameraManager())
        {
            if (const NS::Obj::CameraSinkModifier* modifier = cameras->FindModifier<NS::Obj::CameraSinkModifier>())
            {
                now = modifier->Pixels();
            }
        }
        pixels.push_back(now);
    }
    ASSERT_EQ(pixels.size(), 20u);
    const float power = player->Resolver().LastImpact().power;
    const float bottom = -9.0f * (1.0f - std::exp(-power / 1.0f));
    // 0 が検知、1 が止めの頭
    EXPECT_FLOAT_EQ(pixels[0], 0.0f);
    EXPECT_LT(pixels[1], 0.0f);
    EXPECT_NEAR(pixels[2], bottom, 1.0e-4f);
    for (std::size_t clock = 9; clock <= 13; ++clock)
    {
        SCOPED_TRACE(clock);
        EXPECT_NEAR(pixels[clock], bottom, 1.0e-4f);
    }
    EXPECT_GT(pixels[14], bottom + 0.1f);
}

// impact-feel-pass 3 節「明けの伸び上がりと反動の順番」: 真ん中は明けのフレームに相手が飛び、自機は止まったまま
// 突進の向きへ伸び、伸びきったフレームに反動が始まる。外れは明けのフレームに反動が始まる (出荷のファイルで見る)
TEST(ImpactTimelineClock, ShippedCenterStretchesBeforeTheReboundAndMissReboundsOnRelease)
{
    for (const float rockX : {0.0f, 0.75f})
    {
        SCOPED_TRACE(rockX);
        NS::Obj::Scene scene;
        Player* player = PlaceClockScene(scene, rockX, 4.0f, 0.0f);
        ASSERT_NE(player, nullptr);
        MapObj* rock = RockOf(scene);
        const std::vector<ClockFrame> trace = RunHit(*player, *rock, 1.0f, 24);
        ASSERT_EQ(trace.size(), 24u);
        int released = -1;
        int launched = -1;
        int rebounded = -1;
        for (std::size_t clock = 0; clock < trace.size(); ++clock)
        {
            const int now = static_cast<int>(clock);
            if (released < 0 && trace[clock].released)
            {
                released = now;
            }
            if (launched < 0 && trace[clock].rockFlying)
            {
                launched = now;
            }
            if (rebounded < 0 && trace[clock].rebounding)
            {
                rebounded = now;
            }
        }
        ASSERT_GT(released, 0);
        EXPECT_EQ(launched, released);
        if (rockX == 0.0f)
        {
            ASSERT_EQ(player->Resolver().LastImpact().tier, HitTier::Center);
            // 伸びの間は自機が止まったまま、突進の向き (z) へ伸びて細くなる
            EXPECT_EQ(rebounded, released + 2);
            for (int clock = released; clock < rebounded; ++clock)
            {
                SCOPED_TRACE(clock);
                EXPECT_FALSE(trace[static_cast<std::size_t>(clock)].canMoveBody);
                EXPECT_GT(trace[static_cast<std::size_t>(clock + 1)].shape.z,
                          trace[static_cast<std::size_t>(clock)].shape.z);
            }
            EXPECT_NEAR(trace[static_cast<std::size_t>(rebounded)].shape.z, 1.25f, 1.0e-4f);
            EXPECT_LT(trace[static_cast<std::size_t>(rebounded)].shape.y, 1.0f);
            EXPECT_TRUE(trace[static_cast<std::size_t>(rebounded)].canMoveBody);
        }
        else
        {
            ASSERT_EQ(player->Resolver().LastImpact().tier, HitTier::Wide);
            EXPECT_EQ(rebounded, released);
        }
    }
}

// 外れの向きの重みは、面の上の位置の角度で隣り合う 2 つの向きを直線に混ぜる。足すと 1 で、境目で急に変わらない
TEST(ImpactTimelineClock, DirectionWeightsBlendTheTwoNeighborsByAngle)
{
    const HitDirection directions[] = {HitDirection::Right, HitDirection::Left, HitDirection::Up, HitDirection::Down};
    EXPECT_FLOAT_EQ(HitDirectionWeight(HitDirection::Right, 1.0f, 0.0f), 1.0f);
    EXPECT_FLOAT_EQ(HitDirectionWeight(HitDirection::Up, 1.0f, 0.0f), 0.0f);
    EXPECT_NEAR(HitDirectionWeight(HitDirection::Right, 0.5f, 0.5f), 0.5f, 0.0001f);
    EXPECT_NEAR(HitDirectionWeight(HitDirection::Up, 0.5f, 0.5f), 0.5f, 0.0001f);
    EXPECT_NEAR(HitDirectionWeight(HitDirection::Left, -0.3f, -0.3f), 0.5f, 0.0001f);
    EXPECT_NEAR(HitDirectionWeight(HitDirection::Down, -0.3f, -0.3f), 0.5f, 0.0001f);
    EXPECT_FLOAT_EQ(HitDirectionWeight(HitDirection::Down, 0.0f, -0.4f), 1.0f);
    EXPECT_FLOAT_EQ(HitDirectionWeight(HitDirection::Any, 0.3f, -0.4f), 1.0f);
    // 真ん中は向きが決まらないので、外れの向きの決まり (HitDirectionOf) の向きだけ
    EXPECT_FLOAT_EQ(HitDirectionWeight(HitDirectionOf(0.0f, 0.0f), 0.0f, 0.0f), 1.0f);
    // 円を回って、重みの和が 1 で、隣の角度との差が小さい
    float previous[4] = {};
    for (int step = 0; step <= 720; ++step)
    {
        const float angle = static_cast<float>(step) * 0.5f * NS::Core::k_Pi / 180.0f;
        const float u = 0.7f * std::cos(angle);
        const float v = 0.7f * std::sin(angle);
        float sum = 0.0f;
        for (int i = 0; i < 4; ++i)
        {
            const float weight = HitDirectionWeight(directions[i], u, v);
            EXPECT_GE(weight, 0.0f);
            sum += weight;
            if (step > 0)
            {
                EXPECT_LT(std::abs(weight - previous[i]), 0.02f) << "角度 " << step * 0.5f;
            }
            previous[i] = weight;
        }
        EXPECT_NEAR(sum, 1.0f, 0.0001f);
    }
}

// 向きの付いた振動は、面の上の位置の重みで混ぜて鳴らす。他の種類は今までどおり 1 つの向きだけ
TEST(ImpactTimelineClock, DirectedPadVibrationsAreBlendedByTheFacePosition)
{
    // 左のモーターを一定の値で鳴らす振動
    const auto constantLeft = [](float value) {
        PadVibrationEvent pad;
        pad.left.count = 1;
        pad.left.keys[0] = NS::Obj::Curve::Key{0.0f, value};
        return pad;
    };
    HitTimeline timeline;
    timeline.events = {{HitStopEvent{}, 1, 4, HitDirection::Any},
                       {constantLeft(1.0f), 1, 8, HitDirection::Right},
                       {constantLeft(0.6f), 1, 8, HitDirection::Left},
                       {constantLeft(0.3f), 1, 8, HitDirection::Up},
                       {constantLeft(0.1f), 1, 8, HitDirection::Down},
                       {ReboundEvent{}, 5, 1, HitDirection::Any}};
    const ScopedHitTimelineDirectory directory("BlendedPad");
    ScopedHitTimelineDirectory::SetBothTiers(timeline);
    NS::Obj::Scene scene;
    // 横と上へずらした相手。面の上の位置が斜めになる
    Player* player = PlaceClockScene(scene, 0.6f, 0.6f, 0.0f);
    ASSERT_NE(player, nullptr);
    MapObj* rock = RockOf(scene);
    ASSERT_NE(rock, nullptr);
    rock->Root().SetPosition(NS::Core::Vector3{0.5f, 0.85f, 0.6f});
    const std::vector<ClockFrame> trace = RunHit(*player, *rock, 1.0f, 4);
    ASSERT_EQ(trace.size(), 4u);
    const ImpactRecord& record = player->Resolver().LastImpact();
    const float u = record.faceU;
    const float v = record.faceV;
    // 斜めに当たり、2 つの向きに重みが分かれている
    ASSERT_GT(std::abs(u), 0.05f);
    ASSERT_GT(std::abs(v), 0.05f);
    const float expected =
        HitDirectionWeight(HitDirection::Right, u, v) * 1.0f + HitDirectionWeight(HitDirection::Left, u, v) * 0.6f +
        HitDirectionWeight(HitDirection::Up, u, v) * 0.3f + HitDirectionWeight(HitDirection::Down, u, v) * 0.1f;
    EXPECT_NEAR(trace[1].padLeft, expected, 0.0001f);
    EXPECT_NEAR(trace[3].padLeft, expected, 0.0001f);
    EXPECT_NEAR(record.padStart.left, expected, 0.0001f);
}
