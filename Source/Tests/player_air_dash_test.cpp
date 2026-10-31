#include "Game/Level/ReboundDirections.h"
#include "Game/Player/PlayerAppearance.h"
#include "Game/Player/PlayerParams.h"
#include "Game/Player/PlayerSounds.h"
#include "Game/Player/States/AirDashPlayerState.h"
#include "Game/PlayerActor.h"
#include "NSlib/Core/OBB.h"
#include "NSlib/CoreObject/ActorJson.h"
#include "NSlib/CoreObject/Action/ActionTimeline.h"
#include "NSlib/CoreObject/ClassObjs/Body.h"
#include "NSlib/CoreObject/Reflection/ReflectionJson.h"
#include "NSlib/CoreObject/Scene/Scene.h"
#include "NSlib/Core/Windows/Clock.h"
#include "NSlib/Core/Windows/Input.h"
#include "NSlib/Phys/PhysicsScene.h"
#include "Tests/TestActorFile.h"
#include "Tests/TestLogCapture.h"
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
        const NS::ActorAction* action = player->ActionTimelineClassObj()->FindAction("AirDash");
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
    PlayerActor* PlaceFloatingPlayer(NS::Scene& scene)
    {
        nlohmann::json doc = NS::MakeSceneJson();
        nlohmann::json entry = NS::MakeActorJson();
        NS::SetActorJsonFile(entry, "PlayerActor");
        NS::SetActorJsonId(entry, 1);
        NS::SceneJsonActors(doc).push_back(std::move(entry));
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
        NS::OBB box;
        box.center = center;
        box.halfExtentX = half.x;
        box.halfExtentY = half.y;
        box.halfExtentZ = half.z;
        scene.Physics().AddBox(box, NS::Phys::ObjectLayers::Terrain);
    }

    // 自機の前 (+Z) の、面が z = 3 の大きな壁
    void AddFrontWall(NS::Scene& scene)
    {
        AddBox(scene, NS::Vector3{0.0f, 5.0f, 3.5f}, NS::Vector3{20.0f, 20.0f, 0.5f});
    }

    // +Z へ押した瞬間にダッシュし、放したスティックのまま壁へ当てる。反動へ移ったら true
    bool DashIntoFrontWall(PlayerActor* player)
    {
        player->SetDesiredMove(NS::Vector3{0.0f, 0.0f, 1.0f}, 1.0f);
        player->Update(true);
        player->SetDesiredMove(NS::Vector3{}, 0.0f);
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
        player->SetDesiredMove(NS::Vector3{1.0f, 0.0f, 0.0f}, 1.0f);
        player->Update(true);
        EXPECT_FALSE(player->IsAirDashing());
        EXPECT_EQ(player->AirDashesRemaining(), 1);
        EXPECT_EQ(log.WarningCount(), 1);
    }
} // namespace

// 遅い世界で、世界を進めない固定フレームの前に押して放したダッシュを、次に世界を進めた固定フレームで出す
TEST(PlayerAirDashTest, PressBeforeASkippedSlowStepDashesOnTheNextWorldStep)
{
    NS::Scene scene;
    PlayerActor* player = PlaceFloatingPlayer(scene);
    ASSERT_NE(player, nullptr);
    NS::OS::Input& input = NS::OS::Input::Get();
    input.Keyboard().OnKeyDown(NS::OS::Key::D);
    scene.SetWorldSpeed(0.5f);

    input.Mouse().OnButtonDown(NS::OS::MouseButton::Left);
    input.Mouse().OnButtonUp(NS::OS::MouseButton::Left);
    // 速さ 0.5 の 1 歩目は世界を進めない
    scene.OnUpdate();
    EXPECT_FALSE(player->IsAirDashing());
    scene.OnUpdate();
    EXPECT_TRUE(player->IsAirDashing());
    input.Keyboard().OnKeyUp(NS::OS::Key::D);
}

TEST(PlayerAirDashTest, PressFrameStartsTheDashAtTheDashSpeed)
{
    NS::Scene scene;
    PlayerActor* player = PlaceFloatingPlayer(scene);
    ASSERT_NE(player, nullptr);
    const float distance = ParamsFields(player).at("エアダッシュの距離").get<float>();
    ASSERT_GT(AirDashLength(player), 0);
    const float speed = DashSpeed(player);
    const float startX = player->Root().Position().x;

    player->SetDesiredMove(NS::Vector3{1.0f, 0.0f, 0.0f}, 1.0f);
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
    EXPECT_EQ(player->AirDashesRemaining(), 0);
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

    player->SetDesiredMove(NS::Vector3{1.0f, 0.0f, 0.0f}, 1.0f);
    player->Update(true);
    ASSERT_TRUE(player->IsAirDashing());
    EXPECT_TRUE(player->Sounds().IsLandAllowed());
}

// 倒し直してすぐ押しても、押したフレームに倒している向きへ出る
TEST(PlayerAirDashTest, TiltJustBeforeThePressDashesTheTiltedWay)
{
    NS::Scene scene;
    PlayerActor* player = PlaceFloatingPlayer(scene);
    ASSERT_NE(player, nullptr);

    player->SetDesiredMove(NS::Vector3{0.0f, 0.0f, 1.0f}, 1.0f);
    for (int frame = 0; frame < 30; ++frame)
    {
        player->Update(false);
    }
    player->SetDesiredMove(NS::Vector3{1.0f, 0.0f, 0.0f}, 1.0f);
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

    player->SetDesiredMove(NS::Vector3{1.0f, 0.0f, 0.0f}, 1.0f);
    player->Update(true);
    player->SetDesiredMove(NS::Vector3{}, 0.0f);
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
        std::vector<NS::ActorAction> actions = NS::ReadActorActions("PlayerActor", actorFile);
        for (NS::ActorAction& action : actions)
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

    player->SetDesiredMove(NS::Vector3{1.0f, 0.0f, 0.0f}, 1.0f);
    player->Update(true);
    player->SetDesiredMove(NS::Vector3{}, 0.0f);
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
            std::vector<NS::ActorAction> actions = NS::ReadActorActions("PlayerActor", actorFile);
            std::erase_if(actions, [](const NS::ActorAction& action) { return action.name == "AirDash"; });
            NS::WriteActorActions(actorFile, actions);
        });
        ExpectNoDashOnPress();
    }
    {
        SCOPED_TRACE("長さが 0");
        const ScopedActorFileEdit unset("PlayerActor", [](nlohmann::json& actorFile) {
            std::vector<NS::ActorAction> actions = NS::ReadActorActions("PlayerActor", actorFile);
            for (NS::ActorAction& action : actions)
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

// エアダッシュを断ったフレームも、同じフレームに頼んだ体当たりは出る
TEST(PlayerAirDashTest, ARefusedDashDoesNotSwallowTheBodySlam)
{
    const ScopedActorFileEdit unset("PlayerActor", [](nlohmann::json& actorFile) {
        std::vector<NS::ActorAction> actions = NS::ReadActorActions("PlayerActor", actorFile);
        for (NS::ActorAction& action : actions)
        {
            if (action.name == "AirDash")
            {
                action.length = 0;
            }
        }
        NS::WriteActorActions(actorFile, actions);
    });
    NS::Scene scene;
    PlayerActor* player = PlaceFloatingPlayer(scene);
    ASSERT_NE(player, nullptr);
    const TestLogCapture log;
    player->RequestBodySlam(0.0f, NS::Vector3{0.0f, 0.0f, 1.0f});
    player->Update(true);
    EXPECT_FALSE(player->IsAirDashing());
    EXPECT_TRUE(player->IsBodySlamming());
}

// やり直しの後に一度も接地しないまま宙で体当たりしても、その後の着地の音は鳴る
TEST(PlayerAirDashTest, BodySlamFromTheAirAllowsTheNextLandingSound)
{
    const ScopedActorFileEdit unset("PlayerActor", [](nlohmann::json& actorFile) {
        std::vector<NS::ActorAction> actions = NS::ReadActorActions("PlayerActor", actorFile);
        for (NS::ActorAction& action : actions)
        {
            if (action.name == "AirDash")
            {
                action.length = 0;
            }
        }
        NS::WriteActorActions(actorFile, actions);
    });
    NS::Scene scene;
    PlayerActor* player = PlaceFloatingPlayer(scene);
    ASSERT_NE(player, nullptr);
    const TestLogCapture log;
    ASSERT_FALSE(player->Sounds().IsLandAllowed());
    player->RequestBodySlam(0.0f, NS::Vector3{0.0f, 0.0f, 1.0f});
    player->Update(true);
    ASSERT_TRUE(player->IsBodySlamming());
    EXPECT_TRUE(player->Sounds().IsLandAllowed());
}

// エアダッシュの状態に入っても動作を始められなければ、その場で普段の動きへ移る
TEST(PlayerAirDashTest, TheDashEndsWhenItsActionCannotStart)
{
    const ScopedActorFileEdit removed("PlayerActor", [](nlohmann::json& actorFile) {
        std::vector<NS::ActorAction> actions = NS::ReadActorActions("PlayerActor", actorFile);
        std::erase_if(actions, [](const NS::ActorAction& action) { return action.name == "AirDash"; });
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
    player->SetDesiredMove(NS::Vector3{1.0f, 0.0f, 0.0f}, 1.0f);
    player->Update(true);
    player->SetDesiredMove(NS::Vector3{}, 0.0f);
    player->Update(false);
    ASSERT_TRUE(player->IsAirDashing());
    {
        const ScopedActorFileEdit shorter("PlayerActor", [](nlohmann::json& actorFile) {
            std::vector<NS::ActorAction> actions = NS::ReadActorActions("PlayerActor", actorFile);
            for (NS::ActorAction& action : actions)
            {
                if (action.name == "AirDash")
                {
                    action.length = 10;
                }
            }
            NS::WriteActorActions(actorFile, actions);
        });
        player->ActionTimelineClassObj()->ReloadActions(player);
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
    player->SetDesiredMove(NS::Vector3{1.0f, 0.0f, 0.0f}, 1.0f);
    player->Update(true);
    player->SetDesiredMove(NS::Vector3{}, 0.0f);
    player->Update(false);
    ASSERT_TRUE(player->IsAirDashing());
    player->ActionTimelineClassObj()->ReloadActions(player);
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
    EXPECT_EQ(player->AirDashesRemaining(), 1);
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

    player->SetDesiredMove(NS::Vector3{1.0f, 0.0f, 0.0f}, 1.0f);
    player->Update(true);
    player->SetDesiredMove(NS::Vector3{}, 0.0f);
    for (int frame = 0; frame < 240 && !player->Body().IsGrounded(); ++frame)
    {
        player->Update(false);
    }
    ASSERT_TRUE(player->Body().IsGrounded());
    player->Update(false);
    EXPECT_EQ(player->AirDashesRemaining(), 1);
}

TEST(PlayerAirDashTest, SteeringNeverTurnsMoreThanTheLimit)
{
    NS::Scene scene;
    PlayerActor* player = PlaceFloatingPlayer(scene);
    ASSERT_NE(player, nullptr);
    AddFrontWall(scene);
    const nlohmann::json fields = ParamsFields(player);
    ASSERT_TRUE(DashIntoFrontWall(player));
    // 操作なしの間に倒すとずらしになるので、操作なしの間が明けてから測る
    const float lock = fields.at("反動の操作なしの秒").get<float>();
    for (int frame = 0; frame < 60 && player->AirDash().ReboundElapsed() <= lock; ++frame)
    {
        player->Update(false);
    }
    // 補正の段の間ずっと真横へ倒す
    player->SetDesiredMove(NS::Vector3{1.0f, 0.0f, 0.0f}, 1.0f);
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
        player->SetDesiredMove(desired, scale);
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

    player->SetDesiredMove(NS::Vector3{0.0f, 0.0f, 1.0f}, 1.0f);
    player->Update(true);
    ASSERT_TRUE(player->IsAirDashing());
    const GL::Player::CameraTargetState dashing = player->GetCameraTargetState();
    EXPECT_TRUE(dashing.rebound.widenView);
    EXPECT_FALSE(dashing.rebound.rebounding);

    player->SetDesiredMove(NS::Vector3{}, 0.0f);
    for (int frame = 0; frame < 30 && !player->IsDashRebounding(); ++frame)
    {
        player->Update(false);
    }
    ASSERT_TRUE(player->IsDashRebounding());
    player->SetDesiredMove(NS::Vector3{0.0f, 0.0f, 1.0f}, 1.0f);
    for (int frame = 0; frame < 30 && player->AirDash().LastRebound().shifted.y <= 0.5f; ++frame)
    {
        player->Update(false);
    }
    ASSERT_TRUE(player->IsDashRebounding());
    ASSERT_GT(player->AirDash().LastRebound().shifted.y, 0.5f);
    const GL::Player::CameraTargetState rebounding = player->GetCameraTargetState();
    EXPECT_TRUE(rebounding.rebound.rebounding);
    EXPECT_TRUE(rebounding.rebound.widenView);
    EXPECT_FALSE(rebounding.rebound.holdLook);
    EXPECT_FLOAT_EQ(rebounding.rebound.slamDirection.Length(), 0.0f);
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

    player->SetDesiredMove(NS::Vector3{0.0f, 0.0f, 1.0f}, 1.0f);
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

    player->SetDesiredMove(NS::Vector3{0.0f, 0.0f, 1.0f}, 1.0f);
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

TEST(PlayerAirDashTest, TiltAfterTheLockDoesNotShift)
{
    NS::Scene scene;
    PlayerActor* player = PlaceFloatingPlayer(scene);
    ASSERT_NE(player, nullptr);
    AddFrontWall(scene);
    const float lock = ParamsFields(player).at("反動の操作なしの秒").get<float>();
    ASSERT_TRUE(DashIntoFrontWall(player));
    for (int frame = 0; frame < 60 && player->AirDash().ReboundElapsed() <= lock; ++frame)
    {
        player->Update(false);
    }

    player->SetDesiredMove(NS::Vector3{0.0f, 0.0f, 1.0f}, 1.0f);
    for (int frame = 0; frame < 5; ++frame)
    {
        player->Update(false);
    }
    EXPECT_NEAR(player->AirDash().LastRebound().shifted.y, 0.0f, 1.0e-3f);
}

// 押したフレームに丸まる。立ち姿のままダッシュすると、飛び出した瞬間に玉に見えない
TEST(PlayerAirDashTest, PressFrameCurlsIntoTheBall)
{
    NS::Scene scene;
    PlayerActor* player = PlaceFloatingPlayer(scene);
    ASSERT_NE(player, nullptr);
    ASSERT_FALSE(player->IsCurled());

    player->SetDesiredMove(NS::Vector3{1.0f, 0.0f, 0.0f}, 1.0f);
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

    player->SetDesiredMove(NS::Vector3{1.0f, 0.0f, 0.0f}, 1.0f);
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

    player->SetDesiredMove(NS::Vector3{0.0f, 0.0f, 1.0f}, 1.0f);
    player->Update(true);
    player->SetDesiredMove(NS::Vector3{}, 0.0f);
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

    player->SetDesiredMove(NS::Vector3{1.0f, 0.0f, 0.0f}, 1.0f);
    player->Update(true);
    player->SetDesiredMove(NS::Vector3{}, 0.0f);
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
    EXPECT_EQ(player->AirDashesRemaining(), 1);
}

TEST(PlayerAirDashTest, AirControlComesBackGraduallyAfterTheDash)
{
    NS::Scene scene;
    PlayerActor* player = PlaceFloatingPlayer(scene);
    ASSERT_NE(player, nullptr);
    const nlohmann::json fields = ParamsFields(player);
    const float fullDrag = fields.at("曲がる時の抵抗").get<float>() * NS::OS::FrameTimer::FixedDelta();

    player->SetDesiredMove(NS::Vector3{0.0f, 0.0f, 1.0f}, 1.0f);
    player->Update(true);
    player->SetDesiredMove(NS::Vector3{}, 0.0f);
    for (int frame = 0; frame < 60 && player->IsAirDashing(); ++frame)
    {
        player->Update(false);
    }
    ASSERT_FALSE(player->IsAirDashing());

    player->SetDesiredMove(NS::Vector3{1.0f, 0.0f, 0.0f}, 1.0f);
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

    player->SetDesiredMove(NS::Vector3{1.0f, 0.0f, 0.0f}, 1.0f);
    const float before = player->Body().Velocity().x;
    player->Update(false);
    EXPECT_NEAR(player->Body().Velocity().x - before, full, 1.0e-3f);
}
