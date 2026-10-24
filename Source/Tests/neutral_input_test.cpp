#include "NSlib/Core/Windows/Input.h"
#include "NSlib/Core/Windows/detail/InputWin32.h"

#include <gtest/gtest.h>

// エディタの下見が、手元の機器の入力を読まず、振動を手元のパッドへ送らずに場面を進められる事を縛る

// 中立の間は押しているキーが見えず、終わると元の押しが戻る
TEST(NeutralInput, HidesHeldKeysAndRestoresThem)
{
    NS::OS::Input& input = NS::OS::Input::Get();
    input.Keyboard().OnKeyDown(NS::OS::Key::A);
    ASSERT_TRUE(input.Keyboard().IsHeld(NS::OS::Key::A));
    {
        const NS::OS::ScopedNeutralInput neutral;
        EXPECT_TRUE(input.IsNeutral());
        EXPECT_FALSE(input.Keyboard().IsHeld(NS::OS::Key::A));
    }
    EXPECT_FALSE(input.IsNeutral());
    EXPECT_TRUE(input.Keyboard().IsHeld(NS::OS::Key::A));
    input.Keyboard().OnKeyUp(NS::OS::Key::A);
}

// 中立の間に書いた振動は読めるが、手元のパッドの値は変わらない
TEST(NeutralInput, HoldsVibrationAwayFromThePad)
{
    NS::OS::Input& input = NS::OS::Input::Get();
    ASSERT_TRUE(input.Gamepad().SetVibration(0.2f, 0.1f));
    {
        const NS::OS::ScopedNeutralInput neutral;
        EXPECT_FLOAT_EQ(input.Gamepad().Vibration().left, 0.0f);
        ASSERT_TRUE(input.Gamepad().SetVibration(0.9f, 0.8f));
        EXPECT_FLOAT_EQ(input.Gamepad().Vibration().left, 0.9f);
    }
    EXPECT_FLOAT_EQ(input.Gamepad().Vibration().left, 0.2f);
    EXPECT_FLOAT_EQ(input.Gamepad().Vibration().right, 0.1f);
    input.Gamepad().StopVibration();
}

// 重ねて始めても、外側が終わるまで中立のまま
TEST(NeutralInput, NestedScopesEndWithTheOuterOne)
{
    NS::OS::Input& input = NS::OS::Input::Get();
    input.Keyboard().OnKeyDown(NS::OS::Key::B);
    {
        const NS::OS::ScopedNeutralInput outer;
        {
            const NS::OS::ScopedNeutralInput inner;
            EXPECT_TRUE(input.IsNeutral());
        }
        EXPECT_TRUE(input.IsNeutral());
        EXPECT_FALSE(input.Keyboard().IsHeld(NS::OS::Key::B));
    }
    EXPECT_FALSE(input.IsNeutral());
    EXPECT_TRUE(input.Keyboard().IsHeld(NS::OS::Key::B));
    input.Keyboard().OnKeyUp(NS::OS::Key::B);
}

// 中立の間に OS から届いた離しは手元のキーボードへ書かれ、終わった後に押したままが残らない
TEST(NeutralInput, ReleaseDuringNeutralReachesTheHeldKeyboard)
{
    NS::OS::Input& input = NS::OS::Input::Get();
    input.Keyboard().OnKeyDown(NS::OS::Key::A);
    {
        const NS::OS::ScopedNeutralInput neutral;
        NS::OS::DispatchWin32MessageToInput(input, WM_KEYUP, 'A', 0);
    }
    EXPECT_FALSE(input.Keyboard().IsHeld(NS::OS::Key::A));
    input.Keyboard().OnKeyUp(NS::OS::Key::A);
}

// 中立の間に決めた相対移動は、終わった後の手元のマウスに付いている
TEST(NeutralInput, RelativeModeSetDuringNeutralStaysOnTheHeldMouse)
{
    NS::OS::Input& input = NS::OS::Input::Get();
    {
        const NS::OS::ScopedNeutralInput neutral;
        input.SetMouseRelativeMode(true);
    }
    EXPECT_TRUE(input.Mouse().IsRelativeMode());
    input.SetMouseRelativeMode(false);
}

// 生のマウス移動も、ほかのマウスの入力と同じく UI がマウスを持つ間はゲームへ流さない
TEST(NeutralInput, RawMouseMoveIsTakenWhileUiHasTheMouse)
{
    NS::OS::Input& input = NS::OS::Input::Get();
    input.SetUiCapture(NS::OS::UiCaptureDesc{.wantMouse = true});
    EXPECT_TRUE(NS::OS::IsTakenByUi(input, WM_INPUT));
    input.SetUiCapture(NS::OS::UiCaptureDesc{});
    EXPECT_FALSE(NS::OS::IsTakenByUi(input, WM_INPUT));
}
