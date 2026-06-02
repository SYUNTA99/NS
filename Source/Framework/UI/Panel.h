#pragma once

/// @file Panel.h
/// @brief NS::UI::Panel — `ImGui::Begin` / `ImGui::End` を RAII で扱う薄い facade
///
/// @details コンストラクタで Begin、 デストラクタで End を呼ぶため例外抜け / 早期 return でも End 漏れしない
/// 内部実装は `detail/imgui_init.cpp` に閉じ、 公開ヘッダから `<imgui.h>` を露出させない
/// `ImGuiContext` が未構築 / fallback mode の場合は no-op となり `IsOpen() == false`

#include <string_view>

namespace NS::UI
{

    /// ImGui ウィンドウ Begin / End ペアを RAII で扱う facade
    /// ```
    /// {
    ///     NS::UI::Panel p("Inspector", &show);
    ///     if (p.IsOpen())
    ///     {
    ///         // ImGui widgets...
    ///     }
    /// }
    /// ```
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

        /// `ImGui::Begin` の戻り値。 collapsed や fallback mode では false
        /// false の場合は widget 書き込みを skip する
        [[nodiscard]] bool IsOpen() const noexcept { return m_isOpen; }

    private:
        bool m_isOpen = false;
        bool m_began = false;
    };

} // namespace NS::UI
