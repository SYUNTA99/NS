#include "Editor/CategoryPalette.h"

#include "Editor/PaletteTemplates.h"
#include "Framework/UI/ImGuiContext.h"
#include "Framework/UI/Panel.h"

#include <variant>

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
        m_current = PaletteTemplateSlots()[m_activeSlot];
    }

    void CategoryPalette::SetActiveSlot(std::size_t slot) noexcept
    {
        if (slot < kSlotCount)
        {
            m_activeSlot = slot;
            RefreshCurrentTemplate();
        }
    }

    float CategoryPalette::CurrentSlopeAngleDegrees() const noexcept
    {
        // slope ブラシは prototype の SlopeCollider から角度を読み、 cursor preview の wedge と一致させる
        // slope を持たない cube / goal ブラシは wedge preview を持たないので負値を返す
        const NS::GameCore::Level::ComponentData* slope =
            NS::GameCore::Level::FindComponentData(m_current.prototype, "SlopeColliderComponent");
        if (slope == nullptr)
            return -1.0f;
        const NS::GameCore::Level::FieldValue* angle = NS::GameCore::Level::FindField(*slope, "Angle (deg)");
        if (angle != nullptr && std::holds_alternative<float>(angle->value))
            return std::get<float>(angle->value);
        return -1.0f;
    }

    void CategoryPalette::CycleActiveVariant() noexcept
    {
        // cube には variant が無いので再選択しても何もしない
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
#if NS_EDITOR_ENABLED
        // 画面上部中央に default 配置。 ユーザーは初回ドラッグで移動できる
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

            const char* label = PaletteTemplateSlots()[i].name;
            const bool isActive = (i == m_activeSlot);

            if (isActive)
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.30f, 0.50f, 0.80f, 1.0f));

            if (ImGui::Button(label, ImVec2(64.0f, 32.0f)))
                SetActiveSlot(i);

            if (isActive)
                ImGui::PopStyleColor();

            ImGui::PopID();
        }
#endif
    }
} // namespace NS::Editor
