#pragma once

#include "Runtime/Core/NonCopyable.h"

#include <string_view>

namespace NS::UI
{

    //! @brief UI パネル 1 枚の描画範囲
    //! @details 寿命に合わせて描画の開始と終了を出すので、閉じ忘れが起きない
    //! UI を外したビルドでは何も実行しない
    class Panel : public NS::Core::NonCopyable
    {
    public:
        //! @param[in] title パネルのタイトル
        //! @param[in,out] isOpen 閉じるボタンと連動する開閉状態へのポインタ。閉じるボタンが要らないなら nullptr
        //! @param[in] windowFlags ImGuiWindowFlags のビット値 (既定の 0 はフラグなし)
        Panel(std::string_view title, bool* isOpen = nullptr, int windowFlags = 0) noexcept;
        ~Panel() noexcept;

        //! @brief パネルの中身を描画すべきかどうかを判定する
        //! @note
        //! パネルが折り畳まれている場合や、UI機能が無効な場合はfalseを返す。その場合は中身のUI要素の描画処理をスキップすること
        [[nodiscard]] bool IsOpen() const noexcept { return m_isOpen; }

    private:
        bool m_isOpen = false;
        bool m_began = false;
    };

} // namespace NS::UI