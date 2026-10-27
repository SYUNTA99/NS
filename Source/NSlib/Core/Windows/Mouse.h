#pragma once

#include "NSlib/Core/Types.h"

#include <array>
#include <cstddef>

namespace NS::OS
{
        //! @brief マウスボタンの識別子
        enum class MouseButton : int
        {
            Left = 0,
            Right,
            Middle,
            X1,
            X2,

            Count
        };

        //! 単一スレッドで読むマウス状態。座標はクライアント領域の左上原点
        class Mouse
        {
        public:
            [[nodiscard]] bool IsHeld(MouseButton b) const noexcept;

            //! 押した瞬間ごとに 1 足した回数。下ろさない。押した瞬間は InputReader で読む
            [[nodiscard]] uint32 PressCount(MouseButton b) const noexcept;

            [[nodiscard]] int GetX() const noexcept { return m_x; }

            [[nodiscard]] int GetY() const noexcept { return m_y; }

            //! 相対モードでは画面座標差ではなく Raw Input の移動量を返す
            [[nodiscard]] int GetDeltaX() const noexcept
            {
                if (m_relativeMode)
                {
                    return m_rawDeltaX;
                }
                return m_x - m_prevX;
            }

            //! 相対モードでは画面座標差ではなく Raw Input の移動量を返す
            [[nodiscard]] int GetDeltaY() const noexcept
            {
                if (m_relativeMode)
                {
                    return m_rawDeltaY;
                }
                return m_y - m_prevY;
            }

            //! @brief 縦方向のホイール移動量を取得する (1 目盛りで WHEEL_DELTA = 120)
            [[nodiscard]] int GetWheelDelta() const noexcept { return m_wheel; }

            //! @brief 移動量とホイールのフレーム境界を更新する
            void Update() noexcept;

            //! @brief OSのマウス移動イベントを受け取る内部API
            void OnMove(int x, int y) noexcept;

            //! @brief OSのマウスボタン押下イベントを受け取る内部API。押していないボタンだけを数える
            void OnButtonDown(MouseButton b) noexcept;

            //! @brief OSのマウスボタン解放イベントを受け取る内部API
            void OnButtonUp(MouseButton b) noexcept;

            //! @brief OSのホイールスクロールイベントを受け取る内部API
            void OnWheel(int delta) noexcept;

            //! 画面端に達しても移動量を受け取る。切替時は未消化の移動量を捨てる
            void SetRelativeMode(bool enabled) noexcept;

            [[nodiscard]] bool IsRelativeMode() const noexcept { return m_relativeMode; }

            //! @brief 相対マウスモード用に、OS からの Raw Input の移動量を蓄積する内部 API
            void OnRawMove(int dx, int dy) noexcept;

            //! @brief 相対マウスモード用に、OS からの Raw Input の絶対座標を前回との差にして蓄積する内部 API
            //! @note 最初の 1 回は座標を記録するだけで移動量は積まない
            void OnRawMoveAbsolute(int screenX, int screenY) noexcept;

            //! フォーカス消失時の押しっぱなしを防ぐため入力を解除する。座標と押した回数は保つ
            void ClearState() noexcept;

        private:
            static constexpr std::size_t k_ButtonCount = static_cast<std::size_t>(MouseButton::Count);

            std::array<bool, k_ButtonCount> m_current{};
            std::array<uint32, k_ButtonCount> m_presses{};

            int m_x = 0;
            int m_y = 0;
            int m_prevX = 0;
            int m_prevY = 0;
            int m_wheel = 0;
            int m_rawDeltaX = 0;
            int m_rawDeltaY = 0;
            int m_absX = 0;
            int m_absY = 0;
            bool m_absOriginSet = false;
            bool m_relativeMode = false;
        };
} // namespace NS::OS
