#pragma once

#include <array>
#include <cstddef>

namespace NS::Platform
{

    //! @brief マウスボタンの識別子。
    enum class MouseButton : int
    {
        Left = 0,
        Right,
        Middle,
        X1,
        X2,

        Count
    };

    //! @brief マウス入力の現在および前フレームの状態を追跡・管理するクラス
    //! @details 座標系はクライアント領域の左上を原点とする
    //! @note スレッドセーフではないため、単一スレッドからのアクセスを前提とする
    class Mouse
    {
    public:
        Mouse() = default;

        //! @brief 対象のボタンが現在のフレームで新たに押されたかどうかを判定する
        [[nodiscard]] bool IsPressed(MouseButton b) const noexcept;

        //! @brief 対象のボタンが現在押され続けているかどうかを判定する
        [[nodiscard]] bool IsHeld(MouseButton b) const noexcept;

        //! @brief 対象のボタンが現在のフレームで離されたかどうかを判定する
        [[nodiscard]] bool IsReleased(MouseButton b) const noexcept;

        //! @brief クライアント領域のX座標を取得する
        [[nodiscard]] int GetX() const noexcept { return m_x; }

        //! @brief クライアント領域のY座標を取得する
        [[nodiscard]] int GetY() const noexcept { return m_y; }

        //! @brief 前フレームからのX軸の移動差分を取得する
        //! @note 相対マウスモードが有効な場合は、カーソルの画面座標差分ではなく、デバイスからの物理的な移動量（Raw
        //! Input）を返す
        [[nodiscard]] int GetDeltaX() const noexcept
        {
            if (m_relativeMode)
            {
                return m_rawDeltaX;
            }
            return m_x - m_prevX;
        }

        //! @brief 前フレームからのY軸の移動差分を取得する
        //! @note 相対マウスモードが有効な場合は、カーソルの画面座標差分ではなく、デバイスからの物理的な移動量（Raw
        //! Input）を返す
        [[nodiscard]] int GetDeltaY() const noexcept
        {
            if (m_relativeMode)
            {
                return m_rawDeltaY;
            }
            return m_y - m_prevY;
        }

        //! @brief 縦方向のホイール移動量を取得する（1ステップにつき WHEEL_DELTA = 120 単位）
        [[nodiscard]] int GetWheelDelta() const noexcept { return m_wheel; }

        //! @brief 入力状態のフレーム境界を更新し、差分判定の基準を進める
        void Update() noexcept;

        //! @brief OSのマウス移動イベントを受け取る内部API
        void OnMove(int x, int y) noexcept;

        //! @brief OSのマウスボタン押下イベントを受け取る内部API
        void OnButtonDown(MouseButton b) noexcept;

        //! @brief OSのマウスボタン解放イベントを受け取る内部API
        void OnButtonUp(MouseButton b) noexcept;

        //! @brief OSのホイールスクロールイベントを受け取る内部API
        void OnWheel(int delta) noexcept;

        //! @brief 相対マウスモードの有効/無効を切り替える。
        //! @note プレイ中の視点操作など、マウスポインタが画面端に到達しても操作を継続させたい場合に有効化する
        void SetRelativeMode(bool enabled) noexcept;

        [[nodiscard]] bool IsRelativeMode() const noexcept { return m_relativeMode; }

        //! @brief 相対マウスモード用に、OSからの生のマウス移動量（Raw Input）を蓄積する内部API
        void OnRawMove(int dx, int dy) noexcept;

        //! @brief すべてのマウス入力状態（ボタンおよびホイール）を強制的にリセットする
        //! @note
        //! ウィンドウのフォーカス消失時など、ボタンが押されたまま内部で固着するバグを防ぐために利用する。座標は維持される
        void ClearState() noexcept;

    private:
        static constexpr std::size_t k_ButtonCount = static_cast<std::size_t>(MouseButton::Count);

        std::array<bool, k_ButtonCount> m_current{};
        std::array<bool, k_ButtonCount> m_previous{};

        int m_x = 0;
        int m_y = 0;
        int m_prevX = 0;
        int m_prevY = 0;
        int m_wheel = 0;
        int m_rawDeltaX = 0;
        int m_rawDeltaY = 0;
        bool m_relativeMode = false;
    };

} // namespace NS::Platform