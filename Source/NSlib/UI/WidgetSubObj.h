#pragma once

#include "NSlib/Core/NonCopyable.h"
#include "NSlib/UI/UICanvas.h"
#include "NSlib/UI/Widget.h"

namespace NS::Gfx
{
    class Renderer;
} // namespace NS::Gfx

namespace NS::UI
{

    //! @brief 画面 1 つ分の Widget の木を持つ
    //! @details 拡縮と全画面の矩形は持たず、UICanvas で受ける
    //! 描画は世界の描画の後に呼び、その時点の描画先へ重ねる
    class WidgetSubObj : public NS::NonCopyable
    {
    public:
        WidgetSubObj();

        //! 木の根。全画面に広がる透明 Widget で、UI はこの子として組む
        [[nodiscard]] Widget& Root() noexcept { return m_root; }

        //! 全画面の矩形から全 Widget の矩形を確定する
        void Layout(const UICanvas& canvas) noexcept;

        //! @brief 矩形を確定し、現在の描画先へ重ねて描く
        //! @details 中身が無ければ何もしない。renderer の null は呼び手のバグ
        void Draw(NS::Gfx::Renderer* renderer, const UICanvas& canvas);

        //! 描画先ピクセル座標の点を UI が吸うか。直近の Layout の結果で判定する
        [[nodiscard]] bool ConsumesPointer(const UICanvas& canvas, float px, float py) const noexcept;

    private:
        Widget m_root; // 木の根。全画面 stretch の透明 Widget
    };

} // namespace NS::UI
