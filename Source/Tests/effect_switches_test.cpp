#include "Game/Level/EffectSwitches.h"
#include "Game/Level/HitTimeline.h"
#include "Game/Level/ImpactResolver.h"
#include "Game/Level/MapObj.h"
#include "Game/Player.h"
#include "Game/Player/ImpactEffects.h"
#include "NSlib/Core/OBB.h"
#include "NSlib/Object/IUse/IUseSceneObj.h"
#include "NSlib/Object/ObjectJson.h"
#include "NSlib/Object/Scene/HitScreenDirector.h"
#include "NSlib/Object/Scene/PadRumbleDirector.h"
#include "NSlib/Object/Scene/Scene.h"
#include "NSlib/Object/SubObjects/TransformSubObject.h"
#include "Tests/TestHitTimelines.h"
#include "Tests/TestViewCamera.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

// 切る演出の名前の一覧は、事象を起こす前と層を出す前に見られる

namespace
{
    using namespace GL::Level;

    // EffectSwitches はプロセスに 1 つなので、試しの前後で全部入りへ戻す
    class EffectSwitchesTest : public ::testing::Test
    {
    protected:
        void SetUp() override { EffectSwitches::Get().TurnOnAll(); }
        void TearDown() override { EffectSwitches::Get().TurnOnAll(); }
    };

    Player* PlaceHitScene(NS::Obj::Scene& scene)
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
        NS::Obj::SetObjectPosition(rock, NS::Vector3{0.0f, 0.5f, 0.6f});
        NS::Obj::SceneJsonObjects(doc).push_back(std::move(rock));
        scene.LoadJson(doc);
        NS::OBB floor{};
        floor.center = NS::Vector3{0.0f, -0.5f, 0.0f};
        floor.halfExtentX = 100.0f;
        floor.halfExtentY = 0.5f;
        floor.halfExtentZ = 100.0f;
        scene.Physics().AddBox(floor, NS::Phys::ObjectLayers::Terrain);
        PlaceViewCamera(scene, NS::Vector3{}, NS::Vector3{0.0f, 0.0f, 1.0f});
        return NS::Obj::Cast<Player>(scene.Objects().FindByObjectId(1));
    }

    HitTimeline MakeFlashTimeline()
    {
        FlashEvent flash;
        flash.alpha = 0.5f;
        HitTimeline timeline;
        timeline.events = {
            {HitStopEvent{}, 1, 3, HitDirection::Any},
            {TargetFreezeEvent{}, 1, 3, HitDirection::Any},
            {flash, 1, 4, HitDirection::Any},
            {HitEffectEvent{}, 1, 1, HitDirection::Any},
            {TargetLaunchEvent{}, 4, 1, HitDirection::Any},
            {ReboundEvent{}, 4, 1, HitDirection::Any},
        };
        return timeline;
    }

    struct HitRun
    {
        bool hit = false;
        int mostFlash = 0; // 当たりの後の白の残りの一番大きい値
    };

    HitRun RunHit(NS::Obj::Scene& scene, Player& player)
    {
        MapObj* rock = NS::Obj::Cast<MapObj>(scene.Objects().FindByObjectId(2));
        NS::Obj::HitScreenDirector* screen = NS::Obj::FindSceneObj<NS::Obj::HitScreenDirector>(player);
        NS::Obj::PadRumbleDirector* pad = NS::Obj::FindSceneObj<NS::Obj::PadRumbleDirector>(player);
        HitRun run;
        if (rock == nullptr || screen == nullptr || pad == nullptr)
        {
            return run;
        }
        player.RequestBodySlam(1.0f, NS::Vector3{0.0f, 0.0f, 1.0f}, 0.0f, 0.0f);
        int afterHit = 0;
        for (int frame = 0; frame < 70 && afterHit < 10; ++frame)
        {
            player.Update(false);
            rock->Update();
            screen->OnTick();
            pad->OnTick();
            if (player.Resolver().LastImpact().sequence == 0)
            {
                continue;
            }
            run.hit = true;
            run.mostFlash = std::max(run.mostFlash, screen->FlashFramesRemaining());
            ++afterHit;
        }
        return run;
    }

    int CountLayers(Player& player, std::string_view name)
    {
        int count = 0;
        for (const GL::Player::EffectLayerRecord& record : player.ImpactVisuals().Layers().Records())
        {
            if (record.name == name)
            {
                ++count;
            }
        }
        return count;
    }
} // namespace

TEST_F(EffectSwitchesTest, TurningOffFlashSkipsTheFlashEvent)
{
    const ScopedHitTimelineDirectory directory("EffectSwitchesFlash");
    ScopedHitTimelineDirectory::SetBothTiers(MakeFlashTimeline());
    {
        NS::Obj::Scene scene;
        Player* player = PlaceHitScene(scene);
        ASSERT_NE(player, nullptr);
        EXPECT_TRUE(EffectSwitches::Get().TurnOff({"Flash"}).empty());
        EXPECT_TRUE(EffectSwitches::Get().IsOff("Flash"));
        const HitRun run = RunHit(scene, *player);
        ASSERT_TRUE(run.hit);
        EXPECT_EQ(run.mostFlash, 0);
    }
    EffectSwitches::Get().TurnOn("Flash");
    {
        NS::Obj::Scene scene;
        Player* player = PlaceHitScene(scene);
        ASSERT_NE(player, nullptr);
        const HitRun run = RunHit(scene, *player);
        ASSERT_TRUE(run.hit);
        EXPECT_EQ(run.mostFlash, 4);
    }
}

TEST_F(EffectSwitchesTest, TurningOffALayerKeepsPlayFromShowingIt)
{
    const ScopedHitTimelineDirectory directory("EffectSwitchesLayer");
    ScopedHitTimelineDirectory::SetBothTiers(MakeFlashTimeline());
    {
        NS::Obj::Scene scene;
        Player* player = PlaceHitScene(scene);
        ASSERT_NE(player, nullptr);
        ASSERT_TRUE(RunHit(scene, *player).hit);
        ASSERT_GT(CountLayers(*player, "impact.sparks"), 0);
    }
    NS::Obj::Scene scene;
    Player* player = PlaceHitScene(scene);
    ASSERT_NE(player, nullptr);
    EXPECT_TRUE(EffectSwitches::Get().TurnOff({"impact.sparks"}).empty());
    ASSERT_TRUE(RunHit(scene, *player).hit);
    EXPECT_EQ(CountLayers(*player, "impact.sparks"), 0);
    EXPECT_GT(CountLayers(*player, "impact.core"), 0);
}

TEST_F(EffectSwitchesTest, UnknownNamesAreRejectedAndReturned)
{
    NS::Obj::Scene scene;
    ASSERT_NE(PlaceHitScene(scene), nullptr);
    const std::vector<std::string> rejected = EffectSwitches::Get().TurnOff({"Flash", "impact.sparks", "NoSuchEffect"});
    EXPECT_EQ(rejected, std::vector<std::string>{"NoSuchEffect"});
    EXPECT_TRUE(EffectSwitches::Get().IsOff("Flash"));
    EXPECT_TRUE(EffectSwitches::Get().IsOff("impact.sparks"));
    EXPECT_FALSE(EffectSwitches::Get().IsOff("NoSuchEffect"));
}
