#pragma once

#include "NSlib/Core/NonCopyable.h"

#include "NSlib/Core/Types.h"

namespace NS
{
    namespace Gfx
    {
        class ScreenOverlay;

        //! @brief 全画面へ黒を重ねる暗転と明転
        //! @details Advance で時間を進め、全 UI の後に Render を呼ぶ
        //! 暗転しきったら BeginIn まで全黒を保持する
        class ScreenFade final : public NonCopyable
        {
        public:
            ScreenFade() noexcept = default;

            //! 暗転を始める。seconds かけて透明から全黒へ。演出中の呼び直しは無視する
            void BeginOut(float seconds) noexcept;

            //! 明転を始める。seconds かけて全黒から透明へ。全黒の保持中以外の呼び出しは無視する
            void BeginIn(float seconds) noexcept;

            //! 演出を捨てて透明へ戻す
            void Cancel() noexcept;

            //! dt 秒だけ進める。透明時と全黒の保持中は何もしない
            void Advance(float dt) noexcept;

            //! 現在の描画先の全体へ黒を重ねる。透明時は何もしない
            void Render(ScreenOverlay& overlay) const noexcept;

            //! 暗転か明転が進行中か
            [[nodiscard]] bool IsFading() const noexcept { return m_stage == Stage::Out || m_stage == Stage::In; }

            //! 暗転しきって全黒を保持中か
            [[nodiscard]] bool IsBlack() const noexcept { return m_stage == Stage::Hold; }

            //! 画面に重ねる黒の不透明度 (0=透明 / 1=全黒)
            [[nodiscard]] float Alpha() const noexcept;

        private:
            // 演出の段階。None は透明、Hold は全黒の保持
            enum class Stage : uint8
            {
                None,
                Out,
                Hold,
                In
            };

            Stage m_stage = Stage::None;
            float m_duration = 0.0f; // 今の段階にかける秒数。BeginOut / BeginIn が決める
            float m_timer = 0.0f;    // 今の段階の経過秒。段階が変わるたび 0 へ戻す
        };
    } // namespace Gfx
} // namespace NS
