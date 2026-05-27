#include "Game/Editor/CategoryPalette.h"

#include "Framework/Platform/Input.h"
#include "Framework/UI/ImGuiContext.h"

namespace NS::Game::Editor
{
    void CategoryPalette::SetActiveSlot(std::size_t slot) noexcept
    {
        if (slot < kSlotCount)
            m_activeSlot = slot;
    }

    std::uint16_t CategoryPalette::SlotBlockId(std::size_t slot) const noexcept
    {
        return slot < kSlotCount ? m_slots[slot] : 0;
    }

    void CategoryPalette::TickInput(NS::Platform::Input* input, NS::UI::ImGuiContext* imgui) noexcept
    {
        if (input == nullptr)
            return;

        auto& gp = input->Gamepad(0);
        if (gp.IsConnected())
        {
            if (gp.IsPressed(NS::Platform::GamepadButton::LeftShoulder))
                SetActiveSlot((m_activeSlot + kSlotCount - 1) % kSlotCount);
            if (gp.IsPressed(NS::Platform::GamepadButton::RightShoulder))
                SetActiveSlot((m_activeSlot + 1) % kSlotCount);
        }

        // ImGui テキスト入力中は数字キーを取り合わない
        const bool wantKeyboard = imgui != nullptr && imgui->WantCaptureKeyboard();
        if (wantKeyboard)
            return;

        auto& kb = input->Keyboard();
        for (std::size_t i = 0; i < kSlotCount; ++i)
        {
            const auto code =
                static_cast<NS::Platform::Key>(static_cast<int>(NS::Platform::Key::Num1) + static_cast<int>(i));
            if (kb.IsPressed(code))
                SetActiveSlot(i);
        }
    }

    void CategoryPalette::Render() noexcept
    {
        // 9 スロット toolbar の ImGui 描画は後続タスクで実装する。
        // 公開ヘッダから <imgui.h> を露出させない方針のため、
        // 実装は detail/ または gated cpp で書き、 fallback ビルドでは no-op。
    }
} // namespace NS::Game::Editor
