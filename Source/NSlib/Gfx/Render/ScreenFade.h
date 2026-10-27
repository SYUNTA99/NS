#pragma once

#include "NSlib/Core/NonCopyable.h"

#include "NSlib/Core/Types.h"

namespace NS
{
    namespace Gfx
    {
        class ScreenOverlay;

        //! 全画面へ黒を重ねるフェード演出。暗転完了後は BeginIn まで全黒を保持する
        class ScreenFade final : public NonCopyable
        {
        public:
            ScreenFade() noexcept = default;

            //! seconds 秒かけて透明から全黒へ暗転を開始する。フェード中の呼び出しは無視する
            void BeginOut(float seconds) noexcept;

            //! seconds 秒かけて全黒から透明へ明転を開始する。全黒保持中以外の呼び出しは無視する
            void BeginIn(float seconds) noexcept;

            //! フェードを中断して即座に透明へ戻す
            void Cancel() noexcept;

            void Advance(float dt) noexcept;

            void Render(ScreenOverlay& overlay) const noexcept;

            [[nodiscard]] bool IsFading() const noexcept { return m_stage == Stage::Out || m_stage == Stage::In; }

            [[nodiscard]] bool IsBlack() const noexcept { return m_stage == Stage::Hold; }

            //! 黒の不透明度 (0.0 = 透明、1.0 = 全黒)
            [[nodiscard]] float Alpha() const noexcept;

        private:
            enum class Stage : uint8
            {
                None,
                Out,
                Hold,
                In
            };

            Stage m_stage = Stage::None;
            float m_duration = 0.0f;
            float m_timer = 0.0f;
        };
    } // namespace Gfx
} // namespace NS
