#include "Editor/CategoryPalette.h"

#include "Editor/PaletteTemplates.h"
#include "Framework/Platform/Input.h"
#include "Framework/UI/ImGuiContext.h"
#include "Framework/UI/Panel.h"

#if NS_EDITOR_ENABLED
#include <imgui.h>
#endif

namespace NS::Editor
{
    CategoryPalette::CategoryPalette() noexcept
    {
        RefreshCurrentTemplate();
    }

    void CategoryPalette::RefreshCurrentTemplate() noexcept
    {
        m_current = PaletteTemplateForKind(m_slots[m_activeSlot]);
    }

    void CategoryPalette::SetActiveSlot(std::size_t slot) noexcept
    {
        if (slot < kSlotCount)
        {
            m_activeSlot = slot;
            RefreshCurrentTemplate();
        }
    }

    std::uint16_t CategoryPalette::SlotBlockId(std::size_t slot) const noexcept
    {
        return slot < kSlotCount ? m_slots[slot] : 0;
    }

    float CategoryPalette::CurrentSlopeAngleDegrees() const noexcept
    {
        const std::uint16_t id = m_slots[m_activeSlot];
        return NS::Game::Blocks::IsSlopeBlock(id) ? NS::Game::Blocks::GetSlopeAngleDegrees(id) : -1.0f;
    }

    void CategoryPalette::CycleActiveVariant() noexcept
    {
        const std::uint16_t id = m_slots[m_activeSlot];
        if (NS::Game::Blocks::IsSlopeBlock(id))
        {
            m_slots[m_activeSlot] = NS::Game::Blocks::NextSlopeBlock(id);
            RefreshCurrentTemplate();
        }
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
            {
                // 既に選択中の slot を再押し → variant 循環 (slope のみ実効)、 別 slot → 選択切替
                if (i == m_activeSlot)
                    CycleActiveVariant();
                else
                    SetActiveSlot(i);
            }
        }
    }

    void CategoryPalette::Render() noexcept
    {
#if NS_EDITOR_ENABLED
        // 画面上部中央に default 配置。 ユーザーは初回ドラッグで移動可能 (ImGuiCond_FirstUseEver)
        if (ImGuiViewport* vp = ImGui::GetMainViewport())
        {
            ImGui::SetNextWindowPos(
                ImVec2(vp->Pos.x + vp->Size.x * 0.5f, vp->Pos.y + 20.0f), ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.0f));
            ImGui::SetNextWindowSize(ImVec2(640.0f, 56.0f), ImGuiCond_FirstUseEver);
        }

        NS::UI::Panel panel("Toolbar");
        if (!panel.IsOpen())
            return;

        for (std::size_t i = 0; i < kSlotCount; ++i)
        {
            if (i > 0)
                ImGui::SameLine();

            ImGui::PushID(static_cast<int>(i));

            const std::uint16_t blockId = m_slots[i];
            const char* label =
                NS::Game::Blocks::IsSlopeBlock(blockId) ? SlopeVariantName(blockId) : PaletteTemplateSlots()[i].name;
            const bool isActive = (i == m_activeSlot);

            // active slot は色を変えて視覚的に区別する
            if (isActive)
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.30f, 0.50f, 0.80f, 1.0f));

            if (ImGui::Button(label, ImVec2(64.0f, 32.0f)))
            {
                // active な slope スロットを再クリック → 角度を循環、 別スロット → 選択切替
                if (isActive)
                    CycleActiveVariant();
                else
                    SetActiveSlot(i);
            }

            if (isActive)
                ImGui::PopStyleColor();

            ImGui::PopID();
        }
#endif
    }
} // namespace NS::Editor
