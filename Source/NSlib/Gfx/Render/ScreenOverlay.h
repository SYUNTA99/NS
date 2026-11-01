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

        //! ui_line.vs.hlsl / ui_line.ps.hlsl の cbuffer UiLineCB とレイアウトを一致させる
        //! 四隅はクリップ空間で、始まりの片側・始まりの反対側・終わりの片側・終わりの反対側の順
        struct alignas(16) ScreenLineCB
        {
            float cornersA[4]{}; // 始まりの 2 つの隅の x, y
            float cornersB[4]{}; // 終わりの 2 つの隅の x, y
            Color color;
        };
        static_assert(sizeof(ScreenLineCB) == 48, "ScreenLineCB は HLSL の cbuffer UiLineCB とバイト一致が必要");

        //! 左上原点のピクセル座標の両端と太さ (ピクセル) から、線の四隅をクリップ空間で作る。太さは線に垂直な幅
        //! @return 長さか太さが 0 以下か、描画先の大きさが 0 の場合は false で outLine を書き換えない
        [[nodiscard]] bool MakeScreenLineConstants(float startX,
                                                   float startY,
                                                   float endX,
                                                   float endY,
                                                   float thickness,
                                                   const Color& color,
                                                   Size2D targetSize,
                                                   ScreenLineCB& outLine) noexcept;

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

            //! 左上原点のピクセル座標の両端を結ぶ、太さ thickness (ピクセル) の線を半透明合成する。斜めでもよい
            void DrawScreenLine(
                float startX, float startY, float endX, float endY, float thickness, const Color& color) noexcept;

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
            D3DShader* m_screenLineVs = nullptr;
            D3DShader* m_screenLinePs = nullptr;
            Pass m_fullscreen;
            Pass m_screenRect;
            Pass m_screenLine;
        };
    } // namespace Gfx
} // namespace NS
