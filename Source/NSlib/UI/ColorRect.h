#pragma once

#include "NSlib/UI/Widget.h"

namespace NS
{
    namespace UI
    {
        //! @brief 単色の矩形を描く Widget。下地・板・ゲージ・画面の覆いの素になる
        class ColorRect : public Widget
        {
        public:
            void SetColor(const Color& color) noexcept { m_color = color; }
            [[nodiscard]] const Color& GetColor() const noexcept { return m_color; }

        protected:
            void OnDraw(Gfx::Renderer& renderer, const WidgetRect& rectPx, float alpha) override;

        private:
            Color m_color{1.0f, 1.0f, 1.0f, 1.0f}; // 塗り色。alpha は Widget の実効値と掛かる
        };
    } // namespace UI
} // namespace NS
