#pragma once

#include "Runtime/Core/NonCopyable.h"
#include "Runtime/UI/Widget.h"

namespace NS::Graphics
{
    class Renderer;
} // namespace NS::Graphics

namespace NS::UI
{

    //! @brief ゲーム画面へ重ねる UI の入口。木の根・基準解像度の拡縮・描画・入力の吸い込み判定を持つ
    //! @details 描画は world 描画の後に呼び、その時点で bind されている描画先へ重ねる
    //! 座標は高さ 1080 の基準解像度で書き、実ピクセルへは描画先の高さ比で一様に拡縮する
    //! 横幅は比率に応じて伸び縮みし、端に貼る物は anchor で追従させる
    class UISystem : public NS::Core::NonCopyable
    {
    public:
        static constexpr float k_ReferenceHeight = 1080.0f; //!< 基準解像度の高さ。UI 座標はこの単位で書く

        UISystem();

        //! 木の根。全画面に広がる透明 Widget で、UI はこの子として組む
        [[nodiscard]] Widget& Root() noexcept { return m_root; }

        //! 描画先サイズから拡縮を決め、全 Widget の矩形を確定する。Render が呼ぶが、テストは単独で呼べる
        void Layout(float viewportWidth, float viewportHeight) noexcept;

        //! 現在の描画先へ UI を重ねて描く。中身が無ければ何もしない
        void Render(NS::Graphics::Renderer& renderer);

        //! 描画先ピクセル座標の点を UI が吸うか。直近 Layout の結果で判定する
        [[nodiscard]] bool ConsumesPointer(float px, float py) const noexcept;

        //! 基準解像度 → 実ピクセルの拡縮率。直近の Layout で確定した値
        [[nodiscard]] float Scale() const noexcept { return m_scale; }

        //! 基準解像度単位の全画面矩形。直近の Layout で確定した値
        [[nodiscard]] const WidgetRect& CanvasRect() const noexcept { return m_canvas; }

    private:
        Widget m_root;         // 木の根。全画面 stretch の透明 Widget
        float m_scale = 1.0f;  // 基準解像度 → 実ピクセル
        WidgetRect m_canvas{}; // 基準解像度単位の全画面矩形
    };

} // namespace NS::UI
