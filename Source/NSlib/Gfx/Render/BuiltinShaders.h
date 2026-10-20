#pragma once

#include "NSlib/Gfx/D3DShader.h"

namespace NS
{
    namespace Gfx
    {
        //! @brief 描く段が使う組み込みのシェーダー。持ち主は渡す側で、描く段は指すだけ
        //! @details 渡す側は描く段より長く持つ。読み直しは同じ D3DShader のまま中身を替えるので、
        //! 描く段は指したまま新しい中身で描く。使えるかは描く度に IsUsable で決める
        struct BuiltinShaders
        {
            D3DShader* fullscreenVs = nullptr; //!< ScreenOverlay の全画面塗り・Bloom・ScreenPasses が共有する
            D3DShader* fadePs = nullptr;
            D3DShader* screenRectVs = nullptr;
            D3DShader* screenRectPs = nullptr;
            D3DShader* skyboxVs = nullptr;
            D3DShader* skyboxPs = nullptr;
            D3DShader* debugLineVs = nullptr;
            D3DShader* debugLinePs = nullptr;
            D3DShader* bloomThresholdPs = nullptr;
            D3DShader* bloomDownPs = nullptr;
            D3DShader* bloomUpPs = nullptr;
            D3DShader* bloomCompositePs = nullptr;
            D3DShader* screenDistortionPs = nullptr;
            D3DShader* bodyMaskPs = nullptr;
        };

        //! @brief 描く段がこのシェーダーで描けるか。どの段も描く度にこれで決める
        //! @details 読めずにピンクの代わりへ落ちた物も使えると答え、書き間違いを画面のピンクで見せる。
        //! 読めなかった時の ERROR は D3DShader が出す。作る時に決めた答えを持つと、読み直しが届かない
        //! @return 指していて中身がある場合 true、それ以外の場合は false
        [[nodiscard]] inline bool IsUsable(const D3DShader* shader) noexcept
        {
            return shader != nullptr && shader->IsValid();
        }
    } // namespace Gfx
} // namespace NS
