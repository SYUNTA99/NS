#pragma once

#include "Runtime/UI/Widget.h"

namespace NS::UI
{

    //! @brief 単色の矩形を描く Widget。下地・板・ゲージ・画面の覆いの素になる
    class ColorRect : public Widget
    {
    public:
        void SetColor(const NS::Math::Color& color) noexcept { m_color = color; }
        [[nodiscard]] const NS::Math::Color& GetColor() const noexcept { return m_color; }

    protected:
        void OnDraw(NS::Graphics::Renderer& renderer, const WidgetRect& rectPx, float alpha) override;

    private:
        NS::Math::Color m_color{1.0f, 1.0f, 1.0f, 1.0f}; // 塗り色。alpha は Widget の実効値と掛かる
    };

} // namespace NS::UI
