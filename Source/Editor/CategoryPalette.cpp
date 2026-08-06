#include "Editor/CategoryPalette.h"

#include "Editor/PaletteTemplates.h"
#include "Runtime/Object/Reflection/ComponentEntry.h"
#include "Runtime/Platform/Gamepad.h"
#include "Runtime/Platform/Input.h"
#include "Runtime/Platform/Keyboard.h"
#include "Runtime/UI/ImGuiContext.h"
#include "Runtime/UI/Panel.h"

#include <algorithm>

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
        if (slot < k_SlotCount)
        {
            m_activeSlot = slot;
            RefreshCurrentTemplate();
        }
    }

    float CategoryPalette::CurrentSlopeAngleDegrees() const noexcept
    {
        // コンポーネントからスロープ角度を読み取る。角度を持たない場合は負値を返す
        const nlohmann::json* slope = NS::Object::FindComponentEntry(m_current.prototype, "SlopeColliderComponent");
        if (slope == nullptr)
        {
            return -1.0f;
        }
        return NS::Object::FieldFloat(*slope, "角度 (度)", -1.0f);
    }

    void CategoryPalette::CycleActiveVariant() noexcept
    {
        // ※現在の仕様ではバリエーションの切り替えは行わない
    }

    void CategoryPalette::TickInput(NS::Platform::Input* input, NS::UI::ImGuiContext* imgui) noexcept
    {
        if (input == nullptr)
        {
            return;
        }

        auto& gp = input->Gamepad(0);
        if (gp.IsConnected())
        {
            if (gp.IsPressed(NS::Platform::GamepadButton::LeftShoulder))
            {
                SetActiveSlot((m_activeSlot + k_SlotCount - 1) % k_SlotCount);
            }

            if (gp.IsPressed(NS::Platform::GamepadButton::RightShoulder))
            {
                SetActiveSlot((m_activeSlot + 1) % k_SlotCount);
            }
        }

        // UIがキーボード入力中の場合は、ショートカット操作を無視する
        const bool wantKeyboard = imgui != nullptr && imgui->WantCaptureKeyboard();
        if (wantKeyboard)
        {
            return;
        }

        auto& kb = input->Keyboard();
        for (std::size_t i = 0; i < k_SlotCount; ++i)
        {
            const auto code =
                static_cast<NS::Platform::Key>(static_cast<int>(NS::Platform::Key::Num1) + static_cast<int>(i));
            if (kb.IsPressed(code))
            {
                SetActiveSlot(i);
            }
        }
    }

    void CategoryPalette::Render(const NS::Editor::ViewRect& viewRect) noexcept
    {
#if NS_EDITOR_ENABLED
        constexpr float k_DesiredWidth = 640.0f;
        constexpr float k_Height = 56.0f;
        constexpr float k_TopMargin = 20.0f;
        const float width = std::min(k_DesiredWidth, static_cast<float>(viewRect.width));

        // 初回だけ Scene ビュー上端中央へ置く。以降はドラッグで動かすが位置は自前で持ち、毎フレーム
        // Scene ビュー内へクランプする。窓の枠は Begin 時点の位置で描かれるため、Begin 前に
        // クランプ済みの位置を SetNextWindowPos(Always) で渡し、枠ごと内側へ収める
        if (!m_toolbarPlaced)
        {
            m_toolbarX = static_cast<float>(viewRect.x) + (static_cast<float>(viewRect.width) - width) * 0.5f;
            m_toolbarY = static_cast<float>(viewRect.y) + k_TopMargin;
            m_toolbarPlaced = true;
        }
        const float minX = static_cast<float>(viewRect.x);
        const float minY = static_cast<float>(viewRect.y);
        const float maxX = std::max(minX, static_cast<float>(viewRect.x + viewRect.width) - width);
        const float maxY = std::max(minY, static_cast<float>(viewRect.y + viewRect.height) - k_Height);
        m_toolbarX = std::clamp(m_toolbarX, minX, maxX);
        m_toolbarY = std::clamp(m_toolbarY, minY, maxY);

        ImGui::SetNextWindowPos(ImVec2(m_toolbarX, m_toolbarY), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(width, k_Height), ImGuiCond_Always);

        // NoMove。移動は下の余白ドラッグで自前に行う。ドックへ吸われると枠が外へ出るため NoDocking
        NS::UI::Panel panel("Toolbar",
                            nullptr,
                            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoScrollbar |
                                ImGuiWindowFlags_NoScrollWithMouse);
        if (!panel.IsOpen())
        {
            return;
        }

        // ボタンの無い余白を掴んでいる間だけ自前でドラッグ移動する。移動量は次フレームの位置へ反映され、
        // 常にクランプ済みなので枠が Scene ビューの外へ出ることはない
        if (!m_toolbarDragging && ImGui::IsWindowHovered() && !ImGui::IsAnyItemHovered() &&
            ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        {
            m_toolbarDragging = true;
        }
        if (m_toolbarDragging)
        {
            if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
            {
                const ImVec2 delta = ImGui::GetIO().MouseDelta;
                m_toolbarX = std::clamp(m_toolbarX + delta.x, minX, maxX);
                m_toolbarY = std::clamp(m_toolbarY + delta.y, minY, maxY);
            }
            else
            {
                m_toolbarDragging = false;
            }
        }

        // 各ブラシのスロットボタンを横並びで描画する
        for (std::size_t i = 0; i < k_SlotCount; ++i)
        {
            if (i > 0)
            {
                ImGui::SameLine();
            }

            ImGui::PushID(static_cast<int>(i));

            const char* label = PaletteTemplateSlots()[i].name;
            const bool isActive = (i == m_activeSlot);

            // アクティブなスロットは色を変えてハイライトする
            if (isActive)
            {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.80f, 0.50f, 0.30f, 1.0f));
            }

            if (ImGui::Button(label, ImVec2(64.0f, 32.0f)))
            {
                SetActiveSlot(i);
            }

            if (isActive)
            {
                ImGui::PopStyleColor();
            }

            ImGui::PopID();
        }
#endif
    }
} // namespace NS::Editor