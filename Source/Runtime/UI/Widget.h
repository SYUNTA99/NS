#pragma once

#include "Runtime/Math/Math.h"

#include <memory>
#include <utility>
#include <vector>

namespace NS::Graphics
{
    class Renderer;
} // namespace NS::Graphics

namespace NS::UI
{

    //! @brief 画面上の矩形範囲。単位は基準解像度のピクセルで左上原点
    struct WidgetRect
    {
        float x = 0.0f;
        float y = 0.0f;
        float width = 0.0f;
        float height = 0.0f;

        [[nodiscard]] bool Contains(float px, float py) const noexcept
        {
            return px >= x && px < x + width && py >= y && py < y + height;
        }
    };

    //! @brief 画面 UI の最小単位。木を成し、親の矩形への貼り付け方と重ね順 (子の並び) を持つ
    //! @details 座標は基準解像度 (高さ 1080) 単位で書き、実ピクセルへの一様拡縮は UISystem が決める
    //! anchor は親矩形内の基準点 (0..1)、pivot は自分の矩形のどこを anchor に合わせるか
    //! alpha は子へ掛け算で伝わり、木ごとまとめて薄くできる
    class Widget
    {
    public:
        Widget() = default;
        virtual ~Widget() = default;

        Widget(const Widget&) = delete;
        Widget& operator=(const Widget&) = delete;
        Widget(Widget&&) = delete;
        Widget& operator=(Widget&&) = delete;

        //! 型 T の子を自分の中で作って末尾へ加える。後に加えた子ほど上に描かれる
        //! 所有は自分が握り、呼出側へは生ポインタだけ返す
        template <class T, class... Args> T* AddChild(Args&&... args)
        {
            auto child = std::make_unique<T>(std::forward<Args>(args)...);
            T* raw = child.get();
            Adopt(std::move(child));
            return raw;
        }

        //! 子を 1 つでも抱えているか。何も無い木の描画を丸ごと飛ばす判定に使う
        [[nodiscard]] bool HasChildren() const noexcept { return !m_children.empty(); }

        //! 親矩形内の基準点 (0..1)。(0,0)=左上、(1,1)=右下
        void SetAnchor(const NS::Math::Vector2& anchor) noexcept { m_anchor = anchor; }
        //! 自分の矩形のどこを anchor に合わせるか (0..1)
        void SetPivot(const NS::Math::Vector2& pivot) noexcept { m_pivot = pivot; }
        //! anchor からのずらし量 (基準解像度ピクセル)
        void SetOffset(const NS::Math::Vector2& offset) noexcept { m_offset = offset; }
        //! 自分の矩形の大きさ (基準解像度ピクセル)
        void SetSize(const NS::Math::Vector2& size) noexcept { m_size = size; }
        //! true なら親矩形いっぱいに広がり、anchor / offset / size を無視する。全画面の覆いに使う
        void SetStretch(bool stretch) noexcept { m_stretch = stretch; }
        //! false の間は自分ごと子も描かず、当たり判定からも消える
        void SetVisible(bool visible) noexcept { m_visible = visible; }
        [[nodiscard]] bool IsVisible() const noexcept { return m_visible; }
        //! 自分以下へ掛かる不透明度 (0=透明 / 1=不変)。親の値と掛け算で効く
        void SetAlpha(float alpha) noexcept { m_alpha = alpha; }
        [[nodiscard]] float Alpha() const noexcept { return m_alpha; }
        //! true なら自分の矩形がポインタ入力を吸い、下のゲーム操作へ通さない
        void SetBlocksInput(bool blocks) noexcept { m_blocksInput = blocks; }
        [[nodiscard]] bool BlocksInput() const noexcept { return m_blocksInput; }

        //! 親矩形に対する自分の矩形を求める
        [[nodiscard]] WidgetRect ResolveRect(const WidgetRect& parent) const noexcept;

        //! 直近の Layout で確定した矩形 (基準解像度単位)。当たり判定とテストが読む
        [[nodiscard]] const WidgetRect& LayoutRect() const noexcept { return m_layoutRect; }

        //! 親矩形から自分と子孫の矩形を確定する。UISystem が根から呼ぶ
        void Layout(const WidgetRect& parent) noexcept;

        //! 自分と子孫を描く。scale は基準解像度 → 実ピクセルの拡縮率
        void Draw(NS::Graphics::Renderer& renderer, float parentAlpha, float scale);

        //! 点 (基準解像度単位) を自分以下の誰かが吸うか。後に描かれる子から先に見る
        [[nodiscard]] bool HitTest(float px, float py) const noexcept;

    protected:
        //! 自分だけを描く。rectPx は実ピクセルへ拡縮済みの矩形、alpha は親から掛かった実効値
        virtual void OnDraw(NS::Graphics::Renderer&, const WidgetRect& /*rectPx*/, float /*alpha*/) {}

    private:
        //! 組み上がった子を受け取って所有する。AddChild<T> だけが通る
        void Adopt(std::unique_ptr<Widget> child);

        NS::Math::Vector2 m_anchor{0.0f, 0.0f}; // 親矩形内の基準点 (0..1)
        NS::Math::Vector2 m_pivot{0.0f, 0.0f};  // 自分の矩形の合わせ点 (0..1)
        NS::Math::Vector2 m_offset{0.0f, 0.0f}; // anchor からのずらし (基準解像度ピクセル)
        NS::Math::Vector2 m_size{0.0f, 0.0f};   // 大きさ (基準解像度ピクセル)
        bool m_stretch = false;                 // 親いっぱいに広がるか
        bool m_visible = true;
        float m_alpha = 1.0f;
        bool m_blocksInput = false;

        WidgetRect m_layoutRect{}; // 直近の Layout で確定した矩形
        std::vector<std::unique_ptr<Widget>> m_children;
    };

} // namespace NS::UI
