#pragma once

#include "Editor/GridMath.h"
#include "Editor/PaletteTemplates.h"
#include "NSlib/Core/NonCopyable.h"
#include "NSlib/CoreObject/ObjectJson.h"

namespace NS
{
    namespace OS
    {
        class Input;
    }
} // namespace NS
namespace NS
{
    namespace UI
    {
        class ImGuiContext;
    }
} // namespace NS

namespace NS
{
    namespace Editor
    {
        //! 配置ブラシを数字キーとツールバーで選ぶ
        class CategoryPalette : public NonCopyable
        {
        public:
            //! 数字キーで選ぶ。UI がキーボードを使っている間は無視する
            void TickInput(OS::Input* input, UI::ImGuiContext* imgui) noexcept;

            //! Scene ビュー内に収まるツールバーを描く。viewRect はスクリーン座標
            void Render(const Editor::ViewRect& viewRect) noexcept;

            [[nodiscard]] std::size_t ActiveSlot() const noexcept { return m_activeSlot; }

            [[nodiscard]] const nlohmann::json& CurrentTemplate() const noexcept
            {
                return PaletteTemplateSlots()[m_activeSlot].prototype;
            }

            [[nodiscard]] const char* CurrentTemplateName() const noexcept
            {
                return PaletteTemplateSlots()[m_activeSlot].name;
            }

            [[nodiscard]] bool CurrentIsRotatable() const noexcept
            {
                return PaletteTemplateSlots()[m_activeSlot].rotatable;
            }

            //! @brief 配置プレビュー用のスロープ角度を返す
            //! @note キューブのように角度を持たない形状では負の値が返る
            [[nodiscard]] float CurrentSlopeAngleDegrees() const noexcept;

            void SetActiveSlot(std::size_t slot) noexcept;

        private:
            std::size_t m_activeSlot = 0;

            float m_toolbarX = 0.0f;        // ツールバー窓の左上X。Scene ビュー内で自前ドラッグする管理値
            float m_toolbarY = 0.0f;        // 同Y
            bool m_toolbarPlaced = false;   // 初回に Scene 上端中央へ置いたか
            bool m_toolbarDragging = false; // 余白ドラッグで移動中か
        };
    } // namespace Editor
} // namespace NS
