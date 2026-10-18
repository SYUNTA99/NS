#pragma once

#include "NSlib/Core/Types.h"
#include "NSlib/Gfx/Render/FrameConstants.h"
#include "NSlib/Gfx/D3DPipeline.h"

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

        //! @brief 1回の描画に必要なデータをまとめた構造体
        // FrameCB の 16 バイト整列を値で抱えるため DrawItem に想定どおりのパディングが入る。C4324 を黙らせる
#pragma warning(push)
#pragma warning(disable : 4324)
        struct DrawItem
        {
            D3DMesh* mesh = nullptr;
            D3DMaterial* material = nullptr;
            BlendMode blend = BlendMode::Opaque;
            //! 手前の物に隠れた画素だけへ描く場合 true。その時の合成は blend に関わらず半透明で、深度は書かない
            bool occludedOnly = false;
            bool twoSided = false; //!< 裏からも描く場合 true。偽なら裏を向いた面は描かない
            FrameCB constants{};   //!< オブジェクトの座標やカメラ、ライトなどの基本定数

            // 頂点シェーダへの追加データ。スキンメッシュのボーンパレットがこれを使う
            const D3DBuffer* extraVsCb = nullptr;
            const void* extraVsData = nullptr;
            std::size_t extraVsSize = 0;
            uint extraVsSlot = 1;
        };
#pragma warning(pop)

        //! @brief DrawItem をもとに描画を出す
        //! @note mesh か material が空なら何もしない
        void IssueDrawItem(D3DDevice& device, const DrawItem& item) noexcept;

        //! @brief DrawItem の形を、渡したパイプラインとピクセルシェーダで描く
        //! @details 頂点シェーダと定数は material の物を使うので、骨で動く形も同じ姿勢で描ける
        //! mesh か material が空なら何もしない
        //! @param[in] pixelShader material のピクセルシェーダの代わりに差す物。入力は SV_POSITION から始める
        void IssueDrawItemWith(D3DDevice& device,
                               const DrawItem& item,
                               const D3DPipeline& pipeline,
                               const D3DShader& pixelShader) noexcept;
    } // namespace Gfx
} // namespace NS
