#pragma once

/// @file CategoryPalette.h
/// @brief 配置ブラシを選ぶ toolbar。 cube / 45 度スロープ / ゴールの 3 スロット
///
/// @details 状態は active slot index のみ
/// Gamepad LB/RB、 Keyboard 数字キーの入力ハンドリングは TickInput、
/// ImGui 描画は Render で行う。 Render は Debug / Development build 時のみ
/// 実体があり、 Shipping では何もしない

#include "Editor/PaletteTemplates.h"
#include "GameCore/Level/LevelData.h"

namespace NS::Platform
{
    class Input;
}
namespace NS::UI
{
    class ImGuiContext;
}

namespace NS::Editor
{
    /// toolbar の状態保持と入力ハンドラ
    class CategoryPalette
    {
    public:
        static constexpr std::size_t kSlotCount = kPaletteSlotCount;

        CategoryPalette() noexcept;
        ~CategoryPalette() noexcept = default;

        CategoryPalette(const CategoryPalette&) = delete;
        CategoryPalette& operator=(const CategoryPalette&) = delete;
        CategoryPalette(CategoryPalette&&) = delete;
        CategoryPalette& operator=(CategoryPalette&&) = delete;

        /// Gamepad LB/RB / Keyboard 1-2 で active slot を切替える
        /// imgui が WantCaptureKeyboard true を返す時は数字キー入力を無視する
        void TickInput(NS::Platform::Input* input, NS::UI::ImGuiContext* imgui) noexcept;

        /// ImGui で toolbar を描画する。 ImGui 非搭載 build では何もしない
        void Render() noexcept;

        [[nodiscard]] std::size_t ActiveSlot() const noexcept { return m_activeSlot; }

        /// 配置に複製する現在のプロトタイプ。 active slot のテンプレート
        [[nodiscard]] const NS::GameCore::Level::ObjectInstance& CurrentTemplate() const noexcept
        {
            return m_current.prototype;
        }

        /// active slot の表示名
        [[nodiscard]] const char* CurrentTemplateName() const noexcept { return m_current.name; }

        /// 現在のブラシが R で 90° 回せる種別か
        [[nodiscard]] bool CurrentIsRotatable() const noexcept { return m_current.rotatable; }

        /// cursor preview 用の slope 角度。 cube ブラシは wedge を持たないので常に -1
        [[nodiscard]] float CurrentSlopeAngleDegrees() const noexcept;

        /// 範囲外指定は無視する
        void SetActiveSlot(std::size_t slot) noexcept;

        /// active slot を再選択した時の variant 切替。 cube には variant が無く何もしない
        void CycleActiveVariant() noexcept;

    private:
        /// active slot から m_current を組み直す。 slot を変えた直後に呼ぶ
        void RefreshCurrentTemplate() noexcept;

        std::size_t m_activeSlot = 0;

        // active slot に対応する配置テンプレート。 表示名 / 回転可否 / 複製元を持つ
        PaletteTemplate m_current{};
    };
} // namespace NS::Editor
