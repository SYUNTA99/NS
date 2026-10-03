#include "Game/Level/CollisionInput.h"
#include "Game/Level/CourseDirector.h"
#include "Game/Level/Goal.h"
#include "Game/Level/KillZone.h"
#include "Game/Level/LaunchArc.h"
#include "Game/Level/LevelMessages.h"
#include "Game/Player.h"
#include "Runtime/Object/Actor.h"
#include "Runtime/Object/Components/Body.h"
#include "Runtime/Object/Components/PlayerInput.h"
#include "Runtime/Object/Components/TransformComponent.h"
#include "Runtime/Object/IUse/IUseSceneObj.h"
#include "Runtime/Object/Reflection/ObjectBuilder.h"
#include "Runtime/Object/Scene/Scene.h"
#include "Runtime/Object/Scene/SceneJson.h"
#include "Runtime/Object/ScreenFade.h"
#include "Runtime/Platform/Input.h"
#include "Runtime/Platform/Mouse.h"

#include <gtest/gtest.h>

// 落下死とゴールがメッセージで伝わり、コースの進行役が流れを進めることを縛る

namespace
{
    // 本物のマウス左を押したままにする。試しの終わりにマウスの状態を空へ戻す
    struct MouseLeftPress
    {
        MouseLeftPress() noexcept { NS::Platform::Input::Get().Mouse().OnButtonDown(NS::Platform::MouseButton::Left); }
        ~MouseLeftPress() noexcept { NS::Platform::Input::Get().Mouse().ClearState(); }
        MouseLeftPress(const MouseLeftPress&) = delete;
        MouseLeftPress& operator=(const MouseLeftPress&) = delete;
    };

    // 本番の入力の段と Player の段を 1 歩ずつ回す
    void StepPlayer(Player& player)
    {
        player.ReadInput();
        player.Update();
    }

    // プレイヤーと落下死の範囲を敷いたシーン文書
    nlohmann::json CourseWithPlayer()
    {
        nlohmann::json doc = NS::Obj::MakeSceneJson();
        (void)EnsurePlayerObject(doc);
        (void)NS::Game::Level::EnsureKillZoneObject(doc);
        return doc;
    }

    NS::Core::Vector3 SpawnOf(const nlohmann::json& doc)
    {
        return NS::Obj::ObjectPosition(NS::Obj::SceneJsonObjects(doc)[FindPlayerObjectIndex(doc)]);
    }

    // ゴールを自機の出現位置に重ねたシーン文書
    nlohmann::json CourseWithPlayerOnGoal()
    {
        nlohmann::json doc = CourseWithPlayer();
        nlohmann::json goal = NS::Obj::MakePrototypeJson<NS::Game::Level::Goal>();
        NS::Obj::SetObjectPosition(goal, SpawnOf(doc));
        NS::Obj::SceneJsonObjects(doc).push_back(std::move(goal));
        NS::Obj::EnsureUniqueObjectIds(doc);
        return doc;
    }
} // namespace

TEST(CourseDirector, PlayerCreatesDirectorAfterPlacement)
{
    NS::Obj::Scene scene;
    scene.LoadJson(CourseWithPlayer());
    EXPECT_NE(NS::Obj::FindSceneObj<NS::Game::Level::CourseDirector>(scene), nullptr);

    // 組み直すと捨てられ、プレイヤーの居ないシーンでは作られない
    scene.LoadJson(NS::Obj::MakeSceneJson());
    EXPECT_EQ(NS::Obj::FindSceneObj<NS::Game::Level::CourseDirector>(scene), nullptr);
}

TEST(CourseDirector, KillZoneKillsPlayerAndDirectorRestartsCourse)
{
    const nlohmann::json doc = CourseWithPlayer();
    NS::Obj::Scene scene;
    scene.LoadJson(doc);
    (void)scene.BeginPlayBaseline();
    Player* player = FindPlayer(scene.Objects());
    ASSERT_NE(player, nullptr);

    // 落下死の範囲 (上面 y=-50) の中へ落とす
    player->Root().SetPosition(NS::Core::Vector3{0.0f, -52.0f, 0.0f});
    scene.HitSensors().OnTick();
    EXPECT_TRUE(player->IsDead());

    NS::Game::Level::CourseDirector* director = NS::Obj::FindSceneObj<NS::Game::Level::CourseDirector>(scene);
    ASSERT_NE(director, nullptr);
    director->OnTick();
    // 出現位置へ戻り、命も満タンへ戻る
    EXPECT_FALSE(player->IsDead());
    EXPECT_FLOAT_EQ(player->Root().Position().y, SpawnOf(doc).y);
}

TEST(CourseDirector, GoalStartsClearSequenceAndLocksInput)
{
    nlohmann::json doc = CourseWithPlayer();
    nlohmann::json goal = NS::Obj::MakePrototypeJson<NS::Game::Level::Goal>();
    NS::Obj::SetObjectPosition(goal, SpawnOf(doc));
    NS::Obj::SceneJsonObjects(doc).push_back(std::move(goal));
    NS::Obj::EnsureUniqueObjectIds(doc);

    NS::Obj::Scene scene;
    scene.LoadJson(doc);
    (void)scene.BeginPlayBaseline();
    Player* player = FindPlayer(scene.Objects());
    ASSERT_NE(player, nullptr);
    NS::Game::Level::CourseDirector* director = NS::Obj::FindSceneObj<NS::Game::Level::CourseDirector>(scene);
    ASSERT_NE(director, nullptr);

    scene.HitSensors().OnTick();
    director->OnTick();
    EXPECT_TRUE(director->IsClearing());
    EXPECT_TRUE(director->Fade().IsFading());
    const NS::Obj::PlayerInput* input = NS::Obj::ComponentCast<NS::Obj::PlayerInput>(player->Part("Input"));
    ASSERT_NE(input, nullptr);
    EXPECT_TRUE(input->IsLocked());
    EXPECT_TRUE(input->IsActiveSelf());

    // 流れの最中に届くゴールの知らせは捨てる
    scene.HitSensors().OnTick();
    director->OnTick();
    EXPECT_TRUE(director->IsClearing());
}

// 体当たりのボタンを止めを見ずに読むと、暗転の間も押したままの自機が溜め続ける
TEST(CourseDirector, ClearLockStopsTheHeldSlamButton)
{
    const nlohmann::json doc = CourseWithPlayerOnGoal();
    NS::Obj::Scene scene;
    scene.LoadJson(doc);
    (void)scene.BeginPlayBaseline();
    Player* player = FindPlayer(scene.Objects());
    ASSERT_NE(player, nullptr);
    NS::Game::Level::CourseDirector* director = NS::Obj::FindSceneObj<NS::Game::Level::CourseDirector>(scene);
    ASSERT_NE(director, nullptr);

    MouseLeftPress press;
    for (int i = 0; i < 60 && !player->ChargeJudge().IsCharging(); ++i)
    {
        StepPlayer(*player);
    }
    ASSERT_TRUE(player->ChargeJudge().IsCharging());

    // 溜めている間に落ちた分を戻してゴールに重ねる
    player->Root().SetPosition(SpawnOf(doc));
    scene.HitSensors().OnTick();
    director->OnTick();
    ASSERT_TRUE(director->IsClearing());

    for (int i = 0; i < 10; ++i)
    {
        StepPlayer(*player);
        EXPECT_FALSE(player->ChargeJudge().IsCharging()) << "歩 " << i;
        EXPECT_FALSE(player->IsBodySlamming()) << "歩 " << i;
    }
}

// 止めた瞬間の向きを持ち続けると、暗転の間もゴールで倒していた向きへ歩き続ける
TEST(CourseDirector, ClearLockNeutralizesTheMoveInput)
{
    const nlohmann::json doc = CourseWithPlayerOnGoal();
    NS::Obj::Scene scene;
    scene.LoadJson(doc);
    (void)scene.BeginPlayBaseline();
    Player* player = FindPlayer(scene.Objects());
    ASSERT_NE(player, nullptr);
    NS::Game::Level::CourseDirector* director = NS::Obj::FindSceneObj<NS::Game::Level::CourseDirector>(scene);
    ASSERT_NE(director, nullptr);

    player->SetDesiredMove(NS::Core::Vector3{1.0f, 0.0f, 0.0f}, 1.0f);
    for (int i = 0; i < 5; ++i)
    {
        player->Update();
    }
    const float lockedSpeed = player->Body().LateralVelocity().Length();
    ASSERT_GT(lockedSpeed, 0.0f);

    player->Root().SetPosition(SpawnOf(doc));
    scene.HitSensors().OnTick();
    director->OnTick();
    ASSERT_TRUE(director->IsClearing());

    for (int i = 0; i < 10; ++i)
    {
        StepPlayer(*player);
    }
    EXPECT_FLOAT_EQ(player->DesiredDirection().Length(), 0.0f);
    EXPECT_FLOAT_EQ(player->DesiredSpeedScale(), 0.0f);
    // 床の無い空中では手を放しても横は減らないので、加速し続けないことを見る
    EXPECT_LE(player->Body().LateralVelocity().Length(), lockedSpeed);
}

// 押しが偽になった歩を放したと読むと、溜めの最中の止めで溜めた突進が出る
TEST(CourseDirector, InputLockDropsTheChargeInsteadOfReleasingIt)
{
    NS::Obj::Scene scene;
    scene.LoadJson(CourseWithPlayer());
    (void)scene.BeginPlayBaseline();
    Player* player = FindPlayer(scene.Objects());
    ASSERT_NE(player, nullptr);

    for (int i = 0; i < 60 && !player->ChargeJudge().IsCharging(); ++i)
    {
        player->Update(true);
    }
    ASSERT_TRUE(player->ChargeJudge().IsCharging());
    const std::uint32_t impacts = player->Resolver().LastImpact().sequence;

    EXPECT_TRUE(NS::Game::Level::SendMsgInputLock(*player, true));
    EXPECT_FALSE(player->ChargeJudge().IsCharging());
    EXPECT_FLOAT_EQ(player->StanceHeight(), 1.0f);

    player->Update(false);
    EXPECT_FALSE(player->IsBodySlamming());
    EXPECT_EQ(player->Resolver().LastImpact().sequence, impacts);
}

TEST(CourseDirector, RestartReturnsObjectsToBaseline)
{
    nlohmann::json doc = CourseWithPlayer();
    nlohmann::json rock = NS::Obj::MakeObjectJson();
    NS::Obj::SetObjectJsonClass(rock, "MapObj");
    NS::Obj::SetObjectPosition(rock, NS::Core::Vector3{5.0f, 1.0f, 0.0f});
    NS::Obj::SceneJsonObjects(doc).push_back(std::move(rock));
    NS::Obj::EnsureUniqueObjectIds(doc);

    NS::Obj::Scene scene;
    scene.LoadJson(doc);
    (void)scene.BeginPlayBaseline();
    NS::Obj::Actor* placed = nullptr;
    for (NS::Obj::Actor* actor : scene.Objects())
    {
        if (std::string_view{actor->ClassName()} == "MapObj")
        {
            placed = actor;
        }
    }
    ASSERT_NE(placed, nullptr);
    placed->Root().SetPosition(NS::Core::Vector3{9.0f, 1.0f, 0.0f});

    NS::Obj::GetOrCreateSceneObj<NS::Game::Level::CourseDirector>(scene)->RestartCourse();
    EXPECT_FLOAT_EQ(placed->Root().Position().x, 5.0f);
}
