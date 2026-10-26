#include "NSlib/Core/Windows/Input.h"
#include "NSlib/Core/Windows/InputReader.h"
#include "NSlib/Core/Windows/detail/InputWin32.h"

#include <gtest/gtest.h>

// 機器が押した回数を数え、InputReader が前の Read から後の押しを返す事を縛る。
// 読む物ごとに Read を呼ぶ時計が違っても、押しを落とさず、押していない物を押したと読まない

namespace
{
    // 共有の入力の機器に押して離す。試しの終わりに押していない状態へ戻す
    void TapKey(NS::OS::Key key)
    {
        NS::OS::Input& input = NS::OS::Input::Get();
        input.Keyboard().OnKeyDown(key);
        input.Keyboard().OnKeyUp(key);
    }
} // namespace

// 押した瞬間ごとに 1 足し、押したままの OS の繰り返しは数えない
TEST(InputPressCount, CountsEachPressAndIgnoresKeyRepeat)
{
    NS::OS::Keyboard keyboard;
    keyboard.OnKeyDown(NS::OS::Key::J);
    keyboard.OnKeyDown(NS::OS::Key::J);
    EXPECT_EQ(keyboard.PressCount(NS::OS::Key::J), 1u);
    keyboard.OnKeyUp(NS::OS::Key::J);
    keyboard.OnKeyDown(NS::OS::Key::J);
    EXPECT_EQ(keyboard.PressCount(NS::OS::Key::J), 2u);
    // 手放しの消し (窓の焦点が外れた時) は押している間を解くだけで、数えた回数は下ろさない
    keyboard.ClearState();
    EXPECT_EQ(keyboard.PressCount(NS::OS::Key::J), 2u);
    EXPECT_FALSE(keyboard.IsHeld(NS::OS::Key::J));

    NS::OS::Mouse mouse;
    mouse.OnButtonDown(NS::OS::MouseButton::Middle);
    mouse.OnButtonDown(NS::OS::MouseButton::Middle);
    mouse.OnButtonUp(NS::OS::MouseButton::Middle);
    mouse.OnButtonDown(NS::OS::MouseButton::Middle);
    EXPECT_EQ(mouse.PressCount(NS::OS::MouseButton::Middle), 2u);
}

// 前の Read から後に押して離した物を、次の Read が 1 回だけ返す。作る前の押しは返さない
TEST(InputReader, ReturnsPressesSinceThePreviousReadOnce)
{
    TapKey(NS::OS::Key::K);
    NS::OS::InputReader reader;
    EXPECT_FALSE(reader.Read().WasPressed(NS::OS::Key::K));

    TapKey(NS::OS::Key::K);
    EXPECT_TRUE(reader.Read().WasPressed(NS::OS::Key::K));
    EXPECT_FALSE(reader.Read().WasPressed(NS::OS::Key::K));

    NS::OS::Input& input = NS::OS::Input::Get();
    input.Mouse().OnButtonDown(NS::OS::MouseButton::X1);
    input.Mouse().OnButtonUp(NS::OS::MouseButton::X1);
    const NS::OS::InputPresses presses = reader.Read();
    EXPECT_TRUE(presses.WasPressed(NS::OS::MouseButton::X1));
    EXPECT_FALSE(presses.WasPressed(NS::OS::Key::K));
}

// 読む物はそれぞれ自分の InputReader を持つ。片方が読んでも、もう片方の押しは消えない
TEST(InputReader, TwoReadersDoNotTakeEachOthersPresses)
{
    NS::OS::InputReader everyStep;
    NS::OS::InputReader worldStep;

    TapKey(NS::OS::Key::L);
    EXPECT_TRUE(everyStep.Read().WasPressed(NS::OS::Key::L));
    EXPECT_FALSE(everyStep.Read().WasPressed(NS::OS::Key::L));
    EXPECT_TRUE(worldStep.Read().WasPressed(NS::OS::Key::L));
}

// 中立の間は押しを返さない。中立の間に手元の機器へ届いた押しは、明けた後の Read が返す
TEST(InputReader, NeutralReturnsNothingAndKeepsThePressForAfter)
{
    NS::OS::Input& input = NS::OS::Input::Get();
    NS::OS::InputReader reader;
    {
        const NS::OS::ScopedNeutralInput neutral;
        NS::OS::DispatchWin32MessageToInput(input, WM_KEYDOWN, 'M', 0);
        NS::OS::DispatchWin32MessageToInput(input, WM_KEYUP, 'M', 0);
        EXPECT_FALSE(reader.Read().WasPressed(NS::OS::Key::M));
    }
    EXPECT_TRUE(reader.Read().WasPressed(NS::OS::Key::M));
}

// 中立の間だけ読む物 (エディタの下見の自機) は、中立の外で押した物を押したと読まない
TEST(InputReader, ReaderThatOnlyReadsInNeutralNeverSeesPresses)
{
    NS::OS::InputReader preview;
    {
        const NS::OS::ScopedNeutralInput neutral;
        EXPECT_FALSE(preview.Read().WasPressed(NS::OS::Key::N));
    }
    TapKey(NS::OS::Key::N);
    {
        const NS::OS::ScopedNeutralInput neutral;
        EXPECT_FALSE(preview.Read().WasPressed(NS::OS::Key::N));
    }
}

// 1 つの InputReader の押しを、読まずに捨てる
TEST(InputReader, DiscardDropsOnlyThisReadersPresses)
{
    NS::OS::InputReader dropped;
    NS::OS::InputReader kept;
    TapKey(NS::OS::Key::Q);
    dropped.Discard();
    EXPECT_FALSE(dropped.Read().WasPressed(NS::OS::Key::Q));
    EXPECT_TRUE(kept.Read().WasPressed(NS::OS::Key::Q));
}
