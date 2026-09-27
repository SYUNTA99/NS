#include "Editor/EditorObjects.h"

#include <Game/Level/CollisionInput.h>
#include <Game/Level/FollowCameraFeed.h>
#include <Game/Level/ImpactResolver.h>
#include <Game/Player/PlayerComponent.h>
#include <Game/Player/PlayerInputRelay.h>
#include <Runtime/Core/Math.h>
#include <Runtime/Object/Components/PlayerInput.h>
#include <Runtime/Object/Components/TransformComponent.h>
#include <Runtime/Object/Components/ThirdPersonFollow.h>
#include <Runtime/Object/GameObject.h>
#include <Runtime/Object/Object.h>
#include <Runtime/Object/ObjectJson.h>
#include <Runtime/Object/ObjectList.h>
#include <Runtime/Object/Reflection/ComponentEntry.h>
#include <Runtime/Object/Reflection/ObjectBuilder.h>
#include <Runtime/Object/Reflection/Reflection.h>
#include <Runtime/Object/Reflection/TypeRegistry.h>
#include <Runtime/Object/Scene/Scene.h>
#include <Runtime/Object/Scene/SceneJson.h>
#include <Runtime/Object/Transform.h>
#include <Runtime/Platform/Clock.h>
#include <Runtime/Platform/Input.h>
#include <Runtime/Platform/Keyboard.h>
#include <Runtime/Platform/Mouse.h>
#include <gtest/gtest.h>

#include "camera_screen.h"
#include "tuning_field_access.h"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <string_view>
#include <utility>

namespace
{
    using NS::Core::Vector3;
    using NS::Game::Level::FollowCameraFeed;
    using NS::Game::Player::PlayerComponent;
    using NS::Game::Player::PlayerInputRelay;
    using NS::Obj::GameObject;
    using NS::Obj::ObjectRef;
    using NS::Obj::Scene;
    using NS::Obj::ThirdPersonFollow;
    using NS::Platform::Key;

    constexpr float k_FixedDt = 1.0f / 60.0f;

    constexpr float k_IdleDistance = 3.0f;
    constexpr float k_RunDistance = 8.0f;
    constexpr float k_JumpDistance = 12.0f;
    constexpr float k_RunSpeedThreshold = 4.0f;

    void BuildRelayRig(GameObject& owner)
    {
        owner.AddComponent<NS::Obj::PlayerInput>();
        owner.AddComponent<PlayerInputRelay>();
        owner.AddComponent<PlayerComponent>();
        owner.OnStart();
    }

    NS::Obj::PlayerInput& Input(GameObject& owner)
    {
        return *owner.FindComponent<NS::Obj::PlayerInput>();
    }

    PlayerInputRelay& Relay(GameObject& owner)
    {
        return *owner.FindComponent<PlayerInputRelay>();
    }

    PlayerComponent& Player(GameObject& owner)
    {
        return *owner.FindComponent<PlayerComponent>();
    }

    //! 追従カメラと、それへ追う相手の運動を渡す部品を 1 組にして持つ
    struct FeedCamera
    {
        ThirdPersonFollow& follow;
        FollowCameraFeed& feed;
    };

    //! 3 段の距離を既定値から離して置く。どの段に寄ったかを距離 1 つで見分けられる
    FeedCamera AddFeedCamera(Scene& scene, std::uint32_t targetId)
    {
        GameObject* rig = scene.SpawnTransient<GameObject>();
        ThirdPersonFollow& follow = *rig->AddComponent<ThirdPersonFollow>();
        follow.SetActive(true);
        follow.SetAutoDistances(k_IdleDistance, k_RunDistance, k_JumpDistance);
        follow.SetRunSpeedThreshold(k_RunSpeedThreshold);
        NsTest::WriteObjectRefField(follow, "追従対象", targetId);
        FollowCameraFeed& feed = *rig->AddComponent<FollowCameraFeed>();
        // 開始は部品が揃ってから。SpawnTransient の開始は積む前に済んでいる
        rig->OnStart();
        return FeedCamera{follow, feed};
    }

    // numbered を外すと id の無い一時オブジェクトになる。参照では引けない
    GameObject& SpawnTarget(Scene& scene, bool numbered, bool withEntity)
    {
        GameObject* owner = nullptr;
        if (numbered)
            owner = scene.SpawnObject(std::make_unique<GameObject>(), "Target");
        else
            owner = scene.SpawnTransient<GameObject>();
        if (withEntity)
        {
            owner->AddComponent<PlayerComponent>();
        }
        owner->OnStart();
        return *owner;
    }

    void SettleZoom(ThirdPersonFollow& follow)
    {
        for (int i = 0; i < 200; ++i)
            follow.OnUpdate();
    }
} // namespace

class PlayerRelayTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        NS::Platform::FrameTimer::SetFixedDelta(k_FixedDt);
        ClearKeyboard();
    }
    void TearDown() override { ClearKeyboard(); }

    //! 入力はプロセスに 1 個しか無い。押したキーを次のテストへ持ち越さないよう前後で払う
    static void ClearKeyboard() noexcept
    {
        NS::Platform::Keyboard& kb = NS::Platform::Input::Get().Keyboard();
        kb.ClearState();
        kb.Update();
    }

    //! 押しっぱなしのままフレームを 1 つ進める。押した瞬間の判定はここで消える
    static void AdvanceFrame() noexcept { NS::Platform::Input::Get().Keyboard().Update(); }
};

TEST_F(PlayerRelayTest, DirectionAndSpeedScaleReachThePlayerInTheSameStep)
{
    GameObject owner;
    BuildRelayRig(owner);
    Input(owner).SetCameraForward({0.0f, 0.0f, 1.0f});

    NS::Platform::Input::Get().Keyboard().OnKeyDown(Key::W);
    Input(owner).OnUpdate();
    Relay(owner).OnUpdate();

    EXPECT_GT(Player(owner).DesiredDirection().z, 0.9f);
    EXPECT_NEAR(Player(owner).DesiredDirection().x, 0.0f, 1.0e-5f);
    EXPECT_FLOAT_EQ(Player(owner).DesiredSpeedScale(), Input(owner).DesiredSpeedScale());
}

TEST_F(PlayerRelayTest, ClimbInputReachesThePlayer)
{
    GameObject owner;
    BuildRelayRig(owner);
    Input(owner).SetCameraForward({1.0f, 0.0f, 0.0f});

    NS::Platform::Input::Get().Keyboard().OnKeyDown(Key::D);
    Input(owner).OnUpdate();
    Relay(owner).OnUpdate();

    EXPECT_FLOAT_EQ(Player(owner).ClimbRight(), 1.0f);
    EXPECT_FLOAT_EQ(Player(owner).ClimbForward(), 0.0f);
}

TEST_F(PlayerRelayTest, JumpPressReachesThePlayerOnTheStepOfThePress)
{
    GameObject owner;
    BuildRelayRig(owner);

    NS::Platform::Input::Get().Keyboard().OnKeyDown(Key::Space);
    Input(owner).OnUpdate();
    Relay(owner).OnUpdate();

    Player(owner).SetGrounded(true);
    Player(owner).Jump(k_FixedDt);

    EXPECT_FLOAT_EQ(Player(owner).VerticalVelocity(), 12.0f);
}

// 押しっぱなしのフレームまで押下を渡すと、押していないフレームに跳ぶ
TEST_F(PlayerRelayTest, HoldOnlyStepDoesNotPressTheJump)
{
    GameObject owner;
    BuildRelayRig(owner);

    NS::Platform::Input::Get().Keyboard().OnKeyDown(Key::Space);
    Input(owner).OnUpdate();
    AdvanceFrame();
    Input(owner).OnUpdate();
    ASSERT_FALSE(Input(owner).JumpPressed());

    Relay(owner).OnUpdate();
    Player(owner).SetGrounded(true);
    Player(owner).Jump(k_FixedDt);

    EXPECT_FLOAT_EQ(Player(owner).VerticalVelocity(), 0.0f);
    EXPECT_EQ(Player(owner).JumpsRemaining(), 1);
}

TEST_F(PlayerRelayTest, JumpHoldStateIsRelayed)
{
    GameObject owner;
    BuildRelayRig(owner);

    NS::Platform::Keyboard& kb = NS::Platform::Input::Get().Keyboard();
    kb.OnKeyDown(Key::Space);
    Input(owner).OnUpdate();
    Relay(owner).OnUpdate();
    Player(owner).OnUpdate();

    kb.OnKeyUp(Key::Space);
    AdvanceFrame();
    Input(owner).OnUpdate();
    Relay(owner).OnUpdate();

    // 離したフレームが届いていれば上昇が縮む
    Player(owner).SetVelocity(Vector3{0.0f, 10.0f, 0.0f});
    Player(owner).CutJumpRelease();

    EXPECT_FLOAT_EQ(Player(owner).VerticalVelocity(), 6.0f);
}

TEST_F(PlayerRelayTest, InactiveInputRelaysNothing)
{
    GameObject owner;
    BuildRelayRig(owner);
    Input(owner).SetCameraForward({0.0f, 0.0f, 1.0f});

    Player(owner).SetDesiredMove(Vector3{1.0f, 0.0f, 0.0f}, 0.5f);
    Input(owner).SetActive(false);

    NS::Platform::Input::Get().Keyboard().OnKeyDown(Key::W);
    Input(owner).OnUpdate();
    Relay(owner).OnUpdate();

    EXPECT_FLOAT_EQ(Player(owner).DesiredSpeedScale(), 0.5f);
    EXPECT_FLOAT_EQ(Player(owner).DesiredDirection().x, 1.0f);
}

TEST_F(PlayerRelayTest, MissingSideIsHarmless)
{
    GameObject withoutPlayer;
    withoutPlayer.AddComponent<NS::Obj::PlayerInput>();
    withoutPlayer.AddComponent<PlayerInputRelay>();
    withoutPlayer.OnStart();
    Relay(withoutPlayer).OnUpdate();

    GameObject withoutInput;
    withoutInput.AddComponent<PlayerInputRelay>();
    PlayerComponent& player = *withoutInput.AddComponent<PlayerComponent>();
    withoutInput.OnStart();

    player.SetDesiredMove(Vector3{1.0f, 0.0f, 0.0f}, 0.5f);
    Relay(withoutInput).OnUpdate();

    EXPECT_FLOAT_EQ(player.DesiredSpeedScale(), 0.5f);
}

TEST_F(PlayerRelayTest, AirborneTargetZoomsTheCamera)
{
    Scene scene;
    GameObject& target = SpawnTarget(scene, true, true);
    FeedCamera camera = AddFeedCamera(scene, target.Id());
    ASSERT_EQ(camera.follow.Target(), &target.Root());

    ASSERT_FALSE(Player(target).IsGrounded());
    camera.feed.OnUpdate();
    SettleZoom(camera.follow);

    EXPECT_NEAR(camera.follow.Distance(), k_JumpDistance, 0.01f);
}

TEST_F(PlayerRelayTest, GroundedRunSpeedReachesTheCamera)
{
    Scene scene;
    GameObject& target = SpawnTarget(scene, true, true);
    FeedCamera camera = AddFeedCamera(scene, target.Id());

    Player(target).SetGrounded(true);
    Player(target).SetVelocity(Vector3{10.0f, 0.0f, 0.0f});
    camera.feed.OnUpdate();
    SettleZoom(camera.follow);

    EXPECT_NEAR(camera.follow.Distance(), k_RunDistance, 0.01f);
}

// 丸まると根は円柱の半長ぶん下がるが、カメラは立ち姿の中心を見続ける。根を追うと押すたびに画面が 1 フレームで沈む
// 描画の補間の途中 (0.5) でも注視点は動かない
TEST_F(PlayerRelayTest, CurlingDoesNotSinkTheCamera)
{
    Scene scene;
    GameObject& target = SpawnTarget(scene, true, true);
    FeedCamera camera = AddFeedCamera(scene, target.Id());
    target.Root().SetPosition(Vector3{0.0f, 1.0f, 0.0f});
    target.Root().Snapshot();
    camera.feed.OnUpdate();
    const float standingLookY = camera.follow.EvaluatePose(1.0f).target.y;

    Player(target).SetCurled(true);
    camera.feed.OnUpdate();

    ASSERT_LT(target.Root().Position().y, 1.0f);
    EXPECT_NEAR(camera.follow.EvaluatePose(0.5f).target.y, standingLookY, 1e-5f);
    EXPECT_NEAR(camera.follow.EvaluatePose(1.0f).target.y, standingLookY, 1e-5f);
}

// 未採番の 0 同士を突き合わせると、追従対象の無いカメラが未採番の配置物を追っている扱いになる
TEST_F(PlayerRelayTest, UnsetTargetFeedsNothing)
{
    Scene scene;
    GameObject& target = SpawnTarget(scene, false, true);
    FeedCamera camera = AddFeedCamera(scene, 0u);

    ASSERT_FALSE(Player(target).IsGrounded());
    camera.feed.OnUpdate();
    SettleZoom(camera.follow);

    EXPECT_NEAR(camera.follow.Distance(), k_IdleDistance, 0.01f);
}

TEST_F(PlayerRelayTest, TargetWithoutEntityFeedsNothing)
{
    Scene scene;
    GameObject& target = SpawnTarget(scene, true, false);
    FeedCamera camera = AddFeedCamera(scene, target.Id());
    ASSERT_EQ(camera.follow.Target(), &target.Root());

    camera.feed.OnUpdate();
    SettleZoom(camera.follow);

    EXPECT_NEAR(camera.follow.Distance(), k_IdleDistance, 0.01f);
}

namespace
{
    namespace LevelNs = NS::Game::Level;

    constexpr float k_ChargeNarrowDegrees = 15.0f; // 欄「溜めで締める視野角」の既定 (度)
    constexpr float k_ChargeFrameRatio = 0.7f;     // 欄「溜めの構図の枠」の既定

    // 押しの入口は CollisionInput が読む実機のマウスなので、試しの後に押したまま残すと他の試しが押しを拾う
    struct MouseLeftPress
    {
        MouseLeftPress() noexcept { NS::Platform::Input::Get().Mouse().OnButtonDown(NS::Platform::MouseButton::Left); }
        ~MouseLeftPress() noexcept { NS::Platform::Input::Get().Mouse().ClearState(); }
        MouseLeftPress(const MouseLeftPress&) = delete;
        MouseLeftPress& operator=(const MouseLeftPress&) = delete;

        void Release() noexcept { NS::Platform::Input::Get().Mouse().OnButtonUp(NS::Platform::MouseButton::Left); }
    };

    struct ChargeRig
    {
        PlayerComponent* movement = nullptr;
        LevelNs::CollisionInput* input = nullptr;
        GameObject* player = nullptr;
    };

    // 自機の構成は型名から組む。自機の型の宣言を読むと、この試しの Player(GameObject&) と名前がぶつかる
    [[nodiscard]] nlohmann::json MakeChargePlayer()
    {
        nlohmann::json named = NS::Obj::MakeObjectJson();
        NS::Obj::SetObjectJsonClass(named, "Player");
        const std::unique_ptr<GameObject> prototype = NS::Obj::CreateRegisteredObject(named);
        nlohmann::json player = NS::Obj::MakePrototypeJson(*prototype);
        NS::Obj::SetObjectPosition(player, Vector3{0.0f, 1.41f, 0.0f});
        return player;
    }

    // 自機を原点に置き、+X の 5 m 先の床の上に壊せる的を 1 体置く
    ChargeRig BuildChargeCourse(Scene& scene, bool withCollisionInput)
    {
        nlohmann::json data = NS::Obj::MakeSceneJson();
        nlohmann::json player = MakeChargePlayer();
        NS::Obj::ObjectJsonComponents(player).push_back(NS::Obj::MakeComponentEntry("ImpactResolver"));
        if (withCollisionInput)
        {
            NS::Obj::ObjectJsonComponents(player).push_back(NS::Obj::MakeComponentEntry("CollisionInput"));
        }
        NS::Obj::SceneJsonObjects(data).push_back(player);
        for (std::int16_t i = -3; i <= 8; ++i)
        {
            NS::Obj::SceneJsonObjects(data).push_back(NS::Editor::MakeCellObject(i, 0, 0));
        }
        nlohmann::json target = NS::Editor::MakeCellObject(5, 1, 0);
        nlohmann::json rigidBody = NS::Obj::MakeComponentEntry("RigidBody");
        NS::Obj::SetField(rigidBody, "キネマティック", true);
        NS::Obj::ObjectJsonComponents(target).push_back(rigidBody);
        NS::Obj::ObjectJsonComponents(target).push_back(NS::Obj::MakeComponentEntry("Breakable"));
        NS::Obj::SceneJsonObjects(data).push_back(target);
        scene.LoadJson(std::move(data));

        ChargeRig rig;
        GameObject* live = nullptr;
        scene.Objects().ForEachComponent<PlayerComponent>([&live](PlayerComponent& movement) { live = movement.Owner(); });
        EXPECT_NE(live, nullptr);
        if (live != nullptr)
        {
            rig.player = live;
            rig.movement = live->FindComponent<PlayerComponent>();
            rig.input = live->FindComponent<LevelNs::CollisionInput>();
            // 起こしたままだと実機の入力が毎フレーム 0 を書き込み、試しが置いた狙いの向きが消える
            if (NS::Obj::PlayerInput* input = live->FindComponent<NS::Obj::PlayerInput>())
            {
                input->SetActive(false);
            }
        }
        return rig;
    }

    // 自機の帯 (Update まで) と移動を回し、追従カメラへ運んでからカメラを進める
    void StepWithCamera(Scene& scene, const ChargeRig& rig, const FeedCamera& camera)
    {
        scene.Objects().UpdateObjects(NS::Obj::TickPriority::EarlyUpdate, NS::Obj::TickPriority::Update);
        rig.movement->OnUpdate();
        camera.feed.OnUpdate();
        camera.follow.OnUpdate();
    }

    // 床へ着けてから、シーンの実カメラの正面と倒す向きを aim にする。
    // 落下が混ざると狙う相手を探す位置がフレームごとに動く
    void SettleAndAim(Scene& scene, const ChargeRig& rig, const FeedCamera& camera, const Vector3& aim)
    {
        for (int i = 0; i < 30 && !rig.movement->IsGrounded(); ++i)
        {
            StepWithCamera(scene, rig, camera);
        }
        ASSERT_TRUE(NsTest::FaceSceneCamera(scene, aim));
        rig.movement->SetDesiredMove(aim, 0.0f);
    }
} // namespace

// 押して溜めている間、追従カメラの締めは溜め量に連れて増え、同じフレームに届く
TEST_F(PlayerRelayTest, ChargeNarrowsTheCameraInTheSameFrame)
{
    Scene scene;
    ChargeRig rig = BuildChargeCourse(scene, true);
    ASSERT_NE(rig.input, nullptr);
    FeedCamera camera = AddFeedCamera(scene, rig.player->Id());
    SettleAndAim(scene, rig, camera, Vector3{1.0f, 0.0f, 0.0f});

    MouseLeftPress press;
    float lastNarrow = 0.0f;
    for (int frame = 0; frame < 40; ++frame)
    {
        StepWithCamera(scene, rig, camera);
        ASSERT_TRUE(rig.input->Judge().IsHeld());
        const float charge = rig.input->Judge().Charge01();
        EXPECT_NEAR(camera.follow.ChargeNarrowDegrees(), k_ChargeNarrowDegrees * charge, 1e-4f) << "frame " << frame;
        EXPECT_GE(camera.follow.ChargeNarrowDegrees(), lastNarrow) << "frame " << frame;
        lastNarrow = camera.follow.ChargeNarrowDegrees();
    }
    EXPECT_GT(lastNarrow, 0.0f);
}

// 放した後も溜め量は残るが、締めは放したフレームから戻り始め、6 フレーム目で 0
TEST_F(PlayerRelayTest, ChargeNarrowReturnsFromTheReleaseFrame)
{
    Scene scene;
    ChargeRig rig = BuildChargeCourse(scene, true);
    ASSERT_NE(rig.input, nullptr);
    FeedCamera camera = AddFeedCamera(scene, rig.player->Id());
    SettleAndAim(scene, rig, camera, Vector3{1.0f, 0.0f, 0.0f});

    MouseLeftPress press;
    for (int frame = 0; frame < 40; ++frame)
    {
        StepWithCamera(scene, rig, camera);
    }
    const float heldNarrow = camera.follow.ChargeNarrowDegrees();
    ASSERT_GT(heldNarrow, 0.0f);

    press.Release();
    StepWithCamera(scene, rig, camera);
    ASSERT_FALSE(rig.input->Judge().IsHeld());
    ASSERT_GT(rig.input->Judge().Charge01(), 0.0f);
    EXPECT_LT(camera.follow.ChargeNarrowDegrees(), heldNarrow);
    EXPECT_EQ(camera.follow.ChargeShake(), 0.0f);

    for (int frame = 2; frame <= 6; ++frame)
    {
        StepWithCamera(scene, rig, camera);
    }
    ASSERT_GT(rig.input->Judge().Charge01(), 0.0f);
    EXPECT_EQ(camera.follow.ChargeNarrowDegrees(), 0.0f);
}

// 狙う相手がいる間はその中心が渡って構図がずれ、カメラの正面を外して居なくなれば同じ押しの間でも 0 へ戻る
TEST_F(PlayerRelayTest, AimTargetCenterReachesTheCameraOnlyWhileThereIsOne)
{
    Scene scene;
    ChargeRig rig = BuildChargeCourse(scene, true);
    ASSERT_NE(rig.input, nullptr);
    FeedCamera camera = AddFeedCamera(scene, rig.player->Id());
    SettleAndAim(scene, rig, camera, Vector3{1.0f, 0.0f, 0.0f});

    MouseLeftPress press;
    LevelNs::SlamLineTarget aim{};
    for (int frame = 0; frame < 30; ++frame)
    {
        StepWithCamera(scene, rig, camera);
        ASSERT_TRUE(rig.input->TryGetAimTarget(aim));
    }
    EXPECT_GT(camera.follow.ChargeFrameOffset().x, 0.0f);

    ASSERT_TRUE(NsTest::FaceSceneCamera(scene, Vector3{-1.0f, 0.0f, 0.0f}));
    for (int frame = 0; frame < 60; ++frame)
    {
        StepWithCamera(scene, rig, camera);
        ASSERT_TRUE(rig.input->Judge().IsHeld());
        ASSERT_FALSE(rig.input->TryGetAimTarget(aim));
    }
    EXPECT_EQ(camera.follow.ChargeFrameOffset().x, 0.0f);
    EXPECT_EQ(camera.follow.ChargeFrameOffset().y, 0.0f);
}

// 狙う相手の外接箱の半分の長さの最大が半径として渡り、溜め切って収まった構図では相手の右の縁が枠 0.7 の端に来る
TEST_F(PlayerRelayTest, AimTargetRadiusReachesTheCameraFraming)
{
    Scene scene;
    ChargeRig rig = BuildChargeCourse(scene, true);
    ASSERT_NE(rig.input, nullptr);
    FeedCamera camera = AddFeedCamera(scene, rig.player->Id());
    // 3 m からでは 5 m 横の相手と自機が一緒に枠へ入らず、自機を残す側に倒れる。止まっていても 8 m から見る
    camera.follow.SetAutoDistances(k_RunDistance, k_RunDistance, k_JumpDistance);
    SettleAndAim(scene, rig, camera, Vector3{1.0f, 0.0f, 0.0f});

    MouseLeftPress press;
    LevelNs::SlamLineTarget aim{};
    for (int frame = 0; frame < 120; ++frame)
    {
        StepWithCamera(scene, rig, camera);
        ASSERT_TRUE(rig.input->TryGetAimTarget(aim));
    }
    ASSERT_NEAR(camera.follow.ChargeNarrowDegrees(), k_ChargeNarrowDegrees, 1e-4f);

    const float radius = std::max({aim.bounds.Extents.x, aim.bounds.Extents.y, aim.bounds.Extents.z});
    const Vector3 center{aim.bounds.Center.x, aim.bounds.Center.y, aim.bounds.Center.z};
    const NS::Obj::CameraPose pose = camera.follow.EvaluatePose(1.0f);
    EXPECT_NEAR(NsTest::ScreenOf(pose, center + NsTest::CameraRight(camera.follow) * radius).x, k_ChargeFrameRatio,
                1e-3f);
}

// CollisionInput の無い相手を追う時は、押し続けても締めも揺れもずらしも 0
TEST_F(PlayerRelayTest, WithoutCollisionInputTheCameraStaysUncharged)
{
    Scene scene;
    ChargeRig rig = BuildChargeCourse(scene, false);
    ASSERT_EQ(rig.input, nullptr);
    FeedCamera camera = AddFeedCamera(scene, rig.player->Id());
    SettleAndAim(scene, rig, camera, Vector3{1.0f, 0.0f, 0.0f});

    MouseLeftPress press;
    for (int frame = 0; frame < 40; ++frame)
    {
        StepWithCamera(scene, rig, camera);
    }
    EXPECT_EQ(camera.follow.ChargeNarrowDegrees(), 0.0f);
    EXPECT_EQ(camera.follow.ChargeShake(), 0.0f);
    EXPECT_EQ(camera.follow.ChargeFrameOffset().x, 0.0f);
    EXPECT_EQ(camera.follow.ChargeFrameOffset().y, 0.0f);
}

// プレイを終えると帯が回らず戻しが進まないので、締め・揺れ・ずらしはその場で 0 へ戻る
TEST_F(PlayerRelayTest, EndPlayClearsTheChargeView)
{
    Scene scene;
    ChargeRig rig = BuildChargeCourse(scene, true);
    ASSERT_NE(rig.input, nullptr);
    FeedCamera camera = AddFeedCamera(scene, rig.player->Id());
    SettleAndAim(scene, rig, camera, Vector3{1.0f, 0.0f, 0.0f});

    MouseLeftPress press;
    for (int frame = 0; frame < 40; ++frame)
    {
        StepWithCamera(scene, rig, camera);
    }
    ASSERT_GT(camera.follow.ChargeNarrowDegrees(), 0.0f);
    ASSERT_NE(camera.follow.ChargeFrameOffset().x, 0.0f);

    camera.feed.OnEndPlay();

    EXPECT_EQ(camera.follow.ChargeNarrowDegrees(), 0.0f);
    EXPECT_EQ(camera.follow.ChargeShake(), 0.0f);
    EXPECT_EQ(camera.follow.ChargeFrameOffset().x, 0.0f);
    EXPECT_EQ(camera.follow.ChargeFrameOffset().y, 0.0f);
}

namespace
{
    // 床の上で弾かれる反動。上がる間に自機が画面の帯を越えない高さにする
    const NS::Game::Player::ReboundArc k_RelayRebound{
        .direction = Vector3{-1.0f, 0.0f, 0.0f}, .apexHeight = 1.5f, .distance = 1.0f};

    // 床へ着けてから反動を始め、反動の始まる前のフレームの注視点の高さを返す
    [[nodiscard]] float BeginReboundOnTheFloor(Scene& scene, const ChargeRig& rig, const FeedCamera& camera)
    {
        SettleAndAim(scene, rig, camera, Vector3{1.0f, 0.0f, 0.0f});
        const float hitLookY = camera.follow.EvaluatePose(1.0f).target.y;
        EXPECT_TRUE(rig.movement->BeginRebound(k_RelayRebound));
        return hitLookY;
    }
} // namespace

// 自機の反動は同じフレームにカメラへ届き、
// 上がっていく間も注視点の高さは反動の始まる前のまま
TEST_F(PlayerRelayTest, ReboundReachesTheCameraAndHoldsTheLookHeight)
{
    Scene scene;
    ChargeRig rig = BuildChargeCourse(scene, false);
    FeedCamera camera = AddFeedCamera(scene, rig.player->Id());
    const float hitLookY = BeginReboundOnTheFloor(scene, rig, camera);
    const float startY = rig.player->Root().Position().y;

    for (int frame = 1; frame <= 15; ++frame)
    {
        StepWithCamera(scene, rig, camera);
        ASSERT_TRUE(rig.movement->IsRebounding()) << "frame " << frame;
        EXPECT_NEAR(camera.follow.EvaluatePose(1.0f).target.y, hitLookY, 1e-4f) << "frame " << frame;
    }
    EXPECT_GT(rig.player->Root().Position().y - startY, 0.5f);
}

// プレイを終えると帯が回らず戻しが進まないので、
// 反動の間の追い方はその場で普通の追い方へ戻る
TEST_F(PlayerRelayTest, EndPlayClearsTheReboundFollow)
{
    Scene scene;
    ChargeRig rig = BuildChargeCourse(scene, false);
    FeedCamera camera = AddFeedCamera(scene, rig.player->Id());
    const float hitLookY = BeginReboundOnTheFloor(scene, rig, camera);
    for (int frame = 1; frame <= 15; ++frame)
    {
        StepWithCamera(scene, rig, camera);
    }
    ASSERT_NEAR(camera.follow.EvaluatePose(1.0f).target.y, hitLookY, 1e-4f);

    camera.feed.OnEndPlay();

    // 終えると高さのずれも 0 へ戻すので、注視点は根に頭の高さを足した所
    const Vector3 root = rig.player->Root().Position();
    EXPECT_NEAR(camera.follow.EvaluatePose(1.0f).target.x, root.x, 1e-5f);
    EXPECT_NEAR(camera.follow.EvaluatePose(1.0f).target.y, root.y + 1.2f, 1e-5f);
}

// 自機が突進を出した向きは反動と一緒にカメラへ届き、
// カメラの水平の向きはその向きへ揃う
TEST_F(PlayerRelayTest, ReboundTurnsTheCameraToTheSlamDirection)
{
    Scene scene;
    ChargeRig rig = BuildChargeCourse(scene, false);
    FeedCamera camera = AddFeedCamera(scene, rig.player->Id());
    SettleAndAim(scene, rig, camera, Vector3{1.0f, 0.0f, 0.0f});
    ASSERT_EQ(camera.follow.Yaw(), 0.0f);

    rig.movement->RequestBodySlam(0.0f, Vector3{1.0f, 0.0f, 0.0f});
    StepWithCamera(scene, rig, camera);
    ASSERT_TRUE(rig.movement->IsBodySlamming());
    ASSERT_TRUE(rig.movement->BeginRebound(k_RelayRebound));

    for (int frame = 1; frame <= 20; ++frame)
    {
        StepWithCamera(scene, rig, camera);
        ASSERT_TRUE(rig.movement->IsRebounding()) << "frame " << frame;
    }
    EXPECT_NEAR(camera.follow.Yaw(), 0.5f * NS::Core::k_Pi, 1e-4f);
}
