#include "Game/Level/ReboundDirections.h"
#include "Game/Level/ReboundPreview.h"
#include "Game/Player/PlayerAppearance.h"
#include "Game/Player/PlayerParams.h"
#include "Game/Player/PlayerSounds.h"
#include "Game/Player/States/AirDashPlayerState.h"
#include "Game/PlayerActor.h"
#include "NSlib/Core/Windows/Clock.h"
#include "NSlib/Core/Windows/Input.h"
#include "NSlib/CoreObject/Action/ActorAction.h"
#include "NSlib/CoreObject/ActorJson.h"
#include "NSlib/CoreObject/ClassObjs/Body.h"
#include "NSlib/CoreObject/ClassObjs/Collider.h"
#include "NSlib/CoreObject/ClassObjs/HitReaction.h"
#include "NSlib/CoreObject/ClassObjs/PlayerInput.h"
#include "NSlib/CoreObject/Reflection/ReflectionJson.h"
#include "NSlib/CoreObject/Scene/Scene.h"
#include "NSlib/Phys/PhysicsScene.h"
#include "Tests/TestActorFile.h"
#include "Tests/TestLogCapture.h"
#include "Tests/TestSceneSetup.h"
#include "Tests/TestViewCamera.h"

#include <gtest/gtest.h>

#include <cmath>
#include <vector>

// エアダッシュ (押した瞬間に一定の速さで飛ぶ) と、地形の面での反動 (操作なし → 補正とダッシュ 1 回 → 普段) を縛る

namespace
{
    nlohmann::json ParamsFields(const PlayerActor* player)
    {
        return NS::SerializeClassObjFields(&player->Params());
    }

    // 自機の欄「エアダッシュのフレーム数」(状態の歩の回数)
    int AirDashFrames(const PlayerActor* player)
    {
        return ParamsFields(player).at("エアダッシュのフレーム数").get<int>();
    }

    // 欄「エアダッシュの距離」を、欄「エアダッシュのフレーム数」の固定フレームで進む速さ
    float DashSpeed(const PlayerActor* player)
    {
        return ParamsFields(player).at("エアダッシュの距離").get<float>() /
               (static_cast<float>(AirDashFrames(player)) * NS::OS::FrameTimer::FixedDelta());
    }

    // 自機の欄「エアダッシュのフレーム数」を書く
    void SetAirDashFrames(PlayerActor* player, int frames)
    {
        ASSERT_EQ(NS::ApplyJsonFields(&player->Params(), nlohmann::json{{"エアダッシュのフレーム数", frames}}), 0u);
    }

    // +Z を向くカメラと、宙に浮かせた自機。床は無い
    // 左スティックを worldDir の向きへ speedScale の深さで倒す。入力の部品のカメラは +Z のままなので、
    // スティックの右は世界の +X、前は世界の +Z になる
    void MoveStick(PlayerActor* player, const NS::Vector3& worldDir, float speedScale)
    {
        player->Input().SetStick(worldDir.x * speedScale, worldDir.z * speedScale);
    }

    PlayerActor* PlaceFloatingPlayer(NS::Scene& scene)
    {
        nlohmann::json doc = NS::MakeSceneJson();
        AddActorJson(doc, "PlayerActor", 1);
        scene.ResetFromJson(doc);
        if (PlaceViewCamera(scene, NS::Vector3{0.0f, 5.0f, -5.0f}, NS::Vector3{0.0f, 5.0f, 0.0f}) == nullptr)
        {
            return nullptr;
        }
        PlayerActor* player = static_cast<PlayerActor*>(scene.Actors().FindByActorId(1));
        if (player != nullptr)
        {
            player->TransformObj().SetPosition(NS::Vector3{0.0f, 5.0f, 0.0f});
            // このファイルのテストは、表から向きを選ぶ方式を既定にする。反射から決める方式は UseReflectedRebound で選ぶ
            EXPECT_EQ(NS::ApplyJsonFields(&player->Params(), nlohmann::json{{"反動の向きを反射から決める", false}}),
                      0u);
        }
        return player;
    }

    void AddBox(NS::Scene& scene, const NS::Vector3& center, const NS::Vector3& half)
    {
        AddTerrainBox(scene, center, half.x, half.y, half.z);
    }

    // 自機の前 (+Z) の、面が z = 3 の大きな壁
    void AddFrontWall(NS::Scene& scene)
    {
        AddBox(scene, NS::Vector3{0.0f, 5.0f, 3.5f}, NS::Vector3{20.0f, 20.0f, 0.5f});
    }

    // +Z へ押した瞬間にダッシュし、放したスティックのまま壁へ当てる。反動へ移ったら true
    bool DashIntoFrontWall(PlayerActor* player)
    {
        MoveStick(player, NS::Vector3{0.0f, 0.0f, 1.0f}, 1.0f);
        player->Update(true);
        MoveStick(player, NS::Vector3{}, 0.0f);
        for (int frame = 0; frame < 30; ++frame)
        {
            if (player->IsDashRebounding())
            {
                return true;
            }
            player->Update(true);
        }
        return player->IsDashRebounding();
    }

    // 当たったフレームの直後から、止めが明けるまでのフレーム数を当たったフレームを入れて数える。
    // wiggle なら毎フレーム右と右前へ交互に倒し、動いているとみなす変化より大きく動かす
    int CountReboundStopFrames(PlayerActor* player, bool wiggle)
    {
        int stopped = 1;
        for (int frame = 0; frame < 120 && player->IsHitStopping(); ++frame)
        {
            if (wiggle)
            {
                NS::Vector3 tilt{1.0f, 0.0f, 0.0f};
                if (frame % 2 == 0)
                {
                    tilt = NS::Vector3{0.8f, 0.0f, 0.6f};
                }
                MoveStick(player, tilt, 1.0f);
            }
            player->Update(false);
            ++stopped;
        }
        return stopped;
    }

    float AngleBetween(const NS::Vector3& a, const NS::Vector3& b)
    {
        NS::Vector3 na = a;
        NS::Vector3 nb = b;
        na.Normalize();
        nb.Normalize();
        return NS::AngleBetweenUnit(na, nb);
    }

    // 宙の自機でエアダッシュを押しても、ダッシュに入らず、回数も減らず、WARN を 1 回出す事を確かめる
    void ExpectNoDashOnPress()
    {
        NS::Scene scene;
        PlayerActor* player = PlaceFloatingPlayer(scene);
        ASSERT_NE(player, nullptr);
        SetAirDashFrames(player, 0);
        const TestLogCapture log;
        MoveStick(player, NS::Vector3{1.0f, 0.0f, 0.0f}, 1.0f);
        player->Update(true);
        EXPECT_FALSE(player->IsAirDashing());
        EXPECT_EQ(player->AirDash().Remaining(), 1);
        EXPECT_EQ(log.WarningCount(), 1);
    }
} // namespace

TEST(PlayerAirDashTest, PressFrameStartsTheDashAtTheDashSpeed)
{
    NS::Scene scene;
    PlayerActor* player = PlaceFloatingPlayer(scene);
    ASSERT_NE(player, nullptr);
    const float distance = ParamsFields(player).at("エアダッシュの距離").get<float>();
    ASSERT_GT(AirDashFrames(player), 0);
    const float speed = DashSpeed(player);
    const float startX = player->TransformObj().Position().x;

    MoveStick(player, NS::Vector3{1.0f, 0.0f, 0.0f}, 1.0f);
    player->Update(true);
    EXPECT_TRUE(player->IsAirDashing());
    EXPECT_NEAR(player->Body().Velocity().x, speed, 1.0e-3f);
    EXPECT_NEAR(player->Body().Velocity().y, 0.0f, 1.0e-3f);

    const int frames = AirDashFrames(player);
    for (int frame = 0; frame < frames + 1 && player->IsAirDashing(); ++frame)
    {
        player->Update(true);
    }
    EXPECT_FALSE(player->IsAirDashing());
    EXPECT_EQ(player->AirDash().Remaining(), 0);
    // 距離はダッシュの速さで進む分。終わるフレームは残る速さで進むので、1 フレームで進む分だけずれてよい
    EXPECT_NEAR(player->TransformObj().Position().x - startX, distance, speed * NS::OS::FrameTimer::FixedDelta());
}

// やり直しの後に一度も接地しないまま宙で動いても、その後の着地の音は鳴る
TEST(PlayerAirDashTest, DashFromTheAirAllowsTheNextLandingSound)
{
    NS::Scene scene;
    PlayerActor* player = PlaceFloatingPlayer(scene);
    ASSERT_NE(player, nullptr);
    ASSERT_GT(AirDashFrames(player), 0);
    ASSERT_FALSE(player->Sounds().IsLandAllowed());

    MoveStick(player, NS::Vector3{1.0f, 0.0f, 0.0f}, 1.0f);
    player->Update(true);
    ASSERT_TRUE(player->IsAirDashing());
    EXPECT_TRUE(player->Sounds().IsLandAllowed());
}

// 入力し直して 1 歩止めてから押せば、押したフレームに入力している向きへ出る
TEST(PlayerAirDashTest, PressAStepAfterTheStickStopsDashesTheNewWay)
{
    NS::Scene scene;
    PlayerActor* player = PlaceFloatingPlayer(scene);
    ASSERT_NE(player, nullptr);

    MoveStick(player, NS::Vector3{0.0f, 0.0f, 1.0f}, 1.0f);
    for (int frame = 0; frame < 30; ++frame)
    {
        player->Update(false);
    }
    MoveStick(player, NS::Vector3{1.0f, 0.0f, 0.0f}, 1.0f);
    player->Update(false);
    player->Update(true);
    ASSERT_TRUE(player->IsAirDashing());
    EXPECT_GT(player->Body().Velocity().x, 0.0f);
    EXPECT_NEAR(player->Body().Velocity().z, 0.0f, 1.0e-3f);
}

// 反動のためにスティックを回している最中に押すと、回し始める前に止まっていた向きへ出る
TEST(PlayerAirDashTest, PressWhileTheStickMovesDashesTheWayItRestedBefore)
{
    NS::Scene scene;
    PlayerActor* player = PlaceFloatingPlayer(scene);
    ASSERT_NE(player, nullptr);
    const float change = ParamsFields(player).at("スティックが動いているとみなす変化").get<float>();
    const NS::Vector3 turned20{0.342f, 0.0f, 0.940f};
    const NS::Vector3 turned40{0.643f, 0.0f, 0.766f};
    // 1 歩で回す量が、動いているとみなす変化より大きい
    ASSERT_GT((turned40 - turned20).Length(), change);
    ASSERT_GT((turned20 - NS::Vector3{0.0f, 0.0f, 1.0f}).Length(), change);

    MoveStick(player, NS::Vector3{0.0f, 0.0f, 1.0f}, 1.0f);
    for (int frame = 0; frame < 30; ++frame)
    {
        player->Update(false);
    }
    MoveStick(player, turned20, 1.0f);
    player->Update(false);
    MoveStick(player, turned40, 1.0f);
    player->Update(true);
    ASSERT_TRUE(player->IsAirDashing());
    EXPECT_NEAR(player->Body().Velocity().x, 0.0f, 1.0e-3f);
    EXPECT_GT(player->Body().Velocity().z, 0.0f);
}

// 浅く倒して回している最中に押しても、倒しきりの時と同じように、回し始める前に止まっていた向きへ出る
TEST(PlayerAirDashTest, PressWhileAShallowStickMovesDashesTheWayItRestedBefore)
{
    NS::Scene scene;
    PlayerActor* player = PlaceFloatingPlayer(scene);
    ASSERT_NE(player, nullptr);
    const float tilt = 0.5f;
    const float change = ParamsFields(player).at("スティックが動いているとみなす変化").get<float>();
    const NS::Vector3 rested{0.0f, 0.0f, tilt};
    const NS::Vector3 turned10{tilt * 0.174f, 0.0f, tilt * 0.985f};
    const NS::Vector3 turned20{tilt * 0.342f, 0.0f, tilt * 0.940f};
    // 位置の差は、動いているとみなす変化より大きい
    ASSERT_GT((turned10 - rested).Length(), change);
    ASSERT_GT((turned20 - turned10).Length(), change);

    MoveStick(player, rested, tilt);
    for (int frame = 0; frame < 30; ++frame)
    {
        player->Update(false);
    }
    MoveStick(player, turned10, tilt);
    player->Update(false);
    MoveStick(player, turned20, tilt);
    player->Update(true);
    ASSERT_TRUE(player->IsAirDashing());
    EXPECT_NEAR(player->Body().Velocity().x, 0.0f, 1.0e-3f);
    EXPECT_GT(player->Body().Velocity().z, 0.0f);
}

// スティックを止めたままカメラだけを回している時に押すと、今入力している向きへ出る
TEST(PlayerAirDashTest, PressWhileOnlyTheCameraTurnsDashesTheInputWay)
{
    NS::Scene scene;
    PlayerActor* player = PlaceFloatingPlayer(scene);
    ASSERT_NE(player, nullptr);

    player->Input().SetStick(0.0f, 1.0f);
    for (int frame = 0; frame < 30; ++frame)
    {
        player->Update(false);
    }
    // カメラの回転で、同じスティックの世界の向きだけが 1 歩に 0.08 ラジアン回る
    constexpr float k_TurnRadians = 0.08f;
    float angle = 0.0f;
    for (int frame = 0; frame < 20; ++frame)
    {
        angle += k_TurnRadians;
        player->Input().SetCameraForward(NS::Vector3{std::sin(angle), 0.0f, std::cos(angle)});
        player->Input().SetStick(0.0f, 1.0f);
        player->Update(false);
    }
    angle += k_TurnRadians;
    player->Input().SetCameraForward(NS::Vector3{std::sin(angle), 0.0f, std::cos(angle)});
    player->Input().SetStick(0.0f, 1.0f);
    player->Update(true);
    ASSERT_TRUE(player->IsAirDashing());
    const NS::Vector3 velocity = player->Body().Velocity();
    EXPECT_NEAR(std::atan2(velocity.x, velocity.z), angle, 1.0e-3f);
}

// 真ん中から入力すると同時に押すと、止まっていた向きが無いので入力した向きへ出る。
// 真ん中へ戻す前に入力していた向きは、止まっていた向きに残らない
TEST(PlayerAirDashTest, PressWhilePushingOutFromTheCenterDashesTheInputWay)
{
    NS::Scene scene;
    PlayerActor* player = PlaceFloatingPlayer(scene);
    ASSERT_NE(player, nullptr);

    MoveStick(player, NS::Vector3{0.0f, 0.0f, 1.0f}, 1.0f);
    for (int frame = 0; frame < 30; ++frame)
    {
        player->Update(false);
    }
    MoveStick(player, NS::Vector3{}, 0.0f);
    for (int frame = 0; frame < 30; ++frame)
    {
        player->Update(false);
    }
    MoveStick(player, NS::Vector3{1.0f, 0.0f, 0.0f}, 1.0f);
    player->Update(true);
    ASSERT_TRUE(player->IsAirDashing());
    EXPECT_GT(player->Body().Velocity().x, 0.0f);
    EXPECT_NEAR(player->Body().Velocity().z, 0.0f, 1.0e-3f);
}

// 当たらなければ、押したフレームから欄のフレーム数の歩だけダッシュの速さで進み、次の歩で残る速さにして普段の動きへ移る
TEST(PlayerAirDashTest, DashLastsTheFramesFieldThenKeepsTheExitSpeed)
{
    NS::Scene scene;
    PlayerActor* player = PlaceFloatingPlayer(scene);
    ASSERT_NE(player, nullptr);
    const int length = AirDashFrames(player);
    ASSERT_GT(length, 0);
    const float speed = DashSpeed(player);

    MoveStick(player, NS::Vector3{1.0f, 0.0f, 0.0f}, 1.0f);
    player->Update(true);
    MoveStick(player, NS::Vector3{}, 0.0f);
    for (int step = 1; step < length; ++step)
    {
        SCOPED_TRACE(step);
        ASSERT_TRUE(player->IsAirDashing());
        EXPECT_NEAR(player->Body().Velocity().x, speed, 1.0e-3f);
        player->Update(false);
    }
    ASSERT_TRUE(player->IsAirDashing());
    player->Update(false);
    EXPECT_FALSE(player->IsAirDashing());
    EXPECT_NEAR(player->Body().Velocity().x, ParamsFields(player).at("エアダッシュ後に残る速さ").get<float>(), 1.0e-3f);
}

// ダッシュの長さと速さは欄「エアダッシュのフレーム数」で決まる。.actor の動作 "AirDash" は長さを持たない
TEST(PlayerAirDashTest, TheDashLengthAndSpeedComeFromTheFramesField)
{
    NS::Scene scene;
    PlayerActor* player = PlaceFloatingPlayer(scene);
    ASSERT_NE(player, nullptr);
    const NS::ActorActionInfo* action = player->ActorActionClassObj()->FindAction("AirDash");
    ASSERT_NE(action, nullptr);
    EXPECT_EQ(action->length, 0);
    SetAirDashFrames(player, 10);
    ASSERT_EQ(AirDashFrames(player), 10);
    const float speed =
        ParamsFields(player).at("エアダッシュの距離").get<float>() / (10.0f * NS::OS::FrameTimer::FixedDelta());

    MoveStick(player, NS::Vector3{1.0f, 0.0f, 0.0f}, 1.0f);
    player->Update(true);
    MoveStick(player, NS::Vector3{}, 0.0f);
    EXPECT_NEAR(player->Body().Velocity().x, speed, 1.0e-3f);
    for (int step = 1; step < 10; ++step)
    {
        player->Update(false);
    }
    ASSERT_TRUE(player->IsAirDashing());
    player->Update(false);
    EXPECT_FALSE(player->IsAirDashing());
}

// エアダッシュの距離は正の数だけを書く。0・負・無限大を読むと今の値を残す
TEST(PlayerAirDashTest, AirDashDistanceFieldKeepsTheValueOnZeroNegativeAndInfinite)
{
    GL::Player::PlayerParams params;
    const NS::FieldDesc* field = NS::FindField(params.GetReflection(), "エアダッシュの距離");
    ASSERT_NE(field, nullptr);
    float before = 0.0f;
    field->get(&params, &before);
    // 1e39 は float に収まらず、読むと無限大になる
    for (const double value : {0.0, -1.0, 1.0e39})
    {
        SCOPED_TRACE(value);
        EXPECT_EQ(NS::ApplyJsonFields(&params, nlohmann::json{{"エアダッシュの距離", value}}), 0u);
        float after = 0.0f;
        field->get(&params, &after);
        EXPECT_EQ(after, before);
    }
}

// 反動の 2 つの倍率は、負と無限大を読むと今の値を残す。負は演出の強さと速度の向きを裏返す。
// 合った時の速さの倍率は 0 も残す (速度が 0 になる)。演出の倍率の 0 は演出を消すだけなので書く
TEST(PlayerAirDashTest, ReboundScaleFieldsKeepTheValueOnNegativeAndInfinite)
{
    struct Case
    {
        const char* name;
        std::vector<double> rejected;
    };
    const std::vector<Case> cases{{"向きが合った時の返る速さの倍率", {0.0, -1.0, 1.0e39}},
                                  {"ずらした時の演出の強さの倍率", {-1.0, 1.0e39}}};
    for (const Case& c : cases)
    {
        SCOPED_TRACE(c.name);
        GL::Player::PlayerParams params;
        const NS::FieldDesc* field = NS::FindField(params.GetReflection(), c.name);
        ASSERT_NE(field, nullptr);
        float before = 0.0f;
        field->get(&params, &before);
        for (const double value : c.rejected)
        {
            SCOPED_TRACE(value);
            EXPECT_EQ(NS::ApplyJsonFields(&params, nlohmann::json{{c.name, value}}), 0u);
            float after = 0.0f;
            field->get(&params, &after);
            EXPECT_EQ(after, before);
        }
    }
    GL::Player::PlayerParams params;
    const NS::FieldDesc* effect = NS::FindField(params.GetReflection(), "ずらした時の演出の強さの倍率");
    ASSERT_NE(effect, nullptr);
    EXPECT_EQ(NS::ApplyJsonFields(&params, nlohmann::json{{"ずらした時の演出の強さの倍率", 0.0}}), 0u);
    float written = 1.0f;
    effect->get(&params, &written);
    EXPECT_EQ(written, 0.0f);
}

// 画面で同じに見える角度の差は、負と無限大を読むと今の値を残す。負は一番近い向きまで候補から外し、選んだ向きが無いまま読む
TEST(PlayerAirDashTest, ReboundScreenTieFieldKeepsTheValueOnNegativeAndInfinite)
{
    GL::Player::PlayerParams params;
    const NS::FieldDesc* field = NS::FindField(params.GetReflection(), "画面で同じに見える角度の差");
    ASSERT_NE(field, nullptr);
    float before = 0.0f;
    field->get(&params, &before);
    for (const double value : {-1.0, 1.0e39})
    {
        SCOPED_TRACE(value);
        EXPECT_EQ(NS::ApplyJsonFields(&params, nlohmann::json{{"画面で同じに見える角度の差", value}}), 0u);
        float after = 0.0f;
        field->get(&params, &after);
        EXPECT_EQ(after, before);
    }
}

// 欄「エアダッシュのフレーム数」が 0 の自機は、押してもエアダッシュに入らない。入ると終わりが来ず宙で止まる
TEST(PlayerAirDashTest, NoDashWithZeroAirDashFrames)
{
    ExpectNoDashOnPress();
}

// .actor に動作 "AirDash" が無くても、ダッシュは欄のフレーム数だけ続いて終わる
TEST(PlayerAirDashTest, TheDashRunsItsFramesWithoutItsAction)
{
    const ScopedActorFileEdit removed("PlayerActor", [](nlohmann::json& actorFile) {
        std::vector<NS::ActorActionInfo> actions = NS::ReadActorActions("PlayerActor", actorFile);
        std::erase_if(actions, [](const NS::ActorActionInfo& action) { return action.name == "AirDash"; });
        NS::WriteActorActions(actorFile, actions);
    });
    NS::Scene scene;
    PlayerActor* player = PlaceFloatingPlayer(scene);
    ASSERT_NE(player, nullptr);
    const TestLogCapture log;
    (void)NS::SetState<GL::Player::AirDashPlayerState>(player);
    ASSERT_TRUE(player->IsAirDashing());
    player->Update(false);
    EXPECT_TRUE(player->IsAirDashing());
    for (int step = 0; step < AirDashFrames(player) + 1 && player->IsAirDashing(); ++step)
    {
        player->Update(false);
    }
    EXPECT_FALSE(player->IsAirDashing());
}

// 途中で欄「エアダッシュのフレーム数」を書き換えても、そのダッシュの速さは始めた時のまま
TEST(PlayerAirDashTest, TheDashSpeedHoldsWhenTheLengthChangesMidway)
{
    NS::Scene scene;
    PlayerActor* player = PlaceFloatingPlayer(scene);
    ASSERT_NE(player, nullptr);
    ASSERT_GT(AirDashFrames(player), 10);
    const float speed = DashSpeed(player);
    MoveStick(player, NS::Vector3{1.0f, 0.0f, 0.0f}, 1.0f);
    player->Update(true);
    MoveStick(player, NS::Vector3{}, 0.0f);
    player->Update(false);
    ASSERT_TRUE(player->IsAirDashing());
    SetAirDashFrames(player, 10);
    player->Update(false);
    ASSERT_TRUE(player->IsAirDashing());
    EXPECT_NEAR(player->Body().Velocity().x, speed, 1.0e-3f);
}

// 途中で動作が止まっても (.actor の読み直し)、ダッシュは欄のフレーム数で終わる
TEST(PlayerAirDashTest, TheDashEndsEvenIfTheActionIsReloadedMidway)
{
    NS::Scene scene;
    PlayerActor* player = PlaceFloatingPlayer(scene);
    ASSERT_NE(player, nullptr);
    const int length = AirDashFrames(player);
    ASSERT_GT(length, 1);
    MoveStick(player, NS::Vector3{1.0f, 0.0f, 0.0f}, 1.0f);
    player->Update(true);
    MoveStick(player, NS::Vector3{}, 0.0f);
    player->Update(false);
    ASSERT_TRUE(player->IsAirDashing());
    player->ReloadActions();
    for (int step = 0; step < length + 1 && player->IsAirDashing(); ++step)
    {
        player->Update(false);
    }
    EXPECT_FALSE(player->IsAirDashing());
}

TEST(PlayerAirDashTest, HittingAWallReboundsStraightBackAtTheFullSpeed)
{
    NS::Scene scene;
    PlayerActor* player = PlaceFloatingPlayer(scene);
    ASSERT_NE(player, nullptr);
    AddFrontWall(scene);
    const float speed =
        DashSpeed(player) * ParamsFields(player).at("返る速さのダッシュの速さに対する倍率").get<float>();

    ASSERT_TRUE(DashIntoFrontWall(player));
    const NS::Vector3 velocity = player->Body().Velocity();
    EXPECT_NEAR(velocity.x, 0.0f, 1.0e-3f);
    EXPECT_NEAR(velocity.y, 0.0f, 1.0e-3f);
    EXPECT_NEAR(velocity.z, -speed, 1.0e-3f);
}

// 出荷する自機の値では、ダッシュのフレーム数を増やして遅くした後も、返る速さは前と同じ 24 m/s
TEST(PlayerAirDashTest, ShippedValuesKeepTheReboundSpeedAfterSlowingTheDash)
{
    NS::Scene scene;
    PlayerActor* player = PlaceFloatingPlayer(scene);
    ASSERT_NE(player, nullptr);
    AddFrontWall(scene);
    ASSERT_TRUE(DashIntoFrontWall(player));
    EXPECT_NEAR(player->Body().Velocity().Length(), 24.0f, 0.1f);
}

TEST(PlayerAirDashTest, NoDashDuringTheLockThenOneDashInTheSteerStage)
{
    NS::Scene scene;
    PlayerActor* player = PlaceFloatingPlayer(scene);
    ASSERT_NE(player, nullptr);
    AddFrontWall(scene);
    const nlohmann::json fields = ParamsFields(player);
    ASSERT_TRUE(DashIntoFrontWall(player));

    // 操作なしの間の押しはダッシュにならない
    player->Update(false);
    player->Update(true);
    EXPECT_TRUE(player->IsDashRebounding());
    EXPECT_FALSE(player->IsAirDashing());

    // 当たりのヒットストップの間は反動のフレーム数が進まないので、反動のフレーム数で待つ
    const int lock = fields.at("反動の操作なしのフレーム数").get<int>();
    for (int frame = 0; frame < 60 && player->AirDash().ReboundFrames() <= lock; ++frame)
    {
        player->Update(false);
    }
    player->Update(true);
    EXPECT_TRUE(player->IsAirDashing());

    // 補正の段で戻ったダッシュは 1 回だけ。使い切ったら次の押しは効かない
    for (int frame = 0; frame < AirDashFrames(player) + 1; ++frame)
    {
        player->Update(false);
    }
    ASSERT_FALSE(player->IsAirDashing());
    player->Update(true);
    EXPECT_FALSE(player->IsAirDashing());
}

TEST(PlayerAirDashTest, GroundReboundStillLocksTheDashWhileTheCountIsBack)
{
    NS::Scene scene;
    PlayerActor* player = PlaceFloatingPlayer(scene);
    ASSERT_NE(player, nullptr);
    AddFrontWall(scene);
    // 床に立たせてから壁へダッシュする。接地の間は回数が戻るので、止めるのは操作なしの段だけ
    AddBox(scene, NS::Vector3{0.0f, -0.5f, 0.0f}, NS::Vector3{50.0f, 0.5f, 50.0f});
    player->TransformObj().SetPosition(NS::Vector3{0.0f, 0.9f, 0.0f});
    for (int frame = 0; frame < 5; ++frame)
    {
        player->Update(false);
    }
    ASSERT_TRUE(player->Body().IsGrounded());
    ASSERT_TRUE(DashIntoFrontWall(player));

    player->Update(false);
    EXPECT_EQ(player->AirDash().Remaining(), 1);
    player->Update(true);
    EXPECT_TRUE(player->IsDashRebounding());
    EXPECT_FALSE(player->IsAirDashing());
}

TEST(PlayerAirDashTest, LandingRestoresTheDash)
{
    NS::Scene scene;
    PlayerActor* player = PlaceFloatingPlayer(scene);
    ASSERT_NE(player, nullptr);
    // 自機の 5 m 下の床
    AddBox(scene, NS::Vector3{0.0f, -0.5f, 0.0f}, NS::Vector3{50.0f, 0.5f, 50.0f});

    MoveStick(player, NS::Vector3{1.0f, 0.0f, 0.0f}, 1.0f);
    player->Update(true);
    MoveStick(player, NS::Vector3{}, 0.0f);
    for (int frame = 0; frame < 240 && !player->Body().IsGrounded(); ++frame)
    {
        player->Update(false);
    }
    ASSERT_TRUE(player->Body().IsGrounded());
    player->Update(false);
    EXPECT_EQ(player->AirDash().Remaining(), 1);
}

TEST(PlayerAirDashTest, SteeringNeverTurnsMoreThanTheLimit)
{
    NS::Scene scene;
    PlayerActor* player = PlaceFloatingPlayer(scene);
    ASSERT_NE(player, nullptr);
    AddFrontWall(scene);
    const nlohmann::json fields = ParamsFields(player);
    ASSERT_TRUE(DashIntoFrontWall(player));
    // 止めの間に倒すとずらしになるので、操作なしの間が明けてから測る
    const int lock = fields.at("反動の操作なしのフレーム数").get<int>();
    for (int frame = 0; frame < 60 && player->AirDash().ReboundFrames() <= lock; ++frame)
    {
        player->Update(false);
    }
    // 補正の段の間ずっと真横へ倒す
    MoveStick(player, NS::Vector3{1.0f, 0.0f, 0.0f}, 1.0f);
    const int frames = fields.at("反動の補正が終わるフレーム数").get<int>();
    float largest = 0.0f;
    for (int frame = 0; frame < frames && player->IsDashRebounding(); ++frame)
    {
        player->Update(false);
        // 曲げは速度の全部を回すので、真後ろへ返る縦の面から外れた角度で測る。重力と減速はこの角度を増やさない
        const NS::Vector3 velocity = player->Body().Velocity();
        largest = std::max(largest, std::asin(std::abs(velocity.x) / velocity.Length()));
    }
    const float limit = GL::Level::DirectionStepRadians(fields.at("向きの表の分割数").get<int>()) *
                        fields.at("曲げの上限の段の割合").get<float>();
    EXPECT_GT(largest, 0.0f);
    EXPECT_LE(largest, limit + 1.0e-3f);
}

TEST(PlayerAirDashTest, HoldingForwardThroughTheSteerStageBendsUpNotSideways)
{
    const auto steer = [](const NS::Vector3& desired, float scale) {
        NS::Scene scene;
        PlayerActor* player = PlaceFloatingPlayer(scene);
        AddFrontWall(scene);
        const nlohmann::json fields = ParamsFields(player);
        EXPECT_TRUE(DashIntoFrontWall(player));
        const int lock = fields.at("反動の操作なしのフレーム数").get<int>();
        for (int frame = 0; frame < 60 && player->AirDash().ReboundFrames() <= lock; ++frame)
        {
            player->Update(false);
        }
        MoveStick(player, desired, scale);
        const int end = fields.at("反動の補正が終わるフレーム数").get<int>();
        for (int frame = 0; frame < 120 && player->AirDash().ReboundFrames() + 3 < end; ++frame)
        {
            player->Update(false);
        }
        return player->Body().Velocity();
    };
    const NS::Vector3 released = steer(NS::Vector3{}, 0.0f);
    NS::Vector3 forward{-0.3f, 0.0f, 1.0f};
    forward.Normalize();
    const NS::Vector3 held = steer(forward, 1.0f);

    const float sideways = std::atan2(std::abs(held.x), -held.z);
    EXPECT_LT(sideways, NS::ToRadians(NS::Degrees{10.0f}).value);
    EXPECT_GT(held.y, released.y);
}

// 地形の反動はカメラを回さず、回す入力も止めない。縦の向きは返った向きの縦の成分で渡し、ダッシュと反動の間は視野を広げる
TEST(PlayerAirDashTest, CameraStateDuringDashAndTerrainRebound)
{
    NS::Scene scene;
    PlayerActor* player = PlaceFloatingPlayer(scene);
    ASSERT_NE(player, nullptr);
    AddFrontWall(scene);

    MoveStick(player, NS::Vector3{0.0f, 0.0f, 1.0f}, 1.0f);
    player->Update(true);
    ASSERT_TRUE(player->IsAirDashing());
    const GL::Player::CameraTargetState dashing = player->GetCameraTargetState();
    EXPECT_TRUE(dashing.rebound.widenView);
    EXPECT_FALSE(dashing.rebound.rebounding);

    MoveStick(player, NS::Vector3{}, 0.0f);
    for (int frame = 0; frame < 30 && !player->IsDashRebounding(); ++frame)
    {
        player->Update(false);
    }
    ASSERT_TRUE(player->IsDashRebounding());
    MoveStick(player, NS::Vector3{0.0f, 0.0f, 1.0f}, 1.0f);
    for (int frame = 0; frame < 30 && player->AirDash().LastRebound().shifted.y <= 0.5f; ++frame)
    {
        player->Update(false);
    }
    ASSERT_TRUE(player->IsDashRebounding());
    ASSERT_GT(player->AirDash().LastRebound().shifted.y, 0.5f);
    const GL::Player::CameraTargetState rebounding = player->GetCameraTargetState();
    EXPECT_TRUE(rebounding.rebound.rebounding);
    EXPECT_TRUE(rebounding.rebound.widenView);
    EXPECT_FLOAT_EQ(rebounding.rebound.verticalLean, player->AirDash().LastRebound().shifted.y);

    player->ResetState();
    EXPECT_FALSE(player->GetCameraTargetState().rebound.widenView);
}

TEST(PlayerAirDashTest, TiltAfterTheHitShiftsTheReboundOneStep)
{
    NS::Scene scene;
    PlayerActor* player = PlaceFloatingPlayer(scene);
    ASSERT_NE(player, nullptr);
    AddFrontWall(scene);
    const int lock = ParamsFields(player).at("反動の操作なしのフレーム数").get<int>();
    ASSERT_TRUE(DashIntoFrontWall(player));
    ASSERT_NEAR(player->AirDash().LastRebound().shifted.y, 0.0f, 1.0e-3f);

    MoveStick(player, NS::Vector3{0.0f, 0.0f, 1.0f}, 1.0f);
    for (int frame = 0; frame < 60 && player->AirDash().ReboundFrames() <= lock; ++frame)
    {
        player->Update(false);
    }
    const NS::Vector3 shifted = player->AirDash().LastRebound().shifted;
    EXPECT_NEAR(shifted.y, std::sqrt(0.5f), 1.0e-3f);
    EXPECT_NEAR(shifted.z, -std::sqrt(0.5f), 1.0e-3f);
    EXPECT_GT(player->Body().Velocity().y, 0.0f);
}

TEST(PlayerAirDashTest, HoldingTheDashStickThroughTheHitShiftsTheReboundOneStep)
{
    NS::Scene scene;
    PlayerActor* player = PlaceFloatingPlayer(scene);
    ASSERT_NE(player, nullptr);
    AddFrontWall(scene);
    const int lock = ParamsFields(player).at("反動の操作なしのフレーム数").get<int>();

    MoveStick(player, NS::Vector3{0.0f, 0.0f, 1.0f}, 1.0f);
    player->Update(true);
    for (int frame = 0; frame < 30 && !player->IsDashRebounding(); ++frame)
    {
        player->Update(false);
    }
    ASSERT_TRUE(player->IsDashRebounding());
    for (int frame = 0; frame < 60 && player->AirDash().ReboundFrames() <= lock; ++frame)
    {
        player->Update(false);
    }
    const NS::Vector3 shifted = player->AirDash().LastRebound().shifted;
    EXPECT_NEAR(shifted.y, std::sqrt(0.5f), 1.0e-3f);
    EXPECT_NEAR(shifted.z, -std::sqrt(0.5f), 1.0e-3f);
}

// 向きを選ぶのは止めが明けたフレームの 1 回だけ。明けた後に倒しても、操作なしの段の間は曲がらない
TEST(PlayerAirDashTest, TiltAfterTheReleaseDoesNotShift)
{
    NS::Scene scene;
    PlayerActor* player = PlaceFloatingPlayer(scene);
    ASSERT_NE(player, nullptr);
    AddFrontWall(scene);
    const int lock = ParamsFields(player).at("反動の操作なしのフレーム数").get<int>();
    ASSERT_TRUE(DashIntoFrontWall(player));
    for (int frame = 0; frame < 60 && player->IsHitStopping(); ++frame)
    {
        player->Update(false);
    }
    // 明けのフレーム。ここで放したスティックのまま向きが決まる
    player->Update(false);
    ASSERT_GT(player->AirDash().ReboundFrames(), 0);
    ASSERT_LE(player->AirDash().ReboundFrames(), lock);

    MoveStick(player, NS::Vector3{0.0f, 0.0f, 1.0f}, 1.0f);
    for (int frame = 0; frame < 60 && player->AirDash().ReboundFrames() <= lock; ++frame)
    {
        player->Update(false);
    }
    EXPECT_NEAR(player->AirDash().LastRebound().shifted.y, 0.0f, 1.0e-3f);
}

// スティックを動かさなければ、当たったフレームを入れて最短のフレーム数だけ止まる
TEST(PlayerAirDashTest, StillStickStopsTheReboundForTheShortestFrames)
{
    NS::Scene scene;
    PlayerActor* player = PlaceFloatingPlayer(scene);
    ASSERT_NE(player, nullptr);
    AddFrontWall(scene);
    const int shortest = ParamsFields(player).at("反動の止めの最短のフレーム数").get<int>();
    ASSERT_GT(shortest, 1);
    ASSERT_TRUE(DashIntoFrontWall(player));

    EXPECT_EQ(CountReboundStopFrames(player, false), shortest);
}

// 止めの間にスティックを動かし続けると、止めは上限のフレーム数まで延び、それより延びない
TEST(PlayerAirDashTest, MovingStickExtendsTheReboundStopUpToTheLimit)
{
    NS::Scene scene;
    PlayerActor* player = PlaceFloatingPlayer(scene);
    ASSERT_NE(player, nullptr);
    AddFrontWall(scene);
    const int longest = 12;
    ASSERT_EQ(NS::ApplyJsonFields(&player->Params(), nlohmann::json{{"反動の止めの上限のフレーム数", longest}}), 0u);
    ASSERT_GT(longest, ParamsFields(player).at("反動の止めの最短のフレーム数").get<int>());
    ASSERT_TRUE(DashIntoFrontWall(player));

    EXPECT_EQ(CountReboundStopFrames(player, true), longest);
}

// 既定の調整値では、止めの間にスティックを動かし続けても、止めは最短のフレーム数で明ける
TEST(PlayerAirDashTest, MovingStickDoesNotExtendTheReboundStopByDefault)
{
    NS::Scene scene;
    PlayerActor* player = PlaceFloatingPlayer(scene);
    ASSERT_NE(player, nullptr);
    AddFrontWall(scene);
    const int shortest = ParamsFields(player).at("反動の止めの最短のフレーム数").get<int>();
    ASSERT_TRUE(DashIntoFrontWall(player));

    EXPECT_EQ(CountReboundStopFrames(player, true), shortest);
}

// 延ばすかは倒した量でなく動いた量で見る。ダッシュの向きへ倒したまま当てて動かさなければ、最短のフレーム数で明ける
TEST(PlayerAirDashTest, HeldStickThroughTheHitStopsTheReboundForTheShortestFrames)
{
    NS::Scene scene;
    PlayerActor* player = PlaceFloatingPlayer(scene);
    ASSERT_NE(player, nullptr);
    AddFrontWall(scene);
    const int shortest = ParamsFields(player).at("反動の止めの最短のフレーム数").get<int>();

    MoveStick(player, NS::Vector3{0.0f, 0.0f, 1.0f}, 1.0f);
    player->Update(true);
    for (int frame = 0; frame < 30 && !player->IsDashRebounding(); ++frame)
    {
        player->Update(false);
    }
    ASSERT_TRUE(player->IsDashRebounding());

    EXPECT_EQ(CountReboundStopFrames(player, false), shortest);
}

// 最短が 1 で当たったフレームの後に止めが残らなくても、スティックを動かせば上限まで止める
TEST(PlayerAirDashTest, MovingStickExtendsTheReboundStopEvenWhenTheShortestIsOne)
{
    NS::Scene scene;
    PlayerActor* player = PlaceFloatingPlayer(scene);
    ASSERT_NE(player, nullptr);
    AddFrontWall(scene);
    const int longest = 12;
    ASSERT_EQ(NS::ApplyJsonFields(&player->Params(),
                                  nlohmann::json{{"反動の止めの最短のフレーム数", 1},
                                                 {"反動の止めの上限のフレーム数", longest}}),
              0u);
    ASSERT_TRUE(DashIntoFrontWall(player));
    ASSERT_FALSE(player->IsHitStopping());

    // 当たったフレームの後は止まっていないので、動かした最初のフレームから止める。止めた数は当たったフレームを入れて上限まで
    int stopped = 1;
    for (int frame = 0; frame < 60; ++frame)
    {
        NS::Vector3 tilt{1.0f, 0.0f, 0.0f};
        if (frame % 2 == 0)
        {
            tilt = NS::Vector3{0.8f, 0.0f, 0.6f};
        }
        MoveStick(player, tilt, 1.0f);
        const NS::Vector3 before = player->TransformObj().Position();
        player->Update(false);
        if (player->TransformObj().Position() != before)
        {
            break;
        }
        ++stopped;
    }
    EXPECT_EQ(stopped, longest);
}

// 当たったフレームの束は当て方だけで選ぶ。浅く当てると弱い束で始まり、明けで入力が合うと強い束に替わる
TEST(PlayerAirDashTest, MatchingAtTheReleaseTurnsTheWeakReactionIntoTheFullOne)
{
    // 一番後に始めた残像の長さ。強い束は弱い束より長い
    const auto ghostLength = [](PlayerActor* player) {
        int length = 0;
        for (const NS::HitOwnerRun& run : player->HitReactionClassObj()->OwnerRuns())
        {
            if (run.item.name == "TackleGhost")
            {
                length = run.length;
            }
        }
        return length;
    };
    NS::Scene scene;
    PlayerActor* player = PlaceFloatingPlayer(scene);
    ASSERT_NE(player, nullptr);
    // 面の向きを縦の軸のまわりに 40 度回した壁。正面から +Z へ当てると当て方は cos 40° になる
    const NS::Quaternion turn = NS::Quaternion::CreateFromAxisAngle(NS::Vector3::UnitY, NS::k_Pi * 40.0f / 180.0f);
    scene.Physics().AddBox(NS::MakeOBB(NS::Vector3{0.0f, 5.0f, 3.5f}, turn, NS::Vector3{20.0f, 20.0f, 0.5f}),
                           NS::Phys::k_TerrainObjectLayer);
    const float full = ParamsFields(player).at("全部返ったに数える当て方の下限").get<float>();
    ASSERT_TRUE(DashIntoFrontWall(player));
    ASSERT_LT(player->AirDash().LastRebound().facing, full);
    const int weak = ghostLength(player);
    ASSERT_GT(weak, 0);

    // 返る向きの水平の向きへ倒すと、画面で同じ向きに見えるので合う
    NS::Vector3 assigned = player->AirDash().LastRebound().assigned;
    assigned.y = 0.0f;
    ASSERT_TRUE(NS::HasPositiveMagnitude(assigned.Length()));
    assigned.Normalize();
    MoveStick(player, assigned, 1.0f);
    for (int frame = 0; frame < 60 && player->IsHitStopping(); ++frame)
    {
        player->Update(false);
    }
    player->Update(false);
    ASSERT_EQ(player->AirDash().LastRebound().input, GL::Level::ReboundInputResult::Matched);
    EXPECT_GT(ghostLength(player), weak);
}

// 登れない 45° の坂から離れる向きへ曲げた反動は、最初の移動で記録の向きと速さのまま飛ぶ。
// 前は急な坂に触れている間の処理が水平に坂へ向かう分を消し、奥へ進まず遅くなった
TEST(PlayerAirDashTest, ReboundLeavingASteepSlopeKeepsTheChosenVelocity)
{
    NS::Scene scene;
    nlohmann::json doc = NS::MakeSceneJson();
    AddActorJson(doc, "PlayerActor", 1);
    scene.ResetFromJson(doc);
    // 実機と同じく後ろから約 15° 見下ろす。水平に見ると、この当て方は入力が合ったとみなされて曲がらない
    ASSERT_NE(PlaceViewCamera(scene, NS::Vector3{0.0f, 6.34f, -5.0f}, NS::Vector3{0.0f, 5.0f, 0.0f}), nullptr);
    PlayerActor* player = static_cast<PlayerActor*>(scene.Actors().FindByActorId(1));
    ASSERT_NE(player, nullptr);
    player->TransformObj().SetPosition(NS::Vector3{0.0f, 5.0f, 0.0f});
    // 表から選んだ向きへ、明けのフレームに向く事を見る
    ASSERT_EQ(NS::ApplyJsonFields(&player->Params(), nlohmann::json{{"反動の向きを反射から決める", false}}), 0u);
    const NS::Quaternion tilt = NS::Quaternion::CreateFromAxisAngle(NS::Vector3::UnitX, -NS::k_Pi / 4.0f);
    scene.Physics().AddBox(NS::MakeOBB(NS::Vector3{0.0f, 5.0f, 4.5f}, tilt, NS::Vector3{20.0f, 0.5f, 20.0f}),
                           NS::Phys::k_TerrainObjectLayer);

    const float yaw = NS::k_Pi * 25.0f / 180.0f;
    MoveStick(player, NS::Vector3{std::sin(yaw), 0.0f, std::cos(yaw)}, 1.0f);
    player->Update(true);
    for (int frame = 0; frame < 30 && !player->IsDashRebounding(); ++frame)
    {
        player->Update(true);
    }
    ASSERT_TRUE(player->IsDashRebounding());
    for (int frame = 0; frame < 60 && player->IsHitStopping(); ++frame)
    {
        player->Update(false);
    }
    player->Update(false);

    const GL::Player::TerrainReboundRecord& hit = player->AirDash().LastRebound();
    ASSERT_EQ(hit.input, GL::Level::ReboundInputResult::Shifted);
    ASSERT_GT(hit.shifted.z, 0.1f);
    const NS::Vector3 velocity = player->Body().Velocity();
    EXPECT_NEAR(velocity.x, hit.shifted.x * hit.speed, 0.01f);
    EXPECT_NEAR(velocity.y, hit.shifted.y * hit.speed, 0.01f);
    EXPECT_NEAR(velocity.z, hit.shifted.z * hit.speed, 0.01f);
}

// 止めの間に動かして最後に倒した向きを、明けのフレームに選ぶ
TEST(PlayerAirDashTest, TiltDuringTheStopShiftsTheReboundAtTheRelease)
{
    NS::Scene scene;
    PlayerActor* player = PlaceFloatingPlayer(scene);
    ASSERT_NE(player, nullptr);
    AddFrontWall(scene);
    // 動かしている 6 フレームの間、止めが続くようにする
    ASSERT_EQ(NS::ApplyJsonFields(&player->Params(), nlohmann::json{{"反動の止めの上限のフレーム数", 12}}), 0u);
    ASSERT_TRUE(DashIntoFrontWall(player));
    const NS::Vector3 before = player->Body().Velocity();

    // 右から前へ少しずつ回す。どのフレームも動いているとみなす変化より大きく動く
    for (int frame = 0; frame < 6; ++frame)
    {
        const float angle = NS::k_Pi * 0.5f * static_cast<float>(frame + 1) / 6.0f;
        MoveStick(player, NS::Vector3{std::cos(angle), 0.0f, std::sin(angle)}, 1.0f);
        player->Update(false);
        ASSERT_TRUE(player->IsHitStopping());
        // 止めの間は向きを選ばないので、速度は当たったフレームのまま
        EXPECT_EQ(player->Body().Velocity(), before);
    }
    for (int frame = 0; frame < 60 && player->IsHitStopping(); ++frame)
    {
        player->Update(false);
    }
    player->Update(false);
    const NS::Vector3 shifted = player->AirDash().LastRebound().shifted;
    EXPECT_NEAR(shifted.y, std::sqrt(0.5f), 1.0e-3f);
    EXPECT_NEAR(shifted.z, -std::sqrt(0.5f), 1.0e-3f);
    EXPECT_GT(player->Body().Velocity().y, 0.0f);
}

// 押したフレームに丸まる。立ち姿のままダッシュすると、飛び出した瞬間に玉に見えない
TEST(PlayerAirDashTest, PressFrameCurlsIntoTheBall)
{
    NS::Scene scene;
    PlayerActor* player = PlaceFloatingPlayer(scene);
    ASSERT_NE(player, nullptr);
    ASSERT_FALSE(player->IsCurled());

    MoveStick(player, NS::Vector3{1.0f, 0.0f, 0.0f}, 1.0f);
    player->Update(true);
    ASSERT_TRUE(player->IsAirDashing());
    EXPECT_TRUE(player->IsCurled());
}

TEST(PlayerAirDashTest, PressInTheAirKeepsTheBallAtTheBodyCenter)
{
    NS::Scene scene;
    PlayerActor* player = PlaceFloatingPlayer(scene);
    ASSERT_NE(player, nullptr);
    ASSERT_FALSE(player->Body().IsGrounded());
    const float before = player->TransformObj().Position().y;

    MoveStick(player, NS::Vector3{1.0f, 0.0f, 0.0f}, 1.0f);
    player->Update(true);
    ASSERT_TRUE(player->IsAirDashing());
    ASSERT_TRUE(player->IsCurled());
    EXPECT_NEAR(player->TransformObj().Position().y, before, 1.0e-3f);
}

// ダッシュの間も地形の反動の間も、玉は今の進む向きへ前転し、進んだ距離に合う角度だけ回る
TEST(PlayerAirDashTest, BallRollsTowardTheVelocityDuringTheDashAndTheRebound)
{
    NS::Scene scene;
    PlayerActor* player = PlaceFloatingPlayer(scene);
    ASSERT_NE(player, nullptr);
    AddFrontWall(scene);
    const float degreesPerMeter =
        NS::SerializeClassObjFields(&player->Appearance()).at("エアダッシュの 1 m で回る角度").get<float>();
    const float dt = NS::OS::FrameTimer::FixedDelta();

    MoveStick(player, NS::Vector3{0.0f, 0.0f, 1.0f}, 1.0f);
    player->Update(true);
    MoveStick(player, NS::Vector3{}, 0.0f);
    player->Update(false);
    ASSERT_TRUE(player->IsAirDashing());
    // +Z へ進む前転の軸は 上 × 前 = (1, 0, 0)
    const NS::Vector3 dashAxis = player->Appearance().SpinAxis();
    EXPECT_NEAR(dashAxis.x, 1.0f, 1.0e-3f);
    EXPECT_NEAR(dashAxis.z, 0.0f, 1.0e-3f);
    EXPECT_NEAR(player->Appearance().SpinDegreesThisFrame(),
                player->Body().Velocity().Length() * degreesPerMeter * dt,
                1.0e-2f);

    // 返った後、ヒットストップが明けたら返った向き (−Z) へ回る
    for (int frame = 0; frame < 60 && !(player->IsDashRebounding() && player->Motion().canMoveBody); ++frame)
    {
        player->Update(false);
    }
    ASSERT_TRUE(player->IsDashRebounding());
    player->Update(false);
    ASSERT_TRUE(player->IsDashRebounding());
    const NS::Vector3 reboundAxis = player->Appearance().SpinAxis();
    EXPECT_NEAR(reboundAxis.x, -1.0f, 1.0e-3f);
    EXPECT_NEAR(reboundAxis.z, 0.0f, 1.0e-3f);
    EXPECT_NEAR(player->Appearance().SpinDegreesThisFrame(),
                player->Body().Velocity().Length() * degreesPerMeter * dt,
                1.0e-2f);
}

// ダッシュが終わっても宙の間は玉のまま。着地して落ち着いたら立ち姿へ戻る
TEST(PlayerAirDashTest, BallStaysCurledInTheAirAndUncurlsAfterLanding)
{
    NS::Scene scene;
    PlayerActor* player = PlaceFloatingPlayer(scene);
    ASSERT_NE(player, nullptr);
    AddBox(scene, NS::Vector3{0.0f, -0.5f, 0.0f}, NS::Vector3{50.0f, 0.5f, 50.0f});

    MoveStick(player, NS::Vector3{1.0f, 0.0f, 0.0f}, 1.0f);
    player->Update(true);
    MoveStick(player, NS::Vector3{}, 0.0f);
    for (int frame = 0; frame < 60 && player->IsAirDashing(); ++frame)
    {
        player->Update(false);
    }
    ASSERT_FALSE(player->IsAirDashing());
    ASSERT_FALSE(player->Body().IsGrounded());
    EXPECT_TRUE(player->IsCurled());

    for (int frame = 0; frame < 240 && !player->Body().IsGrounded(); ++frame)
    {
        player->Update(false);
    }
    ASSERT_TRUE(player->Body().IsGrounded());
    player->Update(false);
    EXPECT_FALSE(player->IsCurled());
}

TEST(PlayerAirDashTest, ResetStateEndsTheReboundAndRestoresTheDash)
{
    NS::Scene scene;
    PlayerActor* player = PlaceFloatingPlayer(scene);
    ASSERT_NE(player, nullptr);
    AddFrontWall(scene);
    ASSERT_TRUE(DashIntoFrontWall(player));

    player->ResetState();
    EXPECT_FALSE(player->IsDashRebounding());
    EXPECT_FALSE(player->IsAirDashing());
    EXPECT_EQ(player->AirDash().Remaining(), 1);
}

TEST(PlayerAirDashTest, AirControlComesBackGraduallyAfterTheDash)
{
    NS::Scene scene;
    PlayerActor* player = PlaceFloatingPlayer(scene);
    ASSERT_NE(player, nullptr);
    const nlohmann::json fields = ParamsFields(player);
    const float fullDrag = fields.at("曲がる時の抵抗").get<float>() * NS::OS::FrameTimer::FixedDelta();

    MoveStick(player, NS::Vector3{0.0f, 0.0f, 1.0f}, 1.0f);
    player->Update(true);
    MoveStick(player, NS::Vector3{}, 0.0f);
    for (int frame = 0; frame < 60 && player->IsAirDashing(); ++frame)
    {
        player->Update(false);
    }
    ASSERT_FALSE(player->IsAirDashing());

    MoveStick(player, NS::Vector3{1.0f, 0.0f, 0.0f}, 1.0f);
    float before = player->Body().Velocity().z;
    player->Update(false);
    const float first = before - player->Body().Velocity().z;
    before = player->Body().Velocity().z;
    for (int frame = 0; frame < 4; ++frame)
    {
        before = player->Body().Velocity().z;
        player->Update(false);
    }
    const float later = before - player->Body().Velocity().z;

    EXPECT_LT(first, fullDrag * 0.25f);
    EXPECT_GT(later, first);
    EXPECT_LE(later, fullDrag + 1.0e-4f);
}

TEST(PlayerAirDashTest, PlainFallHasFullAirControlFromTheFirstFrame)
{
    NS::Scene scene;
    PlayerActor* player = PlaceFloatingPlayer(scene);
    ASSERT_NE(player, nullptr);
    const float full = ParamsFields(player).at("空中の加速度").get<float>() * NS::OS::FrameTimer::FixedDelta();
    player->Update(false);
    player->Update(false);

    MoveStick(player, NS::Vector3{1.0f, 0.0f, 0.0f}, 1.0f);
    const float before = player->Body().Velocity().x;
    player->Update(false);
    EXPECT_NEAR(player->Body().Velocity().x - before, full, 1.0e-3f);
}

// 空中の操作は、反動の入力と同じ数の向きへスティックをそろえて加速する。33 度の入力は 16 方向の 22.5 度になる
TEST(PlayerAirDashTest, AirControlSnapsTheStickToTheReboundStickDivisions)
{
    NS::Scene scene;
    PlayerActor* player = PlaceFloatingPlayer(scene);
    ASSERT_NE(player, nullptr);
    ASSERT_EQ(ParamsFields(player).at("反動の入力を読む向きの数").get<int>(), 16);
    player->Update(false);
    player->Update(false);

    const float tilt = NS::ToRadians(NS::Degrees{33.0f}).value;
    MoveStick(player, NS::Vector3{std::sin(tilt), 0.0f, std::cos(tilt)}, 1.0f);
    const NS::Vector3 before = player->Body().Velocity();
    player->Update(false);
    const NS::Vector3 added = player->Body().Velocity() - before;
    const float addedDegrees = NS::ToDegrees(NS::Radians{std::atan2(added.x, added.z)}).value;
    EXPECT_NEAR(addedDegrees, 22.5f, 0.1f);
}

// 空中の操作は、カメラで世界へ回した後の向きをそろえる。カメラが 10 度回った時に真上へ倒すと、世界の 0 度へ進む
TEST(PlayerAirDashTest, AirControlSnapsInTheWorldWhateverTheCameraYaw)
{
    NS::Scene scene;
    PlayerActor* player = PlaceFloatingPlayer(scene);
    ASSERT_NE(player, nullptr);
    player->Update(false);
    player->Update(false);

    const float camera = NS::ToRadians(NS::Degrees{10.0f}).value;
    player->Input().SetCameraForward(NS::Vector3{std::sin(camera), 0.0f, std::cos(camera)});
    player->Input().SetStick(0.0f, 1.0f);
    const NS::Vector3 before = player->Body().Velocity();
    player->Update(false);
    const NS::Vector3 added = player->Body().Velocity() - before;
    const float addedDegrees = NS::ToDegrees(NS::Radians{std::atan2(added.x, added.z)}).value;
    EXPECT_NEAR(addedDegrees, 0.0f, 0.1f);
}

TEST(PlayerAirDashTest, WallAheadShowsTheBounceArrowBeforeTheDash)
{
    // 押す前から、倒している向きの壁に当たる点へ、当て方の向きと曲げられる先の矢印を出す
    NS::Scene scene;
    PlayerActor* player = PlaceFloatingPlayer(scene);
    ASSERT_NE(player, nullptr);
    AddFrontWall(scene);
    MoveStick(player, NS::Vector3{0.0f, 0.0f, 1.0f}, 1.0f);
    player->Update(false);
    ASSERT_FALSE(player->IsAirDashing());

    const GL::Level::ReboundPreviewArrows* arrows = player->ReboundIndicator().ShownArrows();
    ASSERT_NE(arrows, nullptr);
    EXPECT_NEAR(arrows->origin.z, 3.0f, 1.0e-2f);
    EXPECT_NEAR(arrows->assigned.z, -1.0f, 1.0e-3f) << "正面の壁は真後ろへ返る";
    EXPECT_FALSE(arrows->choices.empty());
    for (const NS::Vector3& choice : arrows->choices)
    {
        EXPECT_LT(choice.z, 0.0f) << "曲げられる先も壁から離れる";
    }
}

TEST(PlayerAirDashTest, NoWallWithinTheDashDistanceHidesTheBounceArrow)
{
    NS::Scene scene;
    PlayerActor* player = PlaceFloatingPlayer(scene);
    ASSERT_NE(player, nullptr);
    AddFrontWall(scene);
    // 壁と逆の -Z へ倒す
    MoveStick(player, NS::Vector3{0.0f, 0.0f, -1.0f}, 1.0f);
    player->Update(false);
    EXPECT_EQ(player->ReboundIndicator().ShownArrows(), nullptr);
}

TEST(PlayerAirDashTest, BounceArrowFollowsTheDashDirectionDuringTheDash)
{
    // 出した後は、倒し直しても出た向きの先の壁を指し続ける
    NS::Scene scene;
    PlayerActor* player = PlaceFloatingPlayer(scene);
    ASSERT_NE(player, nullptr);
    AddFrontWall(scene);
    MoveStick(player, NS::Vector3{0.0f, 0.0f, 1.0f}, 1.0f);
    player->Update(true);
    ASSERT_TRUE(player->IsAirDashing());
    MoveStick(player, NS::Vector3{0.0f, 0.0f, -1.0f}, 1.0f);
    player->Update(true);
    ASSERT_TRUE(player->IsAirDashing());
    const GL::Level::ReboundPreviewArrows* arrows = player->ReboundIndicator().ShownArrows();
    ASSERT_NE(arrows, nullptr);
    EXPECT_NEAR(arrows->origin.z, 3.0f, 1.0e-2f);
}

TEST(PlayerAirDashTest, BounceArrowFollowsTheVelocityWhileTheDashSlidesAlongAWall)
{
    // ダッシュの間は自機の速度の向きの先を調べる。反動にならない浅い角度で横の壁に沿って滑る間は、
    // 出た向きでなく、滑って進む向き (+Z) の先の正面の壁を指す
    NS::Scene scene;
    PlayerActor* player = PlaceFloatingPlayer(scene);
    ASSERT_NE(player, nullptr);
    AddFrontWall(scene);
    const float radius = player->Collider().CapsuleRadius();
    const float sideFace = radius + 0.05f;
    AddBox(scene, NS::Vector3{sideFace + 0.5f, 5.0f, 0.0f}, NS::Vector3{0.5f, 20.0f, 20.0f});
    const float angle = NS::ToRadians(NS::Degrees{8.0f}).value;
    MoveStick(player, NS::Vector3{std::sin(angle), 0.0f, std::cos(angle)}, 1.0f);
    player->Update(true);
    ASSERT_TRUE(player->IsAirDashing());

    for (int frame = 0; frame < 30 && player->IsAirDashing() && player->TransformObj().Position().z < 1.0f; ++frame)
    {
        player->Update(true);
    }
    ASSERT_TRUE(player->IsAirDashing());
    const NS::Vector3 position = player->TransformObj().Position();
    ASSERT_NEAR(position.x, sideFace - radius, 2.0e-2f) << "横の壁に沿って滑っている";
    ASSERT_NEAR(player->Body().Velocity().x, 0.0f, 1.0e-2f) << "滑る間の速度は壁に沿う";

    const GL::Level::ReboundPreviewArrows* arrows = player->ReboundIndicator().ShownArrows();
    ASSERT_NE(arrows, nullptr);
    EXPECT_NEAR(arrows->origin.z, 3.0f, 1.0e-2f);
    EXPECT_NEAR(arrows->origin.x, position.x, 5.0e-2f) << "出た向きのままなら、横へずれた所を指す";
}

TEST(PlayerAirDashTest, WallBeyondTheDashReachShowsNoArrowBeforeOrDuringTheDash)
{
    // 欄「エアダッシュの距離」の 1.5 倍先の壁には届かず反動にならないので、押す前もダッシュの間も矢印を出さない。
    // ダッシュの間は残りの距離で調べる。全部の距離で調べると、半ばを過ぎた所から届かない壁に矢印が出る
    NS::Scene scene;
    PlayerActor* player = PlaceFloatingPlayer(scene);
    ASSERT_NE(player, nullptr);
    const float distance = ParamsFields(player).at("エアダッシュの距離").get<float>();
    const float wallFace = player->TransformObj().Position().z + distance * 1.5f;
    AddBox(scene, NS::Vector3{0.0f, 5.0f, wallFace + 0.5f}, NS::Vector3{20.0f, 20.0f, 0.5f});

    MoveStick(player, NS::Vector3{0.0f, 0.0f, 1.0f}, 1.0f);
    player->Update(false);
    EXPECT_EQ(player->ReboundIndicator().ShownArrows(), nullptr) << "押す前";
    player->Update(true);
    ASSERT_TRUE(player->IsAirDashing());
    int frame = 0;
    while (player->IsAirDashing() && frame < AirDashFrames(player) + 1)
    {
        EXPECT_EQ(player->ReboundIndicator().ShownArrows(), nullptr) << "ダッシュの " << frame << " フレーム目";
        player->Update(true);
        ++frame;
    }
}

TEST(PlayerAirDashTest, ArrowStaysThroughTheReboundStopUntilTheDirectionIsChosen)
{
    // 止めの間はスティックで向きを選ぶ時なので、当たった面の矢印を出し続ける。明けで向きを選んだら消す
    NS::Scene scene;
    PlayerActor* player = PlaceFloatingPlayer(scene);
    ASSERT_NE(player, nullptr);
    AddFrontWall(scene);
    ASSERT_TRUE(DashIntoFrontWall(player));

    const GL::Level::ReboundPreviewArrows* arrows = player->ReboundIndicator().ShownArrows();
    ASSERT_NE(arrows, nullptr) << "当たったフレーム";
    const NS::Vector3 assigned = player->AirDash().LastRebound().assigned;
    EXPECT_NEAR(arrows->assigned.x, assigned.x, 1.0e-3f);
    EXPECT_NEAR(arrows->assigned.z, assigned.z, 1.0e-3f);
    EXPECT_NEAR(arrows->origin.z, 3.0f, 1.0e-1f);
    EXPECT_FALSE(arrows->choices.empty());

    // 当たったフレームを入れて、止めの最短のフレーム数の間は出し続ける
    const int stopFrames = ParamsFields(player).at("反動の止めの最短のフレーム数").get<int>();
    int shownFrames = 1;
    while (shownFrames < 30)
    {
        player->Update(true);
        if (player->ReboundIndicator().ShownArrows() == nullptr)
        {
            break;
        }
        ++shownFrames;
    }
    EXPECT_GE(shownFrames, stopFrames);
    EXPECT_LT(shownFrames, 30) << "向きを選んだ後も消えない";
    EXPECT_TRUE(player->IsDashRebounding());
    EXPECT_LT(player->Body().Velocity().z, 0.0f) << "消えた時には壁から離れて飛んでいる";
}

namespace
{
    // 表の向きへのそろえと、スティックの向きのそろえを切る。反射と曲げの計算だけを見るテストで使う
    void TurnOffReboundSnapping(PlayerActor* player)
    {
        EXPECT_EQ(NS::ApplyJsonFields(&player->Params(),
                                      nlohmann::json{{"反動を表の向きへそろえる角度", 0.0f},
                                                     {"反動を表の向きへ寄せ始める角度", 0.0f},
                                                     {"反動の入力を読む向きの数", 0}}),
                  0u);
    }

    // 欄「反動の向きを反射から決める」を真にし、欄「反動を入力の向きへ曲げ終えるフレーム数」を書く。そろえは切る
    void UseReflectedRebound(PlayerActor* player, int bendFrames)
    {
        ASSERT_EQ(NS::ApplyJsonFields(&player->Params(),
                                      nlohmann::json{{"反動の向きを反射から決める", true},
                                                     {"反動を入力の向きへ曲げ終えるフレーム数", bendFrames}}),
                  0u);
        TurnOffReboundSnapping(player);
    }

    // 止めが明けて、向きを選ぶフレームまで進める
    void StepToTheReboundRelease(PlayerActor* player)
    {
        for (int frame = 0; frame < 60 && player->IsHitStopping(); ++frame)
        {
            player->Update(false);
        }
        player->Update(false);
    }

    // 止めの間に、ダッシュのボタンを放してから押し直す。止めは当たったフレームの後に 2 フレーム続く
    void PressInTheReboundStop(PlayerActor* player)
    {
        player->Update(false);
        player->Update(true);
    }

    // 反射から決める方式で、前の壁へ +Z にダッシュしてから反動へ移るまでの Update の回数
    int FramesToTheFrontWall()
    {
        NS::Scene scene;
        PlayerActor* player = PlaceFloatingPlayer(scene);
        AddFrontWall(scene);
        UseReflectedRebound(player, 0);
        MoveStick(player, NS::Vector3{0.0f, 0.0f, 1.0f}, 1.0f);
        player->Update(true);
        MoveStick(player, NS::Vector3{}, 0.0f);
        int frames = 0;
        while (!player->IsDashRebounding() && frames < 60)
        {
            player->Update(false);
            ++frames;
        }
        return frames;
    }

    // 前の壁へ +Z にダッシュし、ボタンを放して stick を倒したまま当てる。当たる pressLead フレーム前にボタンを押し直す。
    // pressLead が負なら押し直さない。hitFrame は FramesToTheFrontWall の値。反動へ移ったら true
    bool DashIntoFrontWallPressing(PlayerActor* player, int hitFrame, int pressLead, const NS::Vector3& stick)
    {
        MoveStick(player, NS::Vector3{0.0f, 0.0f, 1.0f}, 1.0f);
        player->Update(true);
        float scale = 0.0f;
        if (NS::HasPositiveMagnitude(stick.Length()))
        {
            scale = 1.0f;
        }
        MoveStick(player, stick, scale);
        for (int frame = 1; frame <= hitFrame; ++frame)
        {
            player->Update(pressLead >= 0 && frame == hitFrame - pressLead);
        }
        return player->IsDashRebounding();
    }

    // 一番後に始めた残像の長さ。強い束は弱い束より長い
    int LastGhostLength(const PlayerActor* player)
    {
        int length = 0;
        for (const NS::HitOwnerRun& run : player->HitReactionClassObj()->OwnerRuns())
        {
            if (run.item.name == "TackleGhost")
            {
                length = run.length;
            }
        }
        return length;
    }

    // 反射から決める方式で、前の壁へ正面から当て、止めの間に stick を倒して押した時に選ばれる向き。そろえは既定のまま
    NS::Vector3 SnappedReboundChoice(const NS::Vector3& stick)
    {
        NS::Scene scene;
        PlayerActor* player = PlaceFloatingPlayer(scene);
        AddFrontWall(scene);
        EXPECT_EQ(NS::ApplyJsonFields(&player->Params(), nlohmann::json{{"反動の向きを反射から決める", true}}), 0u);
        EXPECT_TRUE(DashIntoFrontWall(player));
        MoveStick(player, stick, 1.0f);
        PressInTheReboundStop(player);
        StepToTheReboundRelease(player);
        return player->AirDash().LastRebound().shifted;
    }

    // 前の壁へ水平に yawDegrees だけ斜めに当てた時の、入力補正を掛ける前の返る向き
    NS::Vector3 ObliqueReboundBase(bool usesReflection, float yawDegrees, bool snaps)
    {
        NS::Scene scene;
        PlayerActor* player = PlaceFloatingPlayer(scene);
        AddFrontWall(scene);
        EXPECT_EQ(
            NS::ApplyJsonFields(&player->Params(), nlohmann::json{{"反動の向きを反射から決める", usesReflection}}), 0u);
        if (!snaps)
        {
            TurnOffReboundSnapping(player);
        }
        const float yaw = NS::k_Pi * yawDegrees / 180.0f;
        MoveStick(player, NS::Vector3{std::sin(yaw), 0.0f, std::cos(yaw)}, 1.0f);
        player->Update(true);
        MoveStick(player, NS::Vector3{}, 0.0f);
        for (int frame = 0; frame < 30 && !player->IsDashRebounding(); ++frame)
        {
            player->Update(false);
        }
        EXPECT_TRUE(player->IsDashRebounding());
        NS::Vector3 direction = player->Body().Velocity();
        direction.Normalize();
        return direction;
    }

    // 反射から決める方式で前の壁へ正面から当て、カメラを cameraDegrees 回してから、画面の stickDegrees へ倒して押した時に
    // 選ばれる向き。表へのそろえは切り、スティックの向きの数は既定のまま
    NS::Vector3 ReboundChoiceUnderCamera(float cameraDegrees, float stickDegrees)
    {
        NS::Scene scene;
        PlayerActor* player = PlaceFloatingPlayer(scene);
        AddFrontWall(scene);
        EXPECT_EQ(NS::ApplyJsonFields(&player->Params(),
                                      nlohmann::json{{"反動の向きを反射から決める", true},
                                                     {"反動を表の向きへそろえる角度", 0.0f},
                                                     {"反動を表の向きへ寄せ始める角度", 0.0f}}),
                  0u);
        EXPECT_TRUE(DashIntoFrontWall(player));
        const float camera = NS::ToRadians(NS::Degrees{cameraDegrees}).value;
        player->Input().SetCameraForward(NS::Vector3{std::sin(camera), 0.0f, std::cos(camera)});
        const float stick = NS::ToRadians(NS::Degrees{stickDegrees}).value;
        player->Input().SetStick(std::sin(stick), std::cos(stick));
        PressInTheReboundStop(player);
        StepToTheReboundRelease(player);
        return player->AirDash().LastRebound().shifted;
    }
} // namespace

// 押して曲げる向きは、倒した世界の向きだけで決まる。世界の 150 度へ、カメラ 10 度で画面の 140 度へ倒しても、
// カメラ 0 度で画面の 150 度へ倒しても、同じ向きへ曲がる
TEST(PlayerAirDashTest, ReflectedReboundBendsTheSameWayWhateverTheCameraYaw)
{
    const NS::Vector3 turned = ReboundChoiceUnderCamera(10.0f, 140.0f);
    const NS::Vector3 straight = ReboundChoiceUnderCamera(0.0f, 150.0f);
    EXPECT_GT(straight.x, 1.0e-2f) << "倒した右後ろの側へ曲がる";
    EXPECT_NEAR(turned.x, straight.x, 1.0e-3f);
    EXPECT_NEAR(turned.y, straight.y, 1.0e-3f);
    EXPECT_NEAR(turned.z, straight.z, 1.0e-3f);
}

// 反射から決める時は、当てた角度のまま返る。表から決める時は、同じ当て方が真後ろへ丸まる
TEST(PlayerAirDashTest, ReflectedReboundKeepsTheHitAngleWhileTheTableRoundsIt)
{
    const float yaw = NS::k_Pi * 20.0f / 180.0f;
    const NS::Vector3 reflected = ObliqueReboundBase(true, 20.0f, false);
    EXPECT_NEAR(reflected.x, std::sin(yaw), 1.0e-3f);
    EXPECT_NEAR(reflected.y, 0.0f, 1.0e-3f);
    EXPECT_NEAR(reflected.z, -std::cos(yaw), 1.0e-3f);

    const NS::Vector3 rounded = ObliqueReboundBase(false, 20.0f, false);
    EXPECT_NEAR(rounded.x, 0.0f, 1.0e-3f);
    EXPECT_NEAR(rounded.z, -1.0f, 1.0e-3f);
}

// 既定の調整値では、前の壁へ 5° 斜めに当てても、正面から当てた時と同じ真後ろへ返る
TEST(PlayerAirDashTest, ReflectedReboundSnapsASlightlyObliqueHitToStraightBack)
{
    const NS::Vector3 snapped = ObliqueReboundBase(true, 5.0f, true);
    EXPECT_NEAR(snapped.x, 0.0f, 1.0e-3f);
    EXPECT_NEAR(snapped.y, 0.0f, 1.0e-3f);
    EXPECT_NEAR(snapped.z, -1.0f, 1.0e-3f);
}

// 既定の調整値では、そろう範囲の外 (前の壁へ 16° 斜め) に当てると、真後ろと当てた角度の間の向きへ返る
TEST(PlayerAirDashTest, ReflectedReboundPullsAHitOutsideTheFullAnglePartWayToTheTableDirection)
{
    const float yaw = NS::k_Pi * 16.0f / 180.0f;
    const float margin = NS::k_Pi * 2.0f / 180.0f;
    const NS::Vector3 pulled = ObliqueReboundBase(true, 16.0f, true);
    const float returned = std::atan2(pulled.x, -pulled.z);
    EXPECT_GT(returned, margin);
    EXPECT_LT(returned, yaw - margin);
    EXPECT_NEAR(pulled.y, 0.0f, 1.0e-3f);
}

// 既定の調整値では、右から 10° ずれたスティックでも、真右を倒した時と同じ向きが選ばれる
TEST(PlayerAirDashTest, ReflectedReboundReadsASlightlyOffStickAsTheNearestDirection)
{
    const float off = NS::k_Pi * 10.0f / 180.0f;
    const NS::Vector3 exact = SnappedReboundChoice(NS::Vector3{1.0f, 0.0f, 0.0f});
    const NS::Vector3 slightlyOff = SnappedReboundChoice(NS::Vector3{std::cos(off), 0.0f, std::sin(off)});
    EXPECT_GT(exact.x, 0.1f);
    EXPECT_NEAR(AngleBetween(exact, slightlyOff), 0.0f, 1.0e-3f);
}

// 止めの明けは返る向きの近くへ飛び出し、曲げ終えるフレーム数を掛けて、入力で曲げた向きまで毎フレーム回る
TEST(PlayerAirDashTest, ReflectedReboundTurnsTowardTheStickOverTheBendFrames)
{
    NS::Scene scene;
    PlayerActor* player = PlaceFloatingPlayer(scene);
    ASSERT_NE(player, nullptr);
    AddFrontWall(scene);
    const nlohmann::json fields = ParamsFields(player);
    const int bendFrames = fields.at("反動を入力の向きへ曲げ終えるフレーム数").get<int>();
    ASSERT_GT(bendFrames, 1);
    ASSERT_LE(bendFrames, fields.at("反動の操作なしのフレーム数").get<int>());
    UseReflectedRebound(player, bendFrames);
    ASSERT_TRUE(DashIntoFrontWall(player));

    MoveStick(player, NS::Vector3{1.0f, 0.0f, 0.0f}, 1.0f);
    PressInTheReboundStop(player);
    StepToTheReboundRelease(player);
    ASSERT_EQ(player->AirDash().ReboundFrames(), 1);
    const GL::Player::TerrainReboundRecord hit = player->AirDash().LastRebound();
    ASSERT_EQ(hit.input, GL::Level::ReboundInputResult::Shifted);
    EXPECT_NEAR(hit.assigned.z, -1.0f, 1.0e-3f);
    // 真後ろへ返る向きに、右の入力を強さの分だけ足した向き
    const float whole = std::atan(fields.at("入力で反動を曲げる強さ").get<float>());
    EXPECT_NEAR(AngleBetween(hit.assigned, hit.shifted), whole, 1.0e-3f);
    EXPECT_GT(hit.shifted.x, 0.0f);

    float previous = 0.0f;
    for (int frame = 1; frame <= bendFrames; ++frame)
    {
        const float turned = AngleBetween(hit.assigned, player->Body().Velocity());
        EXPECT_NEAR(turned, whole * static_cast<float>(frame) / static_cast<float>(bendFrames), 1.0e-3f)
            << "明けから " << frame << " フレーム目";
        EXPECT_GT(turned, previous);
        EXPECT_NEAR(player->Body().Velocity().Length(), hit.speed, 1.0e-2f);
        previous = turned;
        player->Update(false);
    }
    // 曲げ終えた後の操作なしの段では、もう回らない
    EXPECT_NEAR(AngleBetween(hit.shifted, player->Body().Velocity()), 0.0f, 1.0e-3f);
}

TEST(PlayerAirDashTest, ReflectedReboundWithZeroBendFramesTurnsAtTheRelease)
{
    NS::Scene scene;
    PlayerActor* player = PlaceFloatingPlayer(scene);
    ASSERT_NE(player, nullptr);
    AddFrontWall(scene);
    UseReflectedRebound(player, 0);
    ASSERT_TRUE(DashIntoFrontWall(player));

    MoveStick(player, NS::Vector3{1.0f, 0.0f, 0.0f}, 1.0f);
    PressInTheReboundStop(player);
    StepToTheReboundRelease(player);
    const GL::Player::TerrainReboundRecord& hit = player->AirDash().LastRebound();
    ASSERT_EQ(hit.input, GL::Level::ReboundInputResult::Shifted);
    EXPECT_NEAR(AngleBetween(hit.shifted, player->Body().Velocity()), 0.0f, 1.0e-3f);
}

// 反射から決める時は、ボタンを押し直さずに当てると失敗の反動になる。スティックを倒していても曲がらず、
// 欄「外した押しで返る速さの倍率」の速さで当て方の向きへ弾かれる
TEST(PlayerAirDashTest, ReflectedReboundWithoutAPressLosesTheSpeedAndDoesNotBend)
{
    const int hitFrame = FramesToTheFrontWall();
    NS::Scene scene;
    PlayerActor* player = PlaceFloatingPlayer(scene);
    ASSERT_NE(player, nullptr);
    AddFrontWall(scene);
    UseReflectedRebound(player, 0);
    const nlohmann::json fields = ParamsFields(player);
    const float full = DashSpeed(player) * fields.at("返る速さのダッシュの速さに対する倍率").get<float>();
    const float scale = fields.at("外した押しで返る速さの倍率").get<float>();
    ASSERT_LT(scale, 1.0f);
    ASSERT_TRUE(DashIntoFrontWallPressing(player, hitFrame, -1, NS::Vector3{1.0f, 0.0f, 0.0f}));
    StepToTheReboundRelease(player);
    const GL::Player::TerrainReboundRecord& hit = player->AirDash().LastRebound();
    EXPECT_EQ(hit.input, GL::Level::ReboundInputResult::Kept);
    // 明けのフレームから重力で落ちるので、水平の速度で見る。壁の当て方の向きは真後ろ
    EXPECT_NEAR(player->Body().Velocity().x, 0.0f, 1.0e-3f);
    EXPECT_NEAR(-player->Body().Velocity().z, full * scale, 1.0e-2f);
}

// 失敗の反動は、上へ返る面でも当て方の向きへ小さく跳ね返る。速度が 0 にならない
TEST(PlayerAirDashTest, ReflectedReboundWithoutAPressStillBouncesOffAFloor)
{
    NS::Scene scene;
    PlayerActor* player = PlaceFloatingPlayer(scene);
    ASSERT_NE(player, nullptr);
    UseReflectedRebound(player, 0);
    // 自機の前の下に、上を向いた面を斜め下へのダッシュで当てられないので、前の壁を 45 度上へ傾けた坂にする
    const NS::Quaternion tilt = NS::Quaternion::CreateFromAxisAngle(NS::Vector3::UnitX, NS::k_Pi * 45.0f / 180.0f);
    scene.Physics().AddBox(NS::MakeOBB(NS::Vector3{0.0f, 5.0f, 3.5f}, tilt, NS::Vector3{20.0f, 20.0f, 0.5f}),
                           NS::Phys::k_TerrainObjectLayer);
    ASSERT_TRUE(DashIntoFrontWall(player));
    ASSERT_GT(player->AirDash().LastRebound().assigned.y, 0.5f);
    StepToTheReboundRelease(player);
    EXPECT_GT(player->Body().Velocity().y, 1.0f);
}

// 当たる前の押しは、欄「反動の押しを受ける、当たる前のフレーム数」以内なら曲がり、それより 1 フレーム早いと曲がらない
TEST(PlayerAirDashTest, ReflectedReboundBendsOnlyWhenThePressIsWithinTheLeadFrames)
{
    const int hitFrame = FramesToTheFrontWall();
    // 壁が近いので、欄を 2 フレームにして、早すぎる押しもボタンを放した後のフレームに置けるようにする
    const int lead = 2;
    // 当たる pressLead フレーム前に押した時に、入力補正が向きをどうしたか
    const auto inputResult = [hitFrame, lead](int pressLead) {
        NS::Scene scene;
        PlayerActor* player = PlaceFloatingPlayer(scene);
        AddFrontWall(scene);
        UseReflectedRebound(player, 0);
        EXPECT_EQ(NS::ApplyJsonFields(&player->Params(),
                                      nlohmann::json{{"反動の押しを受ける、当たる前のフレーム数", lead}}),
                  0u);
        EXPECT_TRUE(DashIntoFrontWallPressing(player, hitFrame, pressLead, NS::Vector3{1.0f, 0.0f, 0.0f}));
        StepToTheReboundRelease(player);
        return player->AirDash().LastRebound().input;
    };
    // 押し直すフレームは、ボタンを放したフレーム (1) より後でないと押した瞬間にならない
    ASSERT_GE(hitFrame - (lead + 1), 2);
    EXPECT_EQ(inputResult(0), GL::Level::ReboundInputResult::Shifted);
    EXPECT_EQ(inputResult(lead), GL::Level::ReboundInputResult::Shifted);
    EXPECT_EQ(inputResult(lead + 1), GL::Level::ReboundInputResult::Kept);
}

// 1 回のダッシュで数える押しは最初の 1 回だけ。早すぎる押しの後は、止めの間に押し直しても曲がらない
TEST(PlayerAirDashTest, ReflectedReboundIgnoresASecondPressAfterAnEarlyOne)
{
    const int hitFrame = FramesToTheFrontWall();
    NS::Scene scene;
    PlayerActor* player = PlaceFloatingPlayer(scene);
    ASSERT_NE(player, nullptr);
    AddFrontWall(scene);
    UseReflectedRebound(player, 0);
    // 壁が近いので、欄を 2 フレームにして、ボタンを放した次のフレーム (2) の押しを早すぎる押しにする
    ASSERT_EQ(
        NS::ApplyJsonFields(&player->Params(), nlohmann::json{{"反動の押しを受ける、当たる前のフレーム数", 2}}), 0u);
    ASSERT_GT(hitFrame - 2, 2);
    ASSERT_TRUE(DashIntoFrontWallPressing(player, hitFrame, hitFrame - 2, NS::Vector3{1.0f, 0.0f, 0.0f}));
    PressInTheReboundStop(player);
    StepToTheReboundRelease(player);
    EXPECT_EQ(player->AirDash().LastRebound().input, GL::Level::ReboundInputResult::Kept);
}

// 押したのに時機が早すぎた反動は、欄「外した押しで返る速さの倍率」の速さで、上へは返らずに弾かれる。
// 着地するまで、補正の段に入らず、エアダッシュも戻らず、スティックで曲がらない
TEST(PlayerAirDashTest, ReflectedReboundAfterAnEarlyPressLosesTheSpeedAndTheControl)
{
    const int hitFrame = FramesToTheFrontWall();
    NS::Scene scene;
    PlayerActor* player = PlaceFloatingPlayer(scene);
    ASSERT_NE(player, nullptr);
    AddFrontWall(scene);
    UseReflectedRebound(player, 0);
    ASSERT_EQ(
        NS::ApplyJsonFields(&player->Params(), nlohmann::json{{"反動の押しを受ける、当たる前のフレーム数", 2}}), 0u);
    const nlohmann::json fields = ParamsFields(player);
    ASSERT_TRUE(fields.contains("外した押しで返る速さの倍率"));
    const float scale = fields.at("外した押しで返る速さの倍率").get<float>();
    ASSERT_LT(scale, 1.0f);
    const float full = DashSpeed(player) * fields.at("返る速さのダッシュの速さに対する倍率").get<float>();
    ASSERT_GT(hitFrame - 2, 2);
    ASSERT_TRUE(DashIntoFrontWallPressing(player, hitFrame, hitFrame - 2, NS::Vector3{}));
    // 失敗かどうかは止めの明けに決まる
    StepToTheReboundRelease(player);

    const NS::Vector3 bounced = player->Body().Velocity();
    EXPECT_NEAR(-bounced.z, full * scale, 1.0e-2f);
    EXPECT_NEAR(bounced.x, 0.0f, 1.0e-3f);

    // 玉を解いて落下の状態へ戻る。反動は続かず、エアダッシュは戻らない
    const int end = fields.at("反動の補正が終わるフレーム数").get<int>();
    for (int frame = 0; frame < end + 10; ++frame)
    {
        player->Update(false);
    }
    EXPECT_FALSE(player->IsDashRebounding());
    EXPECT_FALSE(player->IsCurled());
    EXPECT_EQ(player->AirDash().Remaining(), 0);
    EXPECT_LT(player->Body().Velocity().y, 0.0f);
}

// 反射から決める時は、押す時機が合った時だけ強い束を始め、押さない時は弱い束を始める。
// スティックを倒さずに押しても、時機が合えば強い束で、向きは当て方のまま
TEST(PlayerAirDashTest, ReflectedReboundStartsTheFullReactionOnlyOnATimedPress)
{
    const int hitFrame = FramesToTheFrontWall();
    ASSERT_GT(hitFrame, 3);
    NS::Scene pressedScene;
    PlayerActor* pressed = PlaceFloatingPlayer(pressedScene);
    ASSERT_NE(pressed, nullptr);
    AddFrontWall(pressedScene);
    UseReflectedRebound(pressed, 0);
    ASSERT_TRUE(DashIntoFrontWallPressing(pressed, hitFrame, 2, NS::Vector3{}));

    NS::Scene quietScene;
    PlayerActor* quiet = PlaceFloatingPlayer(quietScene);
    ASSERT_NE(quiet, nullptr);
    AddFrontWall(quietScene);
    UseReflectedRebound(quiet, 0);
    ASSERT_TRUE(DashIntoFrontWallPressing(quiet, hitFrame, -1, NS::Vector3{}));

    // 失敗の束に残像は無い。正面の壁の当て方の向きは真後ろで、表の向きなので、成功は強い束
    EXPECT_EQ(LastGhostLength(quiet), 0);
    ASSERT_GT(LastGhostLength(pressed), 0);

    // 20 度斜めに当てた返る向きは表の向きでないので、成功しても弱い束 (残像が強い束より短い)
    NS::Scene obliqueScene;
    PlayerActor* oblique = PlaceFloatingPlayer(obliqueScene);
    ASSERT_NE(oblique, nullptr);
    AddFrontWall(obliqueScene);
    UseReflectedRebound(oblique, 0);
    const float yaw = NS::k_Pi * 20.0f / 180.0f;
    MoveStick(oblique, NS::Vector3{std::sin(yaw), 0.0f, std::cos(yaw)}, 1.0f);
    oblique->Update(true);
    MoveStick(oblique, NS::Vector3{}, 0.0f);
    for (int frame = 0; frame < 30 && !oblique->IsDashRebounding(); ++frame)
    {
        oblique->Update(true);
    }
    ASSERT_TRUE(oblique->IsDashRebounding());
    PressInTheReboundStop(oblique);
    EXPECT_GT(LastGhostLength(oblique), 0);
    EXPECT_LT(LastGhostLength(oblique), LastGhostLength(pressed));

    // 押さずに当てた後、止めの間に押すと、そのフレームに強い束へ替わる
    PressInTheReboundStop(quiet);
    EXPECT_EQ(LastGhostLength(quiet), LastGhostLength(pressed));

    StepToTheReboundRelease(pressed);
    const GL::Player::TerrainReboundRecord& hit = pressed->AirDash().LastRebound();
    EXPECT_NEAR(AngleBetween(hit.assigned, pressed->Body().Velocity()), 0.0f, 1.0e-3f);
}

// 返る向きと同じ向きへ入力すると、向きは変わらず、入力が合ったと数える
TEST(PlayerAirDashTest, ReflectedReboundCountsTheStickAlongTheReflectionAsMatched)
{
    NS::Scene scene;
    PlayerActor* player = PlaceFloatingPlayer(scene);
    ASSERT_NE(player, nullptr);
    AddFrontWall(scene);
    UseReflectedRebound(player, 0);
    ASSERT_TRUE(DashIntoFrontWall(player));

    MoveStick(player, NS::Vector3{0.0f, 0.0f, -1.0f}, 1.0f);
    PressInTheReboundStop(player);
    StepToTheReboundRelease(player);
    const GL::Player::TerrainReboundRecord& hit = player->AirDash().LastRebound();
    EXPECT_EQ(hit.input, GL::Level::ReboundInputResult::Matched);
    EXPECT_NEAR(AngleBetween(hit.assigned, hit.shifted), 0.0f, 1.0e-3f);
    EXPECT_NEAR(AngleBetween(hit.assigned, player->Body().Velocity()), 0.0f, 1.0e-3f);
}

// 反射から決める時も、操作なしの段の後の補正の段では、スティックを倒した側へ曲がる。曲がる角度は補正の段の上限まで
TEST(PlayerAirDashTest, ReflectedReboundSteersTowardTheStickInTheSteerStage)
{
    NS::Scene scene;
    PlayerActor* player = PlaceFloatingPlayer(scene);
    ASSERT_NE(player, nullptr);
    AddFrontWall(scene);
    const nlohmann::json fields = ParamsFields(player);
    UseReflectedRebound(player, 0);
    ASSERT_TRUE(DashIntoFrontWall(player));
    // 時機の合った押しが無い反動は補正の段に入らないので、止めの間に押す
    PressInTheReboundStop(player);
    const int lock = fields.at("反動の操作なしのフレーム数").get<int>();
    for (int frame = 0; frame < 60 && player->AirDash().ReboundFrames() <= lock; ++frame)
    {
        player->Update(false);
    }

    MoveStick(player, NS::Vector3{1.0f, 0.0f, 0.0f}, 1.0f);
    const int end = fields.at("反動の補正が終わるフレーム数").get<int>();
    for (int frame = 0; frame < 120 && player->AirDash().ReboundFrames() + 3 < end; ++frame)
    {
        player->Update(false);
    }
    ASSERT_TRUE(player->IsDashRebounding());
    const NS::Vector3 velocity = player->Body().Velocity();
    EXPECT_GT(velocity.x, 0.0f);
    // 水平の向きだけを見る。補正の段は重力で下へも曲がる
    const float turned = std::atan2(velocity.x, -velocity.z);
    const float step = NS::k_Pi * 2.0f / static_cast<float>(fields.at("向きの表の分割数").get<int>());
    EXPECT_LT(turned, step * 0.5f);
}
