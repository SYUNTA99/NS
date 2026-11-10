#pragma once

#include "NSlib/Core/NonCopyable.h"

namespace NS
{
    class SceneActor;
    class SceneRenderer;

    //! シーンに 1 つの、画面全体の白い光。始めた歩は始めの濃さで描き、次の更新から直線で薄れる
    //! 動作の事象と当たりの反応の項目が、持ち主を頼んだ物として始める
    //! 濃さが変わるたびに SceneRenderer::SetScreenFlash へ書く。描くのは SceneRenderer
    class ScreenFlashDirector final : public NonCopyable
    {
    public:
        //! @param[in] renderer 濃さを書く先。この物より長く生きる
        explicit ScreenFlashDirector(SceneRenderer& renderer) noexcept;

        //! 白の光を始める。前の白が残っていても始め直す
        //! @param[in] requester 頼んだ物。Stop で同じ物を渡すと消える
        //! @param[in] frames 白の光のフレーム数。0 以下なら光らない
        //! @param[in] alpha 始めの濃さ。残りのフレーム数に比例して薄くなる
        void Start(const SceneActor* requester, int frames, float alpha) noexcept;

        //! requester が頼んだ白を消す。ほかの物が頼んだ白は残す
        void Stop(const SceneActor* requester) noexcept;

        //! requester が頼んで出している白の濃さに scale を掛ける。始め直さず、残りをその濃さで続ける
        void Scale(const SceneActor* requester, float scale) noexcept;

        //! 頼んだ物に関わらず白を消す。シーンがアクターを組み直す前に呼ぶ
        void Clear() noexcept;

        //! 白の残りフレーム数。出していない場合 0
        [[nodiscard]] int FramesRemaining() const noexcept { return m_remaining; }
        //! 今の白の濃さ。始めの濃さ × 残りのフレーム数 ÷ 始めのフレーム数
        [[nodiscard]] float Alpha() const noexcept;
        //! 白を 1 フレーム薄める
        void OnTick() noexcept;

    private:
        //! 今の濃さを描く仕組みへ書く。状態を変える関数の最後に呼ぶ
        void WriteAlpha() noexcept;

        SceneRenderer& m_renderer;
        // 頼んだ物は見分けるだけで、指す先は読まない
        const SceneActor* m_requester = nullptr;
        int m_remaining = 0;
        int m_frames = 0; // 薄める割合の分母
        float m_alpha = 0.0f;
        bool m_justStarted = false;
    };
} // namespace NS
