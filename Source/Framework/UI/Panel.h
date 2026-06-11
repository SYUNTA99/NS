#pragma once

/// @file Panel.h
/// @brief NS::UI::Panel — `ImGui::Begin` / `ImGui::End` の対を寿命で管理する薄いラッパ
///
/// @details コンストラクタで Begin、 デストラクタで End を呼ぶため例外抜け / 早期 return でも End 漏れしない
/// 実装は `Panel.cpp` に閉じ、 公開ヘッダから `<imgui.h>` を露出させない
/// `ImGuiContext` が未構築 / fallback mode の場合は何もせず `IsOpen() == false`

#include <string_view>

namespace NS::UI
{

    /// ImGui ウィンドウ Begin / End の対を寿命で管理するラッパ
    class Panel
    {
    public:
        /// `title` はラベル兼 ID、 `isOpen` は X ボタンの open / close 状態を受け取る (nullptr 可)
        Panel(std::string_view title, bool* isOpen = nullptr) noexcept;
        ~Panel() noexcept;

        Panel(const Panel&) = delete;
        Panel& operator=(const Panel&) = delete;
        Panel(Panel&&) = delete;
        Panel& operator=(Panel&&) = delete;

        /// `ImGui::Begin` の戻り値。collapsed / fallback mode では false。false なら widget を skip する
        [[nodiscard]] bool IsOpen() const noexcept { return m_isOpen; }

    private:
        bool m_isOpen = false;
        bool m_began = false;
    };

} // namespace NS::UI
