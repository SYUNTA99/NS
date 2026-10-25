#include "Game/Level/ImpactResolver.h"
#include "Game/Player/ChargeEffects.h"
#include "Game/Player/PlayerAppearance.h"
#include "Game/Player/PlayerParams.h"
#include "Game/PlayerActor.h"
#include "NSlib/CoreObject/ClassObjs/ActionTimeline.h"
#include "NSlib/CoreObject/ClassObjs/Body.h"
#include "NSlib/CoreObject/ClassObjs/EffectKeeper.h"
#include "NSlib/CoreObject/ClassObjs/Model.h"
#include "NSlib/CoreObject/ActorJson.h"
#include "NSlib/CoreObject/Reflection/ReflectionJson.h"
#include "NSlib/CoreObject/Scene/Scene.h"
#include "NSlib/CoreObject/Scene/SceneCamera.h"
#include "NSlib/Core/Windows/Clock.h"
#include "Tests/TestActorFile.h"
#include "Tests/TestTackleScene.h"
#include "Tests/TestViewCamera.h"

#include <gtest/gtest.h>

#include <cmath>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
    // 欄の秒を固定ステップのフレーム数にする。PlayerActor::AdvanceCharge と同じ丸め
    int FramesFor(float seconds)
    {
        return static_cast<int>(std::lround(seconds / NS::OS::FrameTimer::FixedDelta()));
    }

    // +Z を向くカメラと一緒に置いた、床の無いシーンの自機
    PlayerActor* PlaceChargePlayer(NS::Scene& scene)
    {
        nlohmann::json doc = NS::MakeSceneJson();
        nlohmann::json entry = NS::MakeActorJson();
        NS::SetActorJsonFile(entry, "PlayerActor");
        NS::SetActorJsonId(entry, 1);
        NS::SceneJsonActors(doc).push_back(std::move(entry));
        scene.ResetFromJson(doc);
        if (PlaceViewCamera(scene, NS::Vector3{}, NS::Vector3{0.0f, 0.0f, 1.0f}) == nullptr)
        {
            return nullptr;
        }
        return static_cast<PlayerActor*>(scene.Actors().FindByActorId(1));
    }

    const NS::EffectRecord* LiveEffect(const PlayerActor* player, std::string_view name)
    {
        NS_ASSERT(Game, player != nullptr, "LiveEffect: 渡したアクターか部品が無い");
        for (const NS::EffectRecord& record : player->GetEffectKeeper()->Records())
        {
            if (record.name == name && !record.endStep.has_value())
            {
                return &record;
            }
        }
        return nullptr;
    }
} // namespace

namespace
{
    // 放しの弾けの記録。出していなければ nullptr
    const NS::EffectRecord* ReleaseBurstOf(const PlayerActor* player)
    {
        NS_ASSERT(Game, player != nullptr, "ReleaseBurstOf: 渡したアクターか部品が無い");
        const NS::EffectRecord* burst = nullptr;
        for (const NS::EffectRecord& record : player->GetEffectKeeper()->Records())
        {
            if (record.name == "release.burst")
            {
                burst = &record;
            }
        }
        return burst;
    }

    // 置物へ +z の突進を出し、検知の歩まで回す。検知の歩と、当たる直前に入った歩の、演出の記録の歩を返す
    struct BurstRun
    {
        int contactStep = -1;
        int beforeContactStep = -1;
    };

    BurstRun RunBurstUntilContact(NS::Scene& scene, PlayerActor* player)
    {
        NS_ASSERT(Game, player != nullptr, "RunBurstUntilContact: 渡したアクターか部品が無い");
        BurstRun run;
        player->RequestBodySlam(1.0f, NS::Vector3{0.0f, 0.0f, 1.0f});
        for (int frame = 0; frame < 120 && run.contactStep < 0; ++frame)
        {
            scene.OnUpdate();
            if (run.beforeContactStep < 0 && player->IsInBeforeContact())
            {
                run.beforeContactStep = static_cast<int>(player->GetEffectKeeper()->Step());
            }
            if (player->Resolver().Answer().lastImpact.sequence != 0)
            {
                run.contactStep = static_cast<int>(player->GetEffectKeeper()->Step());
            }
        }
        return run;
    }
} // namespace

// 当たる直前の状態に入ったフレームに放しの弾けを消し、縮みと止まる間を光で隠さない
TEST(PlayerChargeSequence, ReleaseBurstEndsWhenTheBeforeContactStateStarts)
{
    NS::Scene scene;
    PlayerActor* player = PlaceTackleScene(scene, 0.0f);
    ASSERT_NE(player, nullptr);
    const BurstRun run = RunBurstUntilContact(scene, player);
    ASSERT_GE(run.contactStep, 0);
    ASSERT_GE(run.beforeContactStep, 0);
    const NS::EffectRecord* burst = ReleaseBurstOf(player);
    ASSERT_NE(burst, nullptr);
    ASSERT_TRUE(burst->endStep.has_value());
    EXPECT_EQ(static_cast<int>(*burst->endStep), run.beforeContactStep);
}

// 自機に当たる直前の動作が 1 つも無ければ線の先を予測しないので、触れたフレームに放しの弾けを消す
TEST(PlayerChargeSequence, ReleaseBurstEndsOnTheContactFrameWithoutABeforeContactAction)
{
    const ScopedActorFileEdit noActions("PlayerActor", [](nlohmann::json& actorFile) {
        NS::WriteActorActions(actorFile, std::vector<NS::ActorAction>{});
    });
    NS::Scene scene;
    PlayerActor* player = PlaceTackleScene(scene, 0.0f);
    ASSERT_NE(player, nullptr);
    const BurstRun run = RunBurstUntilContact(scene, player);
    ASSERT_GE(run.contactStep, 0);
    EXPECT_LT(run.beforeContactStep, 0);
    const NS::EffectRecord* burst = ReleaseBurstOf(player);
    ASSERT_NE(burst, nullptr);
    ASSERT_TRUE(burst->endStep.has_value());
    EXPECT_EQ(static_cast<int>(*burst->endStep), run.contactStep);
}

// 触れる見込みが 3 フレーム以内になったら放しの弾けを消す。当たる直前に入らない外れの当たりでも、触れる直前を光で飾らない
TEST(PlayerChargeSequence, ReleaseBurstEndsThreeFramesBeforeThePredictedContact)
{
    NS::Scene scene;
    PlayerActor* player = PlaceTackleScene(scene, 0.75f);
    ASSERT_NE(player, nullptr);
    const BurstRun run = RunBurstUntilContact(scene, player);
    ASSERT_GE(run.contactStep, 0);
    ASSERT_EQ(player->Resolver().Answer().lastImpact.tier, GL::Level::HitTier::Wide);
    EXPECT_LT(run.beforeContactStep, 0);
    const NS::EffectRecord* burst = ReleaseBurstOf(player);
    ASSERT_NE(burst, nullptr);
    ASSERT_TRUE(burst->endStep.has_value());
    EXPECT_EQ(static_cast<int>(*burst->endStep), run.contactStep - 3);
}
