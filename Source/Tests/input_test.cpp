#include <gtest/gtest.h>

#include <Framework/Platform/detail/input_win32.h>
#include <Framework/Platform/Gamepad.h>
#include <Framework/Platform/Input.h>
#include <Framework/Platform/Keyboard.h>
#include <Framework/Platform/Mouse.h>

#include "Framework/Framework.h"

namespace
{
    using NS::Platform::Gamepad;
    using NS::Platform::GamepadButton;
    using NS::Platform::Input;
    using NS::Platform::Key;
    using NS::Platform::Keyboard;
    using NS::Platform::Mouse;
    using NS::Platform::MouseButton;
} // namespace

TEST(NsPlatformKeyboard, IsHeldAfterKeyDown)
{
    Keyboard kb;
    kb.OnKeyDown(Key::Space);
    EXPECT_TRUE(kb.IsHeld(Key::Space));
}

TEST(NsPlatformKeyboard, IsPressedOnlyOneFrame)
{
    Keyboard kb;
    kb.OnKeyDown(Key::A);
    EXPECT_TRUE(kb.IsPressed(Key::A));
    EXPECT_TRUE(kb.IsHeld(Key::A));
    EXPECT_FALSE(kb.IsReleased(Key::A));

    kb.Update();
    EXPECT_FALSE(kb.IsPressed(Key::A));
    EXPECT_TRUE(kb.IsHeld(Key::A));
}

TEST(NsPlatformKeyboard, IsReleasedAfterKeyUp)
{
    Keyboard kb;
    kb.OnKeyDown(Key::Escape);
    kb.Update();
    kb.OnKeyUp(Key::Escape);

    EXPECT_FALSE(kb.IsHeld(Key::Escape));
    EXPECT_TRUE(kb.IsReleased(Key::Escape));
    EXPECT_FALSE(kb.IsPressed(Key::Escape));
}

TEST(NsPlatformKeyboard, ClearStateResetsCurrent)
{
    Keyboard kb;
    kb.OnKeyDown(Key::A);
    kb.OnKeyDown(Key::B);
    kb.OnKeyDown(Key::C);

    kb.ClearState();

    EXPECT_FALSE(kb.IsHeld(Key::A));
    EXPECT_FALSE(kb.IsHeld(Key::B));
    EXPECT_FALSE(kb.IsHeld(Key::C));
}

TEST(NsPlatformKeyboard, UnknownKeyIsNoop)
{
    Keyboard kb;
    kb.OnKeyDown(Key::Unknown);
    EXPECT_FALSE(kb.IsHeld(Key::Unknown));
    EXPECT_FALSE(kb.IsPressed(Key::Unknown));
}

TEST(NsPlatformKeyboard, OutOfRangeKeyIsNoop)
{
    Keyboard kb;
    const auto negativeKey = static_cast<Key>(-1);
    const auto overflowKey = static_cast<Key>(static_cast<int>(Key::kCount) + 10);

    kb.OnKeyDown(negativeKey);
    kb.OnKeyDown(overflowKey);

    EXPECT_FALSE(kb.IsHeld(negativeKey));
    EXPECT_FALSE(kb.IsHeld(overflowKey));
    EXPECT_FALSE(kb.IsPressed(negativeKey));
    EXPECT_FALSE(kb.IsPressed(overflowKey));
    EXPECT_FALSE(kb.IsReleased(negativeKey));
    EXPECT_FALSE(kb.IsReleased(overflowKey));
}

TEST(NsPlatformInput, UpdatePropagatesToKeyboard)
{
    Input input;
    input.Keyboard().OnKeyDown(Key::Enter);
    EXPECT_TRUE(input.Keyboard().IsPressed(Key::Enter));

    input.Update();
    EXPECT_FALSE(input.Keyboard().IsPressed(Key::Enter));
    EXPECT_TRUE(input.Keyboard().IsHeld(Key::Enter));
}

TEST(NsPlatformInputDetail, MapVkToKeyBasicAlpha)
{
    EXPECT_EQ(NS::Platform::MapVkToKey('A'), Key::A);
    EXPECT_EQ(NS::Platform::MapVkToKey('Z'), Key::Z);
    EXPECT_EQ(NS::Platform::MapVkToKey('0'), Key::Num0);
    EXPECT_EQ(NS::Platform::MapVkToKey('9'), Key::Num9);
    EXPECT_EQ(NS::Platform::MapVkToKey(VK_SPACE), Key::Space);
    EXPECT_EQ(NS::Platform::MapVkToKey(VK_ESCAPE), Key::Escape);
    EXPECT_EQ(NS::Platform::MapVkToKey(VK_F1), Key::F1);

    EXPECT_EQ(NS::Platform::MapVkToKey(0xFFFF), Key::Unknown);
    EXPECT_EQ(NS::Platform::MapVkToKey(0x00), Key::Unknown);
}

TEST(NsPlatformMouse, IsHeldAfterButtonDown)
{
    Mouse m;
    m.OnButtonDown(MouseButton::Left);
    EXPECT_TRUE(m.IsHeld(MouseButton::Left));
}

TEST(NsPlatformMouse, PressedOnlyOneFrame)
{
    Mouse m;
    m.OnButtonDown(MouseButton::Right);
    EXPECT_TRUE(m.IsPressed(MouseButton::Right));
    EXPECT_TRUE(m.IsHeld(MouseButton::Right));

    m.Update();
    EXPECT_FALSE(m.IsPressed(MouseButton::Right));
    EXPECT_TRUE(m.IsHeld(MouseButton::Right));
}

TEST(NsPlatformMouse, ReleasedAfterButtonUp)
{
    Mouse m;
    m.OnButtonDown(MouseButton::Middle);
    m.Update();
    m.OnButtonUp(MouseButton::Middle);

    EXPECT_FALSE(m.IsHeld(MouseButton::Middle));
    EXPECT_TRUE(m.IsReleased(MouseButton::Middle));
    EXPECT_FALSE(m.IsPressed(MouseButton::Middle));
}

TEST(NsPlatformMouse, OutOfRangeButtonIsNoop)
{
    Mouse m;
    const auto negativeButton = static_cast<MouseButton>(-1);
    const auto overflowButton = static_cast<MouseButton>(static_cast<int>(MouseButton::kCount) + 10);

    m.OnButtonDown(negativeButton);
    m.OnButtonDown(overflowButton);

    EXPECT_FALSE(m.IsHeld(negativeButton));
    EXPECT_FALSE(m.IsHeld(overflowButton));
    EXPECT_FALSE(m.IsPressed(negativeButton));
    EXPECT_FALSE(m.IsReleased(overflowButton));
}

TEST(NsPlatformMouse, PositionAndDelta)
{
    Mouse m;
    m.OnMove(10, 20);
    EXPECT_EQ(m.GetX(), 10);
    EXPECT_EQ(m.GetY(), 20);

    m.Update();
    m.OnMove(15, 30);
    EXPECT_EQ(m.GetX(), 15);
    EXPECT_EQ(m.GetY(), 30);
    EXPECT_EQ(m.GetDeltaX(), 5);
    EXPECT_EQ(m.GetDeltaY(), 10);
}

TEST(NsPlatformMouse, WheelAccumulatesAndResetsOnUpdate)
{
    Mouse m;
    m.OnWheel(120);
    m.OnWheel(120);
    EXPECT_EQ(m.GetWheelDelta(), 240);

    m.Update();
    EXPECT_EQ(m.GetWheelDelta(), 0);
}

TEST(NsPlatformMouse, ClearStateResetsButtonsAndWheel)
{
    Mouse m;
    m.OnButtonDown(MouseButton::Left);
    m.OnButtonDown(MouseButton::Right);
    m.OnWheel(120);

    m.ClearState();

    EXPECT_FALSE(m.IsHeld(MouseButton::Left));
    EXPECT_FALSE(m.IsHeld(MouseButton::Right));
    EXPECT_EQ(m.GetWheelDelta(), 0);
}

TEST(NsPlatformInput, UpdatePropagatesToMouse)
{
    Input input;
    input.Mouse().OnButtonDown(MouseButton::Left);
    input.Mouse().OnWheel(120);
    EXPECT_TRUE(input.Mouse().IsPressed(MouseButton::Left));
    EXPECT_EQ(input.Mouse().GetWheelDelta(), 120);

    input.Update();
    EXPECT_FALSE(input.Mouse().IsPressed(MouseButton::Left));
    EXPECT_TRUE(input.Mouse().IsHeld(MouseButton::Left));
    EXPECT_EQ(input.Mouse().GetWheelDelta(), 0);
}

TEST(NsPlatformGamepad, DefaultIsNotConnected)
{
    Gamepad pad;
    EXPECT_FALSE(pad.IsConnected());
}

TEST(NsPlatformGamepad, DefaultAllButtonsReleased)
{
    Gamepad pad;
    EXPECT_FALSE(pad.IsHeld(GamepadButton::A));
    EXPECT_FALSE(pad.IsHeld(GamepadButton::B));
    EXPECT_FALSE(pad.IsHeld(GamepadButton::Start));
    EXPECT_FALSE(pad.IsHeld(GamepadButton::DPadUp));
    EXPECT_FALSE(pad.IsPressed(GamepadButton::A));
    EXPECT_FALSE(pad.IsReleased(GamepadButton::A));
}

TEST(NsPlatformGamepad, DefaultStickAndTriggerZero)
{
    Gamepad pad;
    EXPECT_FLOAT_EQ(pad.LeftStick().x, 0.0f);
    EXPECT_FLOAT_EQ(pad.LeftStick().y, 0.0f);
    EXPECT_FLOAT_EQ(pad.RightStick().x, 0.0f);
    EXPECT_FLOAT_EQ(pad.RightStick().y, 0.0f);
    EXPECT_FLOAT_EQ(pad.LeftTrigger(), 0.0f);
    EXPECT_FLOAT_EQ(pad.RightTrigger(), 0.0f);
}

TEST(NsPlatformGamepad, OutOfRangeButtonIsNoop)
{
    Gamepad pad;
    const auto negative = static_cast<GamepadButton>(-1);
    const auto overflow = static_cast<GamepadButton>(static_cast<int>(GamepadButton::kCount) + 10);

    EXPECT_FALSE(pad.IsHeld(negative));
    EXPECT_FALSE(pad.IsHeld(overflow));
    EXPECT_FALSE(pad.IsPressed(negative));
    EXPECT_FALSE(pad.IsReleased(overflow));
}

TEST(NsPlatformGamepad, UpdateIsSafeToCall)
{
    Gamepad pad{0};
    pad.Update();
    pad.Update();
    EXPECT_FALSE(pad.IsHeld(static_cast<GamepadButton>(-1)));
}

TEST(NsPlatformInput, GamepadAccessorReturnsSameInstance)
{
    Input input;
    Gamepad* p1 = &input.Gamepad(0);
    Gamepad* p2 = &input.Gamepad(0);
    EXPECT_EQ(p1, p2);
}

TEST(NsPlatformInput, OutOfRangeGamepadIndexFallsBackToSlotZero)
{
    Input input;
    const Gamepad* slotZero = &input.Gamepad(0);
    EXPECT_EQ(&input.Gamepad(-1), slotZero);
    EXPECT_EQ(&input.Gamepad(99), slotZero);
}

TEST(NsPlatformInput, UpdateIsSafeToCallTwice)
{
    Input input;
    input.Update();
    input.Update();
    EXPECT_FALSE(input.Gamepad(0).IsHeld(static_cast<GamepadButton>(-1)));
}
