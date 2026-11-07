#include "Game/Level/ReboundDirections.h"
#include "Game/Level/ReboundPreview.h"
#include "Game/Player/PlayerAppearance.h"
#include "Game/Player/PlayerParams.h"
#include "Game/Player/PlayerSounds.h"
#include "Game/Player/States/AirDashPlayerState.h"
#include "Game/PlayerActor.h"
#include "NSlib/CoreObject/ActorJson.h"
#include "NSlib/CoreObject/Action/ActorAction.h"
#include "NSlib/CoreObject/ClassObjs/Body.h"
#include "NSlib/CoreObject/ClassObjs/Collider.h"
#include "NSlib/CoreObject/Reflection/ReflectionJson.h"
#include "NSlib/CoreObject/Scene/Scene.h"
#include "NSlib/Core/Windows/Clock.h"
#include "NSlib/Core/Windows/Input.h"
#include "NSlib/Phys/PhysicsScene.h"
#include "NSlib/CoreObject/ClassObjs/PlayerInput.h"
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
    // 欄の秒を固定ステップのフレーム数にする
    int FramesFor(float seconds)
    {
        return static_cast<int>(std::ceil(seconds / NS::OS::FrameTimer::FixedDelta()));
    }

    nlohmann::json ParamsFields(const PlayerActor* player)
    {
        return NS::SerializeClassObjFields(&player->Params());
    }

    // 自機の .actor の動作 "AirDash" の長さ (状態の歩の回数)。動作が無ければ 0
    int AirDashLength(const PlayerActor* player)
    {
        const NS::ActorActionInfo* action = player->ActorActionClassObj()->FindAction("AirDash");
        if (action == nullptr)
        {
            return 0;
        }
        return action->length;
    }

    // 欄「エアダッシュの距離」を、動作 "AirDash" の長さの固定フレームで進む速さ
    float DashSpeed(const PlayerActor* player)
    {
        return ParamsFields(player).at("エアダッシュの距離").get<float>() /
               (static_cast<float>(AirDashLength(player)) * NS::OS::FrameTimer::FixedDelta());
    }

    // +Z を向くカメラと、宙に浮かせた自機。床は無い
    // 左スティックを worldDir の位置へ動かす。テストのカメラは動かないので、世界の向きとカメラで回す前の入力が同じになる
    void MoveStick(PlayerActor* player, const NS::Vector3& worldDir, float speedScale)
    {
        player->Input().SetDesiredMove(worldDir, speedScale);
        player->Input().SetClimbMove(worldDir.x, worldDir.z);
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
            player->Root().SetPosition(NS::Vector3{0.0f, 5.0f, 0.0f});
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
    ASSERT_GT(AirDashLength(player), 0);
    const float speed = DashSpeed(player);
    const float startX = player->Root().Position().x;

    MoveStick(player, NS::Vector3{1.0f, 0.0f, 0.0f}, 1.0f);
    player->Update(true);
    EXPECT_TRUE(player->IsAirDashing());
    EXPECT_NEAR(player->Body().Velocity().x, speed, 1.0e-3f);
    EXPECT_NEAR(player->Body().Velocity().y, 0.0f, 1.0e-3f);

    const int frames = AirDashLength(player);
    for (int frame = 0; frame < frames + 1 && player->IsAirDashing(); ++frame)
    {
        player->Update(true);
    }
    EXPECT_FALSE(player->IsAirDashing());
    EXPECT_EQ(player->AirDash().Remaining(), 0);
    // 距離はダッシュの速さで進む分。終わるフレームは残る速さで進むので、1 フレームで進む分だけずれてよい
    EXPECT_NEAR(player->Root().Position().x - startX, distance, speed * NS::OS::FrameTimer::FixedDelta());
}

// やり直しの後に一度も接地しないまま宙で動いても、その後の着地の音は鳴る
TEST(PlayerAirDashTest, DashFromTheAirAllowsTheNextLandingSound)
{
    NS::Scene scene;
    PlayerActor* player = PlaceFloatingPlayer(scene);
    ASSERT_NE(player, nullptr);
    ASSERT_GT(AirDashLength(player), 0);
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

    player->Input().SetClimbMove(0.0f, 1.0f);
    player->Input().SetDesiredMove(NS::Vector3{0.0f, 0.0f, 1.0f}, 1.0f);
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
        player->Input().SetDesiredMove(NS::Vector3{std::sin(angle), 0.0f, std::cos(angle)}, 1.0f);
        player->Update(false);
    }
    angle += k_TurnRadians;
    player->Input().SetDesiredMove(NS::Vector3{std::sin(angle), 0.0f, std::cos(angle)}, 1.0f);
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

// 当たらなければ、押したフレームから動作の長さの歩だけダッシュの速さで進み、次の歩で残る速さにして普段の動きへ移る
TEST(PlayerAirDashTest, DashLastsTheActionLengthThenKeepsTheExitSpeed)
{
    NS::Scene scene;
    PlayerActor* player = PlaceFloatingPlayer(scene);
    ASSERT_NE(player, nullptr);
    const int length = AirDashLength(player);
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

// ダッシュの長さと速さは .actor の動作 "AirDash" の長さで決まる
TEST(PlayerAirDashTest, TheDashLengthAndSpeedComeFromTheAirDashAction)
{
    const ScopedActorFileEdit shorter("PlayerActor", [](nlohmann::json& actorFile) {
        std::vector<NS::ActorActionInfo> actions = NS::ReadActorActions("PlayerActor", actorFile);
        for (NS::ActorActionInfo& action : actions)
        {
            if (action.name == "AirDash")
            {
                action.length = 10;
            }
        }
        NS::WriteActorActions(actorFile, actions);
    });
    NS::Scene scene;
    PlayerActor* player = PlaceFloatingPlayer(scene);
    ASSERT_NE(player, nullptr);
    ASSERT_EQ(AirDashLength(player), 10);
    const float speed = ParamsFields(player).at("エアダッシュの距離").get<float>() /
                        (10.0f * NS::OS::FrameTimer::FixedDelta());

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

// 動作 "AirDash" が無いか長さを決めていない自機は、押してもエアダッシュに入らない。入ると終わりが来ず宙で止まる
TEST(PlayerAirDashTest, NoDashWithoutTheAirDashActionLength)
{
    {
        SCOPED_TRACE("動作が無い");
        const ScopedActorFileEdit removed("PlayerActor", [](nlohmann::json& actorFile) {
            std::vector<NS::ActorActionInfo> actions = NS::ReadActorActions("PlayerActor", actorFile);
            std::erase_if(actions, [](const NS::ActorActionInfo& action) { return action.name == "AirDash"; });
            NS::WriteActorActions(actorFile, actions);
        });
        ExpectNoDashOnPress();
    }
    {
        SCOPED_TRACE("長さが 0");
        const ScopedActorFileEdit unset("PlayerActor", [](nlohmann::json& actorFile) {
            std::vector<NS::ActorActionInfo> actions = NS::ReadActorActions("PlayerActor", actorFile);
            for (NS::ActorActionInfo& action : actions)
            {
                if (action.name == "AirDash")
                {
                    action.length = 0;
                }
            }
            NS::WriteActorActions(actorFile, actions);
        });
        ExpectNoDashOnPress();
    }
}

// エアダッシュの状態に入っても動作を始められなければ、その場で普段の動きへ移る
TEST(PlayerAirDashTest, TheDashEndsWhenItsActionCannotStart)
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
    EXPECT_FALSE(player->IsAirDashing());
}

// 途中で動作の長さを変えて読み直しても、そのダッシュの速さは始めた時のまま
TEST(PlayerAirDashTest, TheDashSpeedHoldsWhenTheLengthChangesMidway)
{
    NS::Scene scene;
    PlayerActor* player = PlaceFloatingPlayer(scene);
    ASSERT_NE(player, nullptr);
    ASSERT_GT(AirDashLength(player), 10);
    const float speed = DashSpeed(player);
    MoveStick(player, NS::Vector3{1.0f, 0.0f, 0.0f}, 1.0f);
    player->Update(true);
    MoveStick(player, NS::Vector3{}, 0.0f);
    player->Update(false);
    ASSERT_TRUE(player->IsAirDashing());
    {
        const ScopedActorFileEdit shorter("PlayerActor", [](nlohmann::json& actorFile) {
            std::vector<NS::ActorActionInfo> actions = NS::ReadActorActions("PlayerActor", actorFile);
            for (NS::ActorActionInfo& action : actions)
            {
                if (action.name == "AirDash")
                {
                    action.length = 10;
                }
            }
            NS::WriteActorActions(actorFile, actions);
        });
        player->ActorActionClassObj()->ReloadActions(player);
    }
    player->Update(false);
    ASSERT_TRUE(player->IsAirDashing());
    EXPECT_NEAR(player->Body().Velocity().x, speed, 1.0e-3f);
}

// 途中で動作が止まっても (.actor の読み直し)、ダッシュは動作の長さで終わる
TEST(PlayerAirDashTest, TheDashEndsEvenIfTheActionIsReloadedMidway)
{
    NS::Scene scene;
    PlayerActor* player = PlaceFloatingPlayer(scene);
    ASSERT_NE(player, nullptr);
    const int length = AirDashLength(player);
    ASSERT_GT(length, 1);
    MoveStick(player, NS::Vector3{1.0f, 0.0f, 0.0f}, 1.0f);
    player->Update(true);
    MoveStick(player, NS::Vector3{}, 0.0f);
    player->Update(false);
    ASSERT_TRUE(player->IsAirDashing());
    player->ActorActionClassObj()->ReloadActions(player);
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

    // 当たりのヒットストップの間は反動の秒が進まないので、秒で待つ
    const float lock = fields.at("反動の操作なしの秒").get<float>();
    for (int frame = 0; frame < 60 && player->AirDash().ReboundElapsed() <= lock; ++frame)
    {
        player->Update(false);
    }
    player->Update(true);
    EXPECT_TRUE(player->IsAirDashing());

    // 補正の段で戻ったダッシュは 1 回だけ。使い切ったら次の押しは効かない
    for (int frame = 0; frame < AirDashLength(player) + 1; ++frame)
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
    player->Root().SetPosition(NS::Vector3{0.0f, 0.9f, 0.0f});
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
    const float lock = fields.at("反動の操作なしの秒").get<float>();
    for (int frame = 0; frame < 60 && player->AirDash().ReboundElapsed() <= lock; ++frame)
    {
        player->Update(false);
    }
    // 補正の段の間ずっと真横へ倒す
    MoveStick(player, NS::Vector3{1.0f, 0.0f, 0.0f}, 1.0f);
    const int frames = FramesFor(fields.at("反動の補正が終わる秒").get<float>());
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
        const float lock = fields.at("反動の操作なしの秒").get<float>();
        for (int frame = 0; frame < 60 && player->AirDash().ReboundElapsed() <= lock; ++frame)
        {
            player->Update(false);
        }
        MoveStick(player, desired, scale);
        const float end = fields.at("反動の補正が終わる秒").get<float>();
        for (int frame = 0; frame < 120 && player->AirDash().ReboundElapsed() + 0.05f < end; ++frame)
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
    const float lock = ParamsFields(player).at("反動の操作なしの秒").get<float>();
    ASSERT_TRUE(DashIntoFrontWall(player));
    ASSERT_NEAR(player->AirDash().LastRebound().shifted.y, 0.0f, 1.0e-3f);

    MoveStick(player, NS::Vector3{0.0f, 0.0f, 1.0f}, 1.0f);
    for (int frame = 0; frame < 60 && player->AirDash().ReboundElapsed() <= lock; ++frame)
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
    const float lock = ParamsFields(player).at("反動の操作なしの秒").get<float>();

    MoveStick(player, NS::Vector3{0.0f, 0.0f, 1.0f}, 1.0f);
    player->Update(true);
    for (int frame = 0; frame < 30 && !player->IsDashRebounding(); ++frame)
    {
        player->Update(false);
    }
    ASSERT_TRUE(player->IsDashRebounding());
    for (int frame = 0; frame < 60 && player->AirDash().ReboundElapsed() <= lock; ++frame)
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
    const float lock = ParamsFields(player).at("反動の操作なしの秒").get<float>();
    ASSERT_TRUE(DashIntoFrontWall(player));
    for (int frame = 0; frame < 60 && player->IsHitStopping(); ++frame)
    {
        player->Update(false);
    }
    // 明けのフレーム。ここで放したスティックのまま向きが決まる
    player->Update(false);
    ASSERT_GT(player->AirDash().ReboundElapsed(), 0.0f);
    ASSERT_LE(player->AirDash().ReboundElapsed(), lock);

    MoveStick(player, NS::Vector3{0.0f, 0.0f, 1.0f}, 1.0f);
    for (int frame = 0; frame < 60 && player->AirDash().ReboundElapsed() <= lock; ++frame)
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
    const nlohmann::json fields = ParamsFields(player);
    const int longest = fields.at("反動の止めの上限のフレーム数").get<int>();
    ASSERT_GT(longest, fields.at("反動の止めの最短のフレーム数").get<int>());
    ASSERT_TRUE(DashIntoFrontWall(player));

    EXPECT_EQ(CountReboundStopFrames(player, true), longest);
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
    ASSERT_EQ(NS::ApplyJsonFields(&player->Params(), nlohmann::json{{"反動の止めの最短のフレーム数", 1}}), 0u);
    const int longest = ParamsFields(player).at("反動の止めの上限のフレーム数").get<int>();
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
        const NS::Vector3 before = player->Root().Position();
        player->Update(false);
        if (player->Root().Position() != before)
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
    player->Root().SetPosition(NS::Vector3{0.0f, 5.0f, 0.0f});
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
    const float before = player->Root().Position().y;

    MoveStick(player, NS::Vector3{1.0f, 0.0f, 0.0f}, 1.0f);
    player->Update(true);
    ASSERT_TRUE(player->IsAirDashing());
    ASSERT_TRUE(player->IsCurled());
    EXPECT_NEAR(player->Root().Position().y, before, 1.0e-3f);
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
    const float full =
        ParamsFields(player).at("空中の加速度").get<float>() * NS::OS::FrameTimer::FixedDelta();
    player->Update(false);
    player->Update(false);

    MoveStick(player, NS::Vector3{1.0f, 0.0f, 0.0f}, 1.0f);
    const float before = player->Body().Velocity().x;
    player->Update(false);
    EXPECT_NEAR(player->Body().Velocity().x - before, full, 1.0e-3f);
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

    for (int frame = 0; frame < 30 && player->IsAirDashing() && player->Root().Position().z < 1.0f; ++frame)
    {
        player->Update(true);
    }
    ASSERT_TRUE(player->IsAirDashing());
    const NS::Vector3 position = player->Root().Position();
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
    const float wallFace = player->Root().Position().z + distance * 1.5f;
    AddBox(scene, NS::Vector3{0.0f, 5.0f, wallFace + 0.5f}, NS::Vector3{20.0f, 20.0f, 0.5f});

    MoveStick(player, NS::Vector3{0.0f, 0.0f, 1.0f}, 1.0f);
    player->Update(false);
    EXPECT_EQ(player->ReboundIndicator().ShownArrows(), nullptr) << "押す前";
    player->Update(true);
    ASSERT_TRUE(player->IsAirDashing());
    int frame = 0;
    while (player->IsAirDashing() && frame < AirDashLength(player) + 1)
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
