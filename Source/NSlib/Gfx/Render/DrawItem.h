#pragma once

#include "NSlib/Core/Types.h"
#include "NSlib/Gfx/Render/FrameConstants.h"
#include "NSlib/Gfx/D3DPipeline.h"
#include "NSlib/Gfx/D3DShader.h"

#include <cstddef>

namespace NS
{
    namespace Gfx
    {
        class D3DMesh;
        class D3DMaterial;
        class D3DBuffer;
        class D3DDevice;
        class D3DShader;

        // FrameCB の 16 バイト整列を値で抱えるため DrawItem に想定どおりのパディングが入る。C4324 を黙らせる
#pragma warning(push)
#pragma warning(disable : 4324)
        struct DrawItem
        {
            D3DMesh* mesh = nullptr;
            D3DMaterial* material = nullptr;
            BlendMode blend = BlendMode::Opaque;
            bool occludedOnly = false; // true のとき手前の物に隠れた画素のみを半透明・深度書き込みなしで描く
            bool twoSided = false;
            FrameCB constants{};

            // 頂点シェーダーへの追加定数バッファ (スキンメッシュのボーンパレット等)
            const D3DBuffer* extraVsCb = nullptr;
            const void* extraVsData = nullptr;
            std::size_t extraVsSize = 0;
            uint extraVsSlot = k_BonePaletteSlot;
        };
#pragma warning(pop)

        /// mesh または material が nullptr の場合は何もしない
        void IssueDrawItem(D3DDevice& device, const DrawItem& item) noexcept;

        /// 指定したパイプラインとピクセルシェーダーで DrawItem を描く (頂点シェーダーと定数は material のものを使用)
        /// pixelShader の入力は SV_POSITION から始めること。mesh または material が nullptr の場合は何もしない
        void IssueDrawItemWith(D3DDevice& device,
                               const DrawItem& item,
                               const D3DPipeline& pipeline,
                               const D3DShader& pixelShader) noexcept;
    } // namespace Gfx
} // namespace NS
