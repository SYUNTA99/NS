#pragma once

#include "Runtime/App/Layer.h"

#include <cstdint>

//! @brief 単体の遊びの外枠。Editor も ReplayLayer も載らない構成で、プレイ中のカーソルと Esc を持つ
//! @details 起動でカーソルを握り、Esc で出し、出ている状態の Esc でアプリを終える。
//! 積むのは構成を組み立てる GameMain だけ。エディタはプレイ中の Esc を自分で読み、Replay.exe は ReplayLayer
//! か ServeLayer を積む
class StandaloneLayer : public NS::App::Layer
{
public:
    //! @brief プレイ中に Esc を押した時の応答
    enum class EscapeResponse : std::uint8_t
    {
        ReleaseCursor, //!< カーソルを出し、固定と相対を解く
        Quit           //!< アプリを終える
    };

    StandaloneLayer();

    //! @brief カーソルを握り、視点操作をカーソル位置から切り離す
    void OnAttach() override;

    //! @brief Esc を読み、ResolveEscape の応答どおりにカーソルを出すかアプリを終える
    void OnUpdate() override;

    //! @brief Esc の応答を返す
    //! @details 1 回目で隠したカーソルを出し、出ている状態の 2 回目で終える。カーソルの状態がそのまま段階の記録になる
    //! @param[in] cursorVisible 押した時にカーソルが出ていたか
    //! @return Esc への応答
    [[nodiscard]] static EscapeResponse ResolveEscape(bool cursorVisible) noexcept;
};
