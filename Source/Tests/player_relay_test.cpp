#include <Game/Level/FollowCameraFeedComponent.h>
#include <Game/Player/PlayerComponent.h>
#include <Game/Player/PlayerInputRelayComponent.h>
#include <Runtime/Core/Clock.h>
#include <Runtime/Core/Math.h>
#include <Runtime/Object/Components/PlayerInputComponent.h>
#include <Runtime/Object/Components/ThirdPersonFollowComponent.h>
#include <Runtime/Object/GameObject.h>
#include <Runtime/Object/Object.h>
#include <Runtime/Object/Reflection/Reflection.h>
#include <Runtime/Object/Scene/Scene.h>
#include <Runtime/Object/Transform.h>
#include <Runtime/Object/World.h>
#include <Runtime/Platform/Input.h>
#include <Runtime/Platform/Keyboard.h>
#include <gtest/gtest.h>

#include <cstdint>
#include <string_view>

namespace
{
    using NS::Core::Vector3;
    using NS::Game::Level::FollowCameraFeedComponent;
    using NS::Game::Player::PlayerComponent;
    using NS::Game::Player::PlayerInputRelayComponent;
    using NS::Object::GameObject;
    using NS::Object::ObjectIdAccess;
    using NS::Object::ObjectRef;
    using NS::Object::Scene;
    using NS::Object::ThirdPersonFollowComponent;
    using NS::Platform::Key;

    constexpr float k_FixedDt = 1.0f / 60.0f;

    constexpr float k_IdleDistance = 3.0f;
    constexpr float k_RunDistance = 8.0f;
    constexpr float k_JumpDistance = 12.0f;
    constexpr float k_RunSpeedThreshold = 4.0f;

    constexpr std::uint32_t k_PlayerId = 11u;
    constexpr std::uint32_t k_StrangerId = 99u;

    void BuildRelayRig(GameObject& owner)
    {
        owner.AddComponent<NS::Object::PlayerInputComponent>();
        owner.AddComponent<PlayerInputRelayComponent>();
        auto& player = *owner.AddComponent<PlayerComponent>();
        player.SetDebugDrawEnabled(false);
        owner.OnStart();
    }

    NS::Object::PlayerInputComponent& Input(GameObject& owner)
    {
        return *owner.FindComponent<NS::Object::PlayerInputComponent>();
    }

    PlayerInputRelayComponent& Relay(GameObject& owner)
    {
        return *owner.FindComponent<PlayerInputRelayComponent>();
    }

    PlayerComponent& Player(GameObject& owner)
    {
        return *owner.FindComponent<PlayerComponent>();
    }

    FollowCameraFeedComponent& Feed(GameObject& owner)
    {
        return *owner.FindComponent<FollowCameraFeedComponent>();
    }

    //! 追従先はリフレクション経由でしか書けない。データからの構築と同じ set を通す
    void SetTargetRef(NS::Object::Component& comp, std::uint32_t id)
    {
        const NS::Object::ReflectionInfo* info = comp.GetReflection();
        ASSERT_NE(info, nullptr);
        for (std::size_t i = 0; i < info->fieldCount; ++i)
        {
            if (std::string_view{info->fields[i].name} != "追従対象")
                continue;
            const ObjectRef ref{id};
            info->fields[i].set(&comp, &ref);
            return;
        }
        FAIL() << "追従対象フィールドがリフレクションに無い";
    }

    //! 3 段の距離を既定値から離して置く。どの段に寄ったかを距離 1 つで見分けられる
    ThirdPersonFollowComponent& AddFollowCamera(Scene& scene)
    {
        GameObject* rig = scene.SpawnTransient<GameObject>();
        auto& follow = *rig->AddComponent<ThirdPersonFollowComponent>();
        follow.SetActive(true);
        follow.SetAutoDistances(k_IdleDistance, k_RunDistance, k_JumpDistance);
        follow.SetRunSpeedThreshold(k_RunSpeedThreshold);
        return follow;
    }

    GameObject& SpawnFeeder(Scene& scene, std::uint32_t id, bool withEntity)
    {
        GameObject* owner = scene.SpawnTransient<GameObject>();
        ObjectIdAccess::SetId(*owner, id);
        if (withEntity)
        {
            auto& player = *owner->AddComponent<PlayerComponent>();
            player.SetDebugDrawEnabled(false);
        }
        owner->AddComponent<FollowCameraFeedComponent>();
        owner->OnStart();
        return *owner;
    }

    void SettleZoom(ThirdPersonFollowComponent& follow)
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
        NS::Core::FrameTimer::SetFixedDelta(k_FixedDt);
        ClearKeyboard();
    }
    void TearDown() override { ClearKeyboard(); }

    //! 入力はプロセスに 1 個しか無い。押したキーを次のテストへ持ち越さないよう前後で払う
    static void ClearKeyboard() noexcept
    {
        auto& kb = NS::Platform::Input::Get().Keyboard();
        kb.ClearState();
        kb.Update();
    }

    //! 押しっぱなしのまま歩を 1 つ進める。押した瞬間の判定はここで消える
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

// 押しっぱなしの歩まで押下を渡すと、押していない歩に跳ぶ
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

    auto& kb = NS::Platform::Input::Get().Keyboard();
    kb.OnKeyDown(Key::Space);
    Input(owner).OnUpdate();
    Relay(owner).OnUpdate();
    Player(owner).OnUpdate();

    kb.OnKeyUp(Key::Space);
    AdvanceFrame();
    Input(owner).OnUpdate();
    Relay(owner).OnUpdate();

    // 離した歩が届いていれば上昇が縮む
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
    withoutPlayer.AddComponent<NS::Object::PlayerInputComponent>();
    withoutPlayer.AddComponent<PlayerInputRelayComponent>();
    withoutPlayer.OnStart();
    Relay(withoutPlayer).OnUpdate();

    GameObject withoutInput;
    withoutInput.AddComponent<PlayerInputRelayComponent>();
    auto& player = *withoutInput.AddComponent<PlayerComponent>();
    player.SetDebugDrawEnabled(false);
    withoutInput.OnStart();

    player.SetDesiredMove(Vector3{1.0f, 0.0f, 0.0f}, 0.5f);
    Relay(withoutInput).OnUpdate();

    EXPECT_FLOAT_EQ(player.DesiredSpeedScale(), 0.5f);
}

TEST_F(PlayerRelayTest, AirborneOwnerZoomsTheFollowingCamera)
{
    Scene scene;
    GameObject& owner = SpawnFeeder(scene, k_PlayerId, true);
    auto& follow = AddFollowCamera(scene);
    SetTargetRef(follow, k_PlayerId);
    follow.OnStart();
    ASSERT_EQ(follow.Target(), &owner.Root());

    ASSERT_FALSE(Player(owner).IsGrounded());
    Feed(owner).OnUpdate();
    SettleZoom(follow);

    EXPECT_NEAR(follow.Distance(), k_JumpDistance, 0.01f);
}

TEST_F(PlayerRelayTest, GroundedRunSpeedReachesTheFollowingCamera)
{
    Scene scene;
    GameObject& owner = SpawnFeeder(scene, k_PlayerId, true);
    auto& follow = AddFollowCamera(scene);
    SetTargetRef(follow, k_PlayerId);
    follow.OnStart();

    Player(owner).SetGrounded(true);
    Player(owner).SetVelocity(Vector3{10.0f, 0.0f, 0.0f});
    Feed(owner).OnUpdate();
    SettleZoom(follow);

    EXPECT_NEAR(follow.Distance(), k_RunDistance, 0.01f);
}

TEST_F(PlayerRelayTest, CameraFollowingSomeoneElseIsNotFed)
{
    Scene scene;
    GameObject& owner = SpawnFeeder(scene, k_PlayerId, true);
    GameObject* stranger = scene.SpawnTransient<GameObject>();
    ObjectIdAccess::SetId(*stranger, k_StrangerId);

    auto& follow = AddFollowCamera(scene);
    SetTargetRef(follow, k_StrangerId);
    follow.OnStart();
    ASSERT_EQ(follow.Target(), &stranger->Root());

    ASSERT_FALSE(Player(owner).IsGrounded());
    Feed(owner).OnUpdate();
    SettleZoom(follow);

    EXPECT_NEAR(follow.Distance(), k_IdleDistance, 0.01f);
}

TEST_F(PlayerRelayTest, UnsetReferenceDoesNotMatchTheUnnumberedOwner)
{
    Scene scene;
    GameObject& owner = SpawnFeeder(scene, 0u, true);
    auto& follow = AddFollowCamera(scene);
    follow.SetTarget(&owner.Root());
    ASSERT_FALSE(follow.TargetRef().IsSet());

    ASSERT_FALSE(Player(owner).IsGrounded());
    Feed(owner).OnUpdate();
    SettleZoom(follow);

    EXPECT_NEAR(follow.Distance(), k_IdleDistance, 0.01f);
}

TEST_F(PlayerRelayTest, OwnerWithoutEntityFeedsNothing)
{
    Scene scene;
    GameObject& owner = SpawnFeeder(scene, k_PlayerId, false);
    auto& follow = AddFollowCamera(scene);
    SetTargetRef(follow, k_PlayerId);
    follow.OnStart();
    ASSERT_EQ(follow.Target(), &owner.Root());

    Feed(owner).OnUpdate();
    SettleZoom(follow);

    EXPECT_NEAR(follow.Distance(), k_IdleDistance, 0.01f);
}

TEST_F(PlayerRelayTest, FeedWithoutAnyCameraIsHarmless)
{
    Scene scene;
    GameObject& owner = SpawnFeeder(scene, k_PlayerId, true);

    Feed(owner).OnUpdate();

    SUCCEED();
}
