#include <Game/Level/FollowCameraFeed.h>
#include <Game/Player/PlayerComponent.h>
#include <Game/Player/PlayerInputRelay.h>
#include <Runtime/Platform/Clock.h>
#include <Runtime/Core/Math.h>
#include <Runtime/Object/Components/PlayerInput.h>
#include <Runtime/Object/Components/ThirdPersonFollow.h>
#include <Runtime/Object/GameObject.h>
#include <Runtime/Object/Object.h>
#include <Runtime/Object/Reflection/Reflection.h>
#include <Runtime/Object/Scene/Scene.h>
#include <Runtime/Object/Transform.h>
#include <Runtime/Object/ObjectList.h>
#include <Runtime/Platform/Input.h>
#include <Runtime/Platform/Keyboard.h>
#include <gtest/gtest.h>

#include "tuning_field_access.h"

#include <cstdint>
#include <string_view>

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
