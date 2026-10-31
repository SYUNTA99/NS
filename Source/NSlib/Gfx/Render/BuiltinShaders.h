#pragma once

#include "NSlib/Gfx/D3DShader.h"

namespace NS
{
    namespace Gfx
    {
        //! 描画パスが非所有で参照する組み込みシェーダー群。所有者は各描画パスより長く生存させること
        struct BuiltinShaders
        {
            D3DShader* fullscreenVs = nullptr;
            D3DShader* fadePs = nullptr;
            D3DShader* screenRectVs = nullptr;
            D3DShader* screenRectPs = nullptr;
            D3DShader* screenLineVs = nullptr;
            D3DShader* screenLinePs = nullptr;
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

        //! シェーダーが描画可能か判定する。ホットリロードに追従するため描画ごとに呼ぶこと
        //! 読み込み失敗時にピンクのフォールバックへ切り替わった場合も true を返す
        [[nodiscard]] inline bool IsUsable(const D3DShader* shader) noexcept
        {
            return shader != nullptr && shader->IsValid();
        }

        //! SV_VertexID で頂点を生成する頂点シェーダーが描画可能か判定する
        //! フォールバック頂点シェーダーは POSITION 入力を要求し、入力レイアウト無しでは描画できないため除外する
        [[nodiscard]] inline bool CanDrawFromVertexId(const D3DShader* vertexShader) noexcept
        {
            return IsUsable(vertexShader) && !vertexShader->IsUsingFallback();
        }
    } // namespace Gfx
} // namespace NS
