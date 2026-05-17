#include <gtest/gtest.h>

#include <ns/platform/detail/input_win32.h>
#include <ns/platform/input.h>
#include <ns/platform/keyboard.h>

#include <windows.h>

namespace
{
    using ns::platform::Input;
    using ns::platform::Key;
    using ns::platform::Keyboard;
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
