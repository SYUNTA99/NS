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

        //! fade.ps.hlsl の cbuffer FadeCB とレイアウトを一致させる
        struct alignas(16) FullscreenColorCB
        {
            Color color;
        };
        static_assert(sizeof(FullscreenColorCB) == 16, "FullscreenColorCB は HLSL の cbuffer FadeCB とバイト一致が必要");

        //! ui_rect.vs.hlsl / ui_rect.ps.hlsl の cbuffer UiRectCB とレイアウトを一致させる
        struct alignas(16) ScreenRectCB
        {
            float rect[4]{}; // クリップ空間の左上 (x, y) と幅・高さ (高さは画面下向き正)
            Color color;
        };
        static_assert(sizeof(ScreenRectCB) == 32, "ScreenRectCB は HLSL の cbuffer UiRectCB とバイト一致が必要");

        //! 現在の描画先の最前面へ全画面色や矩形を半透明合成する
        //! D3DDevice より先に破棄し、device と shaders はこのインスタンスより長く生存させること
        class ScreenOverlay : public NonCopyable
        {
        public:
            ScreenOverlay(D3DDevice* device, const BuiltinShaders& shaders) noexcept;
            ~ScreenOverlay();

            void DrawFullscreenColor(const Color& color) noexcept;

            //! 左上原点のピクセル座標で矩形を半透明合成する
            void DrawScreenRect(float x, float y, float width, float height, const Color& color) noexcept;

        private:
            struct Pass
            {
                std::unique_ptr<D3DBuffer> cb;
                std::unique_ptr<D3DPipeline> pipeline;
                bool tried = false;
                bool ready = false;
            };

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
