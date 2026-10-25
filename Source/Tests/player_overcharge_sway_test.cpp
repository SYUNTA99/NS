#include "Game/Player/PlayerParams.h"
#include "Game/PlayerActor.h"
#include "NSlib/CoreObject/Reflection/ReflectionJson.h"
#include "NSlib/CoreObject/Scene/Scene.h"
#include "NSlib/Core/Windows/Clock.h"
#include "Tests/TestViewCamera.h"

#include <gtest/gtest.h>

#include <cmath>

namespace
{
    // 欄の秒を固定ステップのフレーム数にする。PlayerActor::AdvanceCharge と同じ丸め
    int FramesFor(float seconds)
    {
        return static_cast<int>(std::lround(seconds / NS::OS::FrameTimer::FixedDelta()));
    }

    // 前 (z) から右 (x) へ振れた角度 (ラジアン)
    float SwayAngle(const NS::Vector3& direction)
    {
        return std::atan2(direction.x, direction.z);
    }

    // カメラが z を向いた場面に自機を 1 体置く。狙う相手はいないので、揺れは欄「相手がいない時に直す距離」で角度に直る
    struct SwayScene
    {
        NS::Scene scene;
        PlayerActor* player = nullptr;

        void Load()
        {
            nlohmann::json doc = NS::MakeSceneJson();
            nlohmann::json entry = NS::MakeActorJson();
            NS::SetActorJsonFile(entry, "PlayerActor");
            NS::SetActorJsonId(entry, 1);
            NS::SceneJsonActors(doc).push_back(std::move(entry));
            scene.ResetFromJson(doc);
            player = static_cast<PlayerActor*>(scene.Actors().FindByActorId(1));
            ASSERT_NE(player, nullptr);
            ASSERT_EQ(NS::ApplyJsonFields(&player->Params(),
                                          {{"チャージしきい値秒", 0.2f},
                                           {"チャージ満タン秒", 1.0f},
                                           {"溜めすぎの秒数", 1.0f},
                                           {"紫の揺れの最大のずれ", 2.0f},
                                           {"相手がいない時に直す距離", 10.0f}}),
                      0u);
            ASSERT_NE(PlaceViewCamera(scene, NS::Vector3{}, NS::Vector3{0.0f, 0.0f, 1.0f}), nullptr);
        }
    };
} // namespace

// 紫の威力は溜めきりの倍率に、紫の深さで 1 から欄「紫の威力の上限」まで上がる倍率を掛ける
TEST(PlayerOverchargeSway, PurplePowerRisesToTheCapFromTheFullCharge)
{
    SwayScene s;
    s.Load();
    PlayerActor& player = *s.player;
    ASSERT_EQ(NS::ApplyJsonFields(&player.Params(), {{"紫の威力の上限", 1.5f}}), 0u);
    const GL::Player::PlayerParams& params = player.Params();
    const float full = params.ChargeFactorFor(1.0f);
    EXPECT_FLOAT_EQ(params.ChargeFactorFor(1.0f, 0.0f), full);
    EXPECT_FLOAT_EQ(params.ChargeFactorFor(1.0f, 0.5f), full * 1.25f);
    EXPECT_FLOAT_EQ(params.ChargeFactorFor(1.0f, 1.0f), full * 1.5f);
    EXPECT_FLOAT_EQ(params.ChargeFactorFor(0.5f, 0.0f), params.ChargeFactorFor(0.5f));
}

