#include "Runtime/Platform/Input.h"

#include <gtest/gtest.h>

// エディタの下見が、手元の機器の入力を読まず、振動を手元のパッドへ送らずに場面を進められる事を縛る

// 中立の間は押しているキーが見えず、終わると元の押しが戻る
TEST(NeutralInput, HidesHeldKeysAndRestoresThem)
{
    NS::Platform::Input& input = NS::Platform::Input::Get();
    input.Keyboard().OnKeyDown(NS::Platform::Key::A);
    ASSERT_TRUE(input.Keyboard().IsHeld(NS::Platform::Key::A));
    {
        const NS::Platform::ScopedNeutralInput neutral;
        EXPECT_TRUE(input.IsNeutral());
        EXPECT_FALSE(input.Keyboard().IsHeld(NS::Platform::Key::A));
    }
    EXPECT_FALSE(input.IsNeutral());
    EXPECT_TRUE(input.Keyboard().IsHeld(NS::Platform::Key::A));
    input.Keyboard().OnKeyUp(NS::Platform::Key::A);
}

// 中立の間に書いた振動は読めるが、手元のパッドの値は変わらない
TEST(NeutralInput, HoldsVibrationAwayFromThePad)
{
    NS::Platform::Input& input = NS::Platform::Input::Get();
    ASSERT_TRUE(input.Gamepad().SetVibration(0.2f, 0.1f));
    {
        const NS::Platform::ScopedNeutralInput neutral;
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
    NS::Platform::Input& input = NS::Platform::Input::Get();
    input.Keyboard().OnKeyDown(NS::Platform::Key::B);
    {
        const NS::Platform::ScopedNeutralInput outer;
        {
            const NS::Platform::ScopedNeutralInput inner;
            EXPECT_TRUE(input.IsNeutral());
        }
        EXPECT_TRUE(input.IsNeutral());
        EXPECT_FALSE(input.Keyboard().IsHeld(NS::Platform::Key::B));
    }
    EXPECT_FALSE(input.IsNeutral());
    EXPECT_TRUE(input.Keyboard().IsHeld(NS::Platform::Key::B));
    input.Keyboard().OnKeyUp(NS::Platform::Key::B);
}
