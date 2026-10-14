#pragma once

#include "NSlib/App/Layer.h"

#include <cstdint>

//! @brief 単体構成のカーソルと Esc の操作
//! @details 最初の Esc でカーソルを出し、表示中の Esc で終了する
class StandaloneLayer : public NS::Layer
{
public:
    //! @brief プレイ中に Esc を押した時の応答
    enum class EscapeResponse : std::uint8_t
    {
        ReleaseCursor, //!< カーソルを出し、固定と相対を解く
        Quit           //!< アプリを終える
    };

    StandaloneLayer();

    void OnAttach() override;

    void OnUpdate() override;

    //! 非表示ならカーソルを解放し、表示中なら終了する
    [[nodiscard]] static EscapeResponse ResolveEscape(bool cursorVisible) noexcept;
};
