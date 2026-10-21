#include "Game/Level/HitTimeline.h"
#include "Game/Level/ImpactResolver.h"
#include "Game/Level/LevelMessages.h"
#include "Game/Level/PropActor.h"
#include "Game/Player/PlayerParams.h"
#include "Game/PlayerActor.h"
#include "NSlib/Core/OBB.h"
#include "NSlib/Core/Types.h"
#include "NSlib/CoreObject/ClassObjs/CameraModifier.h"
#include "NSlib/CoreObject/ClassObjs/EffectKeeper.h"
#include "NSlib/CoreObject/ClassObjs/Transform.h"
#include "NSlib/CoreObject/IUse/IUseSceneSystem.h"
#include "NSlib/CoreObject/ObjectJson.h"
#include "NSlib/CoreObject/Reflection/ReflectionJson.h"
#include "NSlib/CoreObject/Scene/Scene.h"
#include "NSlib/CoreObject/System/CameraManager.h"
#include "NSlib/CoreObject/System/HitScreenDirector.h"
#include "NSlib/CoreObject/System/PadRumbleDirector.h"
#include "NSlib/CoreObject/System/SceneSystem.h"
#include "NSlib/CoreObject/UpdatePhase.h"
#include "Tests/TestHitTimelines.h"
#include "Tests/TestViewCamera.h"

#include <gtest/gtest.h>

#include <cmath>
#include <string_view>
#include <vector>

// 当たりのタイムラインの型と、外れ・貫通・沈みの返りを縛る

namespace
{
    using namespace GL::Level;

    // HitScreenDirector と PadRumbleDirector
    // は描く支度の段で進む。自機と相手を手で回す試しは、回した後にこれを呼んで同じ順にする
    void TickHitDirectors(PlayerActor& player)
    {
        if (NS::HitScreenDirector* screen = NS::FindSceneSystem<NS::HitScreenDirector>(&player))
        {
            screen->OnTick();
        }
        if (NS::PadRumbleDirector* pad = NS::FindSceneSystem<NS::PadRumbleDirector>(&player))
        {
            pad->OnTick();
        }
    }

    PlayerActor* PlaceClockScene(NS::Scene& scene, float rockX, float rockZ, float mass)
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
        NS::SetObjectPosition(rock, NS::Vector3{rockX, 0.5f, rockZ});
        if (mass > 0.0f)
        {
            NS::ObjectJsonClassObjs(rock)["Params"]["質量"] = mass;
        }
        NS::SceneJsonObjects(doc).push_back(std::move(rock));
        scene.ResetFromJson(doc);
        NS::OBB floor{};
        floor.center = NS::Vector3{0.0f, -0.5f, 0.0f};
        floor.halfExtentX = 100.0f;
        floor.halfExtentY = 0.5f;
        floor.halfExtentZ = 100.0f;
        scene.Physics().AddBox(floor, NS::Phys::ObjectLayers::Terrain);
        PlaceViewCamera(scene, NS::Vector3{}, NS::Vector3{0.0f, 0.0f, 1.0f});
        return NS::Cast<PlayerActor>(scene.Objects().FindByObjectId(1));
    }

    PropActor* RockOf(NS::Scene& scene)
    {
        return NS::Cast<PropActor>(scene.Objects().FindByObjectId(2));
    }

    // 検知のフレームを 0 にした 1 フレームの姿
    struct ClockFrame
    {
        bool rebounding = false;
        int flightLayers = 0; // 出した反動の尾の数
    };

    int CountLayers(PlayerActor& player, std::string_view name)
    {
        int count = 0;
        for (const NS::EffectRecord& record : player.GetEffectKeeper()->Records())
        {
            if (record.name == name)
            {
                ++count;
            }
        }
        return count;
    }

    // 突進を出して当たりまで回し、検知のフレームから frames フレームぶんの姿を並べる。当たらなければ空
    std::vector<ClockFrame> RunHit(PlayerActor& player, PropActor& rock, float charge01, int frames)
    {
        std::vector<ClockFrame> trace;
        player.RequestBodySlam(charge01, NS::Vector3{0.0f, 0.0f, 1.0f});
        bool detected = false;
        for (int frame = 0; frame < 60 + frames && static_cast<int>(trace.size()) < frames; ++frame)
        {
            player.Update(false);
            rock.Update();
            TickHitDirectors(player);
            if (!detected && player.Resolver().Answer().lastImpact.sequence == 0)
            {
                continue;
            }
            detected = true;
            ClockFrame now;
            now.rebounding = player.IsRebounding();
            now.flightLayers = CountLayers(player, "rebound.trail");
            trace.push_back(now);
        }
        return trace;
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

// 外れは弾かれた手応えを出さない。反動しても反動の尾と弾かれ線を出さない
TEST(ImpactTimelineClock, AMissShowsNoReboundTrailOrRecoilLines)
{
    HitTimeline timeline;
    timeline.events = {
        {HitStopEvent{}, 1, 2, HitDirection::Any},
        {HitEffectEvent{}, 1, 1, HitDirection::Any},
        {TargetLaunchEvent{}, 3, 1, HitDirection::Any},
        {ReboundEvent{}, 3, 1, HitDirection::Any},
        {FlightEffectEvent{}, 4, 1, HitDirection::Any},
    };
    const ScopedHitTimelineDirectory directory("MissFlight");
    ScopedHitTimelineDirectory::SetBothTiers(timeline);
    NS::Scene scene;
    PlayerActor* player = PlaceClockScene(scene, 0.75f, 0.6f, 0.0f);
    ASSERT_NE(player, nullptr);
    const std::vector<ClockFrame> trace = RunHit(*player, *RockOf(scene), 1.0f, 20);
    ASSERT_EQ(trace.size(), 20u);
    ASSERT_EQ(player->Resolver().Answer().lastImpact.tier, HitTier::Wide);
    bool rebounded = false;
    for (const ClockFrame& frame : trace)
    {
        rebounded = rebounded || frame.rebounding;
        EXPECT_EQ(frame.flightLayers, 0);
    }
    EXPECT_TRUE(rebounded);
    EXPECT_EQ(CountLayers(*player, "impact.recoil"), 0);
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
        NS::Scene scene;
        PlayerActor* player = PlaceClockScene(scene, 0.0f, 0.6f, 2.0f);
        ASSERT_NE(player, nullptr);
        ASSERT_FALSE(RunHit(*player, *RockOf(scene), 1.0f, 1).empty());
        shakes.push_back(player->Resolver().Answer().lastImpact.cameraShake);
    }
    ASSERT_EQ(shakes.size(), 3u);
    EXPECT_GT(shakes[0], 0.0f);
    EXPECT_NEAR(shakes[1], shakes[0] * 2.0f, 1e-6f);
    EXPECT_NEAR(shakes[2], shakes[1], 1e-6f);
}

namespace
{
    // 壊れる動きを持つと答え、送られた止めの上書きを控える置物。今の置物は壊れないと答える
    class BreakableRock final : public PropActor
    {
    public:
        int freezeSteps = -1;

    protected:
        bool ReceiveMsg(const NS::Message& msg, NS::HitSensor* sender, NS::HitSensor* receiver) override
        {
            if (const MsgTackleHit* hit = NS::MsgCast<MsgTackleHit>(msg))
            {
                freezeSteps = hit->Desc().stopOverride.value_or(-1);
            }
            const bool answered = PropActor::ReceiveMsg(msg, sender, receiver);
            if (const MsgAskTackleTarget* ask = NS::MsgCast<MsgAskTackleTarget>(msg))
            {
                ask->Answer().breakable = true;
            }
            return answered;
        }
    };
} // namespace

// 貫通の当たりは、置物の凍結も貫通の止めと同じフレーム数にする。タイムラインの凍結の行の長さは使わない
TEST(ImpactTimelineClock, ABreakFreezesTheTargetForTheBreakStop)
{
    const ScopedHitTimelineDirectory directory("BreakFreeze");
    ScopedHitTimelineDirectory::SetBothTiers(MakeSplitReleaseTimeline());
    NS::Scene scene;
    // 置いた岩は線の外へ退け、壊れる岩で見る
    PlayerActor* player = PlaceClockScene(scene, 50.0f, 0.6f, 0.0f);
    ASSERT_NE(player, nullptr);
    ASSERT_EQ(NS::ApplyJsonFields(player->Params(), nlohmann::json{{"破壊を許可", true}, {"貫通の止め秒", 2.0f / 60.0f}}),
              0u);
    BreakableRock* rock = scene.SpawnObject<BreakableRock>("BreakableRock");
    ASSERT_NE(rock, nullptr);
    ASSERT_EQ(NS::ApplyJsonFields(rock->Params(), nlohmann::json{{"耐久", 0.0f}}), 0u);
    rock->Root().SetPosition(NS::Vector3{0.0f, 0.5f, 0.6f});
    const std::vector<ClockFrame> trace = RunHit(*player, *rock, 1.0f, 10);
    ASSERT_FALSE(trace.empty());
    const ImpactRecord& impact = player->Resolver().Answer().lastImpact;
    ASSERT_TRUE(impact.broke);
    ASSERT_NE(impact.hitStopSteps, 5) << "凍結の行の長さ (5) と違う貫通の止めで見る";
    EXPECT_EQ(rock->freezeSteps, impact.hitStopSteps);
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
    NS::Scene scene;
    PlayerActor* player = PlaceClockScene(scene, 0.0f, 4.0f, 0.0f);
    ASSERT_NE(player, nullptr);
    PropActor* rock = RockOf(scene);
    player->RequestBodySlam(1.0f, NS::Vector3{0.0f, 0.0f, 1.0f});
    std::vector<float> pixels;
    bool detected = false;
    for (int frame = 0; frame < 120 && pixels.size() < 20; ++frame)
    {
        player->Update(false);
        rock->Update();
        TickHitDirectors(*player);
        // カメラの効果はカメラの段で 1 フレーム進む
        scene.ExecutePhase(NS::UpdatePhase::Camera);
        detected = detected || player->Resolver().Answer().lastImpact.sequence != 0;
        if (!detected)
        {
            continue;
        }
        float now = 0.0f;
        if (const NS::CameraManager* cameras = player->GetCameraManager())
        {
            if (const NS::CameraSinkModifier* modifier = cameras->FindModifier<NS::CameraSinkModifier>())
            {
                now = modifier->Pixels();
            }
        }
        pixels.push_back(now);
    }
    ASSERT_EQ(pixels.size(), 20u);
    const float power = player->Resolver().Answer().lastImpact.power;
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
        const float angle = static_cast<float>(step) * 0.5f * NS::k_Pi / 180.0f;
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
