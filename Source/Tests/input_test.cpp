#include <gtest/gtest.h>

#include <ns/platform/detail/input_win32.h>
#include <ns/platform/input.h>
#include <ns/platform/keyboard.h>
#include <ns/platform/mouse.h>

#include <windows.h>

namespace
{
    using ns::platform::Input;
    using ns::platform::Key;
    using ns::platform::Keyboard;
    using ns::platform::Mouse;
    using ns::platform::MouseButton;
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
    EXPECT_EQ(ns::platform::MapVkToKey('A'), Key::A);
    EXPECT_EQ(ns::platform::MapVkToKey('Z'), Key::Z);
    EXPECT_EQ(ns::platform::MapVkToKey('0'), Key::Num0);
    EXPECT_EQ(ns::platform::MapVkToKey('9'), Key::Num9);
    EXPECT_EQ(ns::platform::MapVkToKey(VK_SPACE), Key::Space);
    EXPECT_EQ(ns::platform::MapVkToKey(VK_ESCAPE), Key::Escape);
    EXPECT_EQ(ns::platform::MapVkToKey(VK_F1), Key::F1);

    EXPECT_EQ(ns::platform::MapVkToKey(0xFFFF), Key::Unknown);
    EXPECT_EQ(ns::platform::MapVkToKey(0x00), Key::Unknown);
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
    EXPECT_EQ(m.X(), 10);
    EXPECT_EQ(m.Y(), 20);

    m.Update();
    m.OnMove(15, 30);
    EXPECT_EQ(m.X(), 15);
    EXPECT_EQ(m.Y(), 30);
    EXPECT_EQ(m.DeltaX(), 5);
    EXPECT_EQ(m.DeltaY(), 10);
}

TEST(NsPlatformMouse, WheelAccumulatesAndResetsOnUpdate)
{
    Mouse m;
    m.OnWheel(120);
    m.OnWheel(120);
    EXPECT_EQ(m.WheelDelta(), 240);

    m.Update();
    EXPECT_EQ(m.WheelDelta(), 0);
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
    EXPECT_EQ(m.WheelDelta(), 0);
}

TEST(NsPlatformInput, UpdatePropagatesToMouse)
{
    Input input;
    input.Mouse().OnButtonDown(MouseButton::Left);
    input.Mouse().OnWheel(120);
    EXPECT_TRUE(input.Mouse().IsPressed(MouseButton::Left));
    EXPECT_EQ(input.Mouse().WheelDelta(), 120);

    input.Update();
    EXPECT_FALSE(input.Mouse().IsPressed(MouseButton::Left));
    EXPECT_TRUE(input.Mouse().IsHeld(MouseButton::Left));
    EXPECT_EQ(input.Mouse().WheelDelta(), 0);
}
