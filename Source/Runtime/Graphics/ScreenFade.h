#pragma once

#include <cstdint>

namespace NS::Graphics
{
    class Renderer;

    //! @brief 世界を写すカメラとは別に、画面の最前面へ黒を重ねる暗転と明転
    //! @details alpha の上下だけを持ち、何のための暗転かは知らない
    //! 暗転しきったら BeginIn まで全黒を保持する。呼び出し側が Advance へ dt を渡し、
    //! IsBlack を見て次へ進める。描画は Scene が world 描画の最後に呼ぶ
    class ScreenFade
    {
    public:
        //! 暗転を始める。seconds かけて透明から全黒へ。演出中の呼び直しは無視する
        void BeginOut(float seconds) noexcept;

        //! 明転を始める。seconds かけて全黒から透明へ。全黒の保持中以外の呼び出しは無視する
        void BeginIn(float seconds) noexcept;

        //! 演出を捨てて透明へ戻す
        void Cancel() noexcept;

        //! dt 秒だけ進める。透明時と全黒の保持中は何もしない
        void Advance(float dt) noexcept;

        //! 暗転か明転が進行中か
        [[nodiscard]] bool IsFading() const noexcept { return m_stage == Stage::Out || m_stage == Stage::In; }

        //! 暗転しきって全黒を保持中か
        [[nodiscard]] bool IsBlack() const noexcept { return m_stage == Stage::Hold; }

        //! 画面に重ねる黒の不透明度 (0=透明 / 1=全黒)
        [[nodiscard]] float Alpha() const noexcept { return m_alpha; }

        //! 現在の不透明度で黒を全画面へ重ねる。全透明フレームは描かない
        void Render(Renderer& renderer) noexcept;

    private:
        // 演出の段階。None は透明、Hold は全黒の保持
        enum class Stage : std::uint8_t
        {
            None,
            Out,
            Hold,
            In
        };

        Stage m_stage = Stage::None;
        float m_duration = 0.0f; // 今の段階にかける秒数。BeginOut / BeginIn が決める
        float m_timer = 0.0f;    // 今の段階の経過秒。段階が変わるたび 0 へ戻す
        float m_alpha = 0.0f;    // 黒の不透明度 (0=透明 / 1=全黒)。Render が読む
    };

} // namespace NS::Graphics
