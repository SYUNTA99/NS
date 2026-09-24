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

#include <cstdint>
#include <string_view>
#include <vector>

namespace
{
    using NS::Core::Vector3;
    using NS::Game::Level::FollowCameraFeed;
    using NS::Game::Player::PlayerComponent;
    using NS::Game::Player::PlayerInputRelay;
    using NS::Obj::GameObject;
    using NS::Obj::ObjectIdAccess;
    using NS::Obj::ObjectRef;
    using NS::Obj::Scene;
    using NS::Obj::ThirdPersonFollow;
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

    FollowCameraFeed& Feed(GameObject& owner)
    {
        return *owner.FindComponent<FollowCameraFeed>();
    }

    //! 追従先はリフレクション経由でしか書けない。データからの構築と同じ set を通す
    void SetTargetRef(NS::Obj::Component& comp, std::uint32_t id)
    {
        const NS::Obj::ReflectionInfo* info = comp.GetReflection();
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
    ThirdPersonFollow& AddFollowCamera(Scene& scene)
    {
        GameObject* rig = scene.SpawnTransient<GameObject>();
        auto& follow = *rig->AddComponent<ThirdPersonFollow>();
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
            owner->AddComponent<PlayerComponent>();
        }
        owner->AddComponent<FollowCameraFeed>();
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
        auto& kb = NS::Platform::Input::Get().Keyboard();
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

    auto& kb = NS::Platform::Input::Get().Keyboard();
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
    auto& player = *withoutInput.AddComponent<PlayerComponent>();
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

// 持ち主を追うカメラが無ければ、持ち主を追う 1 台を配置物として足す。次のフレームからはそれへ渡す
TEST_F(PlayerRelayTest, FeedSpawnsACameraWhenNoneFollowsTheOwner)
{
    Scene scene;
    GameObject& owner = SpawnFeeder(scene, k_PlayerId, true);

    Feed(owner).OnUpdate();
    Feed(owner).OnUpdate();

    std::vector<ThirdPersonFollow*> following;
    scene.Objects().ForEachComponent<ThirdPersonFollow>([&following](ThirdPersonFollow& follow) {
        if (follow.TargetRef().id == k_PlayerId)
            following.push_back(&follow);
    });
    ASSERT_EQ(following.size(), 1u);
    EXPECT_TRUE(following[0]->IsActive());
    EXPECT_EQ(following[0]->Target(), &owner.Root());
    // 一時オブジェクトだと組み直しを越えて残り、作り直された持ち主の代わりに古い Transform を指す
    EXPECT_FALSE(following[0]->Owner()->IsTransient());
}

TEST_F(PlayerRelayTest, FeedDoesNotSpawnWhileACameraFollowsTheOwner)
{
    Scene scene;
    GameObject& owner = SpawnFeeder(scene, k_PlayerId, true);
    auto& follow = AddFollowCamera(scene);
    SetTargetRef(follow, k_PlayerId);
    follow.OnStart();
    const std::size_t before = scene.Objects().ObjectCount();

    Feed(owner).OnUpdate();

    EXPECT_EQ(scene.Objects().ObjectCount(), before);
}
