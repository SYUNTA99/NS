#include <Runtime/Object/Components/PlayerInput.h>
#include <Runtime/Object/GameObject.h>
#include <Runtime/Platform/Input.h>
#include <Runtime/Platform/Keyboard.h>
#include <gtest/gtest.h>

namespace
{
    using NS::Obj::GameObject;
    using NS::Obj::PlayerInput;
    using NS::Platform::Key;

    //! 入力はプロセスに 1 個しか無い。押したキーを次のテストへ持ち越さないよう前後で払う
    class PlayerInputHeldValues : public ::testing::Test
    {
    protected:
        void SetUp() override { ClearKeyboard(); }
        void TearDown() override { ClearKeyboard(); }

        //! ClearState は現在の押下しか消さない。前フレームの控えは Update で揃える
        static void ClearKeyboard() noexcept
        {
            NS::Platform::Keyboard& kb = NS::Platform::Input::Get().Keyboard();
            kb.ClearState();
            kb.Update();
        }

        //! 押しっぱなしのままフレームを 1 つ進める。押した瞬間の判定はここで消える
        static void AdvanceFrame() noexcept { NS::Platform::Input::Get().Keyboard().Update(); }
    };
} // namespace

TEST(PlayerInputTest, ConstructsActive)
{
    PlayerInput input;
    EXPECT_TRUE(input.IsActive());
}

TEST(PlayerInputTest, OnUpdateRunsWithoutAnOwner)
{
    PlayerInput input;
    input.OnUpdate();
    SUCCEED();
}

TEST(PlayerInputTest, CameraForwardSetterPersists)
{
    PlayerInput input;
    input.SetCameraForward({1.0f, 5.0f, 0.0f});
    SUCCEED();
}

TEST_F(PlayerInputHeldValues, ForwardKeyProducesCameraRelativeDirection)
{
    GameObject obj;
    PlayerInput& input = *obj.AddComponent<PlayerInput>();
    obj.OnStart();
    input.SetCameraForward({0.0f, 0.0f, 1.0f});

    NS::Platform::Input::Get().Keyboard().OnKeyDown(Key::W);
    input.OnUpdate();

    EXPECT_GT(input.DesiredDirection().z, 0.9f);
    EXPECT_NEAR(input.DesiredDirection().x, 0.0f, 1e-5f);
    EXPECT_GT(input.DesiredSpeedScale(), 0.0f);
}

TEST_F(PlayerInputHeldValues, NoKeyLeavesSpeedScaleAtZero)
{
    GameObject obj;
    PlayerInput& input = *obj.AddComponent<PlayerInput>();
    obj.OnStart();
    input.SetCameraForward({0.0f, 0.0f, 1.0f});

    input.OnUpdate();

    EXPECT_FLOAT_EQ(input.DesiredSpeedScale(), 0.0f);
}

TEST_F(PlayerInputHeldValues, JumpPressedIsTrueOnlyForTheStepOfThePress)
{
    GameObject obj;
    PlayerInput& input = *obj.AddComponent<PlayerInput>();
    obj.OnStart();

    NS::Platform::Input::Get().Keyboard().OnKeyDown(Key::Space);
    input.OnUpdate();
    EXPECT_TRUE(input.JumpPressed());

    AdvanceFrame();
    input.OnUpdate();
    EXPECT_FALSE(input.JumpPressed());
}

TEST_F(PlayerInputHeldValues, JumpHeldFollowsTheKeyState)
{
    GameObject obj;
    PlayerInput& input = *obj.AddComponent<PlayerInput>();
    obj.OnStart();

    NS::Platform::Keyboard& kb = NS::Platform::Input::Get().Keyboard();
    kb.OnKeyDown(Key::Space);
    input.OnUpdate();
    EXPECT_TRUE(input.JumpHeld());

    AdvanceFrame();
    input.OnUpdate();
    EXPECT_TRUE(input.JumpHeld());

    kb.OnKeyUp(Key::Space);
    AdvanceFrame();
    input.OnUpdate();
    EXPECT_FALSE(input.JumpHeld());
}

TEST_F(PlayerInputHeldValues, ClimbMoveKeepsRawLocalInput)
{
    GameObject obj;
    PlayerInput& input = *obj.AddComponent<PlayerInput>();
    obj.OnStart();
    input.SetCameraForward({1.0f, 0.0f, 0.0f});

    NS::Platform::Input::Get().Keyboard().OnKeyDown(Key::D);
    input.OnUpdate();

    EXPECT_FLOAT_EQ(input.ClimbRight(), 1.0f);
    EXPECT_FLOAT_EQ(input.ClimbForward(), 0.0f);
}

TEST_F(PlayerInputHeldValues, InactiveStepKeepsThePreviousValues)
{
    GameObject obj;
    PlayerInput& input = *obj.AddComponent<PlayerInput>();
    obj.OnStart();
    input.SetCameraForward({0.0f, 0.0f, 1.0f});

    NS::Platform::Keyboard& kb = NS::Platform::Input::Get().Keyboard();
    kb.OnKeyDown(Key::W);
    input.OnUpdate();
    const float held = input.DesiredSpeedScale();

    input.SetActive(false);
    kb.OnKeyUp(Key::W);
    AdvanceFrame();
    input.OnUpdate();

    EXPECT_FLOAT_EQ(input.DesiredSpeedScale(), held);
}
