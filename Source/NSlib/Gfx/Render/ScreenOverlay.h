#pragma once

#include "NSlib/Core/Math.h"
#include "NSlib/Core/NonCopyable.h"
#include "NSlib/Core/Types.h"

#include <cstddef>
#include <memory>
#include <string_view>

namespace NS
{
    namespace Gfx
    {
        class D3DBuffer;
        class D3DPipeline;
        class D3DShader;
        class D3DDevice;
        struct BuiltinShaders;

        //! @brief 今の描画先の最前面へ、全画面の色と画面の矩形を半透明で重ねる
        //! @details 描く資源は初回の描画で作る。装置が無い時と IsUsable が使えないと答えた時は何も描かない
        //! @warning D3DDevice より先に破棄する。シェーダーの持ち主が捨てた後は描画を呼ばない
        class ScreenOverlay : public NonCopyable
        {
        public:
            //! @param[in,out] device 描く命令と描画先の大きさを借りる先。この型より長く生きる
            //! @param[in] shaders fullscreenVs・fadePs・screenRectVs・screenRectPs を指すだけで持たない
            ScreenOverlay(D3DDevice* device, const BuiltinShaders& shaders) noexcept;
            ~ScreenOverlay();

            //! 全画面に指定色を半透明合成する。最前面に出す場合は最後に呼ぶ
            void DrawFullscreenColor(const Color& color) noexcept;

            //! 左上原点のピクセル座標で矩形を半透明合成する。最前面に出す場合は最後に呼ぶ
            void DrawScreenRect(float x, float y, float width, float height, const Color& color) noexcept;

        private:
            // 頂点を持たず SV_VertexID から形を作る描画の資源
            struct Pass
            {
                std::unique_ptr<D3DBuffer> cb;
                std::unique_ptr<D3DPipeline> pipeline;
                bool tried = false;
                bool ready = false;
            };

            // 描く資源を初回だけ構築し、今描けるかを返す。シェーダーは毎回 IsUsable で見る
            // label はログに出す名前
            [[nodiscard]] bool EnsurePass(Pass& pass,
                                          const D3DShader* vs,
                                          const D3DShader* ps,
                                          std::size_t constantBytes,
                                          std::string_view label) noexcept;

            void DrawPass(const Pass& pass,
                          const D3DShader& vs,
                          const D3DShader& ps,
                          const void* constants,
                          std::size_t constantBytes,
                          uint vertexCount,
                          bool vsReadsConstants) noexcept;

            D3DDevice* m_device = nullptr;
            D3DShader* m_fullscreenVs = nullptr;
            D3DShader* m_fadePs = nullptr;
            D3DShader* m_screenRectVs = nullptr;
            D3DShader* m_screenRectPs = nullptr;
            Pass m_fullscreen;
            Pass m_screenRect;
        };
    } // namespace Gfx
} // namespace NS
