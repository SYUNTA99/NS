#pragma once

#include "NSlib/App/Layer.h"

#include "NSlib/Core/Types.h"
#include "NSlib/Core/Windows/InputReader.h"

/// 単体構成のカーソルと Esc の操作
/// 最初の Esc でカーソルを出し、表示中の Esc で終了する
class StandaloneLayer : public NS::Layer
{
public:
    enum class EscapeResponse : NS::uint8
    {
        ReleaseCursor,
        Quit
    };

    StandaloneLayer();

    void OnAttach() override;

    void OnUpdate() override;

    /// 非表示ならカーソルを解放し、表示中なら終了する
    [[nodiscard]] static EscapeResponse ResolveEscape(bool cursorVisible) noexcept;

private:
    NS::OS::InputReader m_inputReader;
};
