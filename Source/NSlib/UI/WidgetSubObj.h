#pragma once

#include "NSlib/Core/NonCopyable.h"
#include "NSlib/UI/UICanvas.h"
#include "NSlib/UI/Widget.h"

namespace NS
{
    namespace Gfx
    {
        class Renderer;
    } // namespace Gfx
} // namespace NS

namespace NS
{
    namespace UI
    {
        //! 画面ごとの Widget の木。拡縮は描画時に渡す UICanvas に従う
        class WidgetSubObj : public NS::NonCopyable
        {
        public:
            WidgetSubObj();

            //! 木の根。全画面に広がる透明 Widget で、UI はこの子として組む
            [[nodiscard]] Widget& Root() noexcept { return m_root; }

            //! 全画面の矩形から全 Widget の矩形を確定する
            void Layout(const UICanvas& canvas) noexcept;

            //! @brief 矩形を確定し、現在の描画先へ重ねて描く
            //! @details 中身が無ければ何もしない。renderer が未設定なら呼び出し側の不備
            void Draw(NS::Gfx::Renderer* renderer, const UICanvas& canvas);

            //! 描画先ピクセル座標の点を UI が吸うか。直近の Layout の結果で判定する
            [[nodiscard]] bool ConsumesPointer(const UICanvas& canvas, float px, float py) const noexcept;

        private:
            Widget m_root; // 木の根。全画面 stretch の透明 Widget
        };
    } // namespace UI
} // namespace NS
