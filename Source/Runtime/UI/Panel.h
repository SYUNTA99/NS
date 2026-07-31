#pragma once

#include "Runtime/Core/NonCopyable.h"

#include <string_view>

namespace NS::UI
{

    //! @brief UIパネル（ウィンドウ）の描画状態を安全に管理するクラス。
    //! @details スコープ（寿命）を利用してパネルの描画開始と終了を自動で行い、処理の抜け漏れを防ぐ。
    //! UI機能が無効化されている環境では何も実行されない。
    class Panel : public NS::Core::NonCopyable
    {
    public:
        //! @param title パネルのタイトル
        //! @param isOpen 閉じるボタン（×）と連動する開閉状態フラグへのポインタ。不要な場合はnullptrを指定する
        //! @param windowFlags ImGuiWindowFlags のビット値。既定は0（フラグなし）
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