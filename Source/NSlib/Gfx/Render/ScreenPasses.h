#pragma once

#include "NSlib/Core/Math.h"
#include "NSlib/Core/NonCopyable.h"
#include "NSlib/Gfx/D3DCommandList.h"
#include "NSlib/Gfx/D3dCommon.h"

#include <memory>

namespace NS
{
    namespace Gfx
    {
        class D3DBuffer;
        struct DrawItem;
        class D3DPipeline;
        class D3DDevice;
        class D3DShader;
        class D3DTexture;
        struct BuiltinShaders;

        /// 画面歪みパラメータ (長さの単位はすべて描画先のピクセル)
        struct ScreenRing
        {
            Vector2 centerPixel{};        // 左上原点の中心座標
            float radiusPixels = 0.0f;
            float pushPixels = 0.0f;      // 押し出し量 (0 以下で無効)
            float halfWidthPixels = 0.0f; // 輪の半幅 (0 以下で無効)
            bool keepBody = false;        // true のときボディマスクが 1 の画素を歪めない
        };

        /// screen_distortion.ps.hlsl の cbuffer b0 とレイアウトを一致させる
        struct alignas(16) ScreenDistortionCB
        {
            float ring[4]{};            // 中心 (x, y)、半径、押し出し量 (ピクセル単位)
            float ringHalfWidth = 0.0f; // 輪の半幅 (ピクセル単位)
            float keepBody = 0.0f;      // 0 より大きいときボディマスクが 1 の画素を歪めない
            float padding[2]{};
        };
        static_assert(sizeof(ScreenDistortionCB) == 32, "ScreenDistortionCB は HLSL の cbuffer b0 とバイト一致が必要");

        /// ポストプロセス演出 (画面歪み・ボディマスク描画) を行う
        /// D3DDevice より先に破棄し、shaders の破棄後は描画を呼ばないこと
        class ScreenPasses : public NonCopyable
        {
        public:
            /// shaders は非所有で参照のみ保持する
            explicit ScreenPasses(const BuiltinShaders& shaders) noexcept;
            ~ScreenPasses();

            [[nodiscard]] bool IsValid() const noexcept;

            /// 現在の描画先に輪状の歪みを適用する。描画先・深度・ビューポートは呼び出し前の状態へ戻す
            void Distort(D3DDevice& device, const ScreenRing& ring) noexcept;

            /// ボディマスクを 0 でクリアして描画先に設定する (深度は現在のものを読み取り専用で共有)
            /// true を返した場合は描画後に EndBodyMask を呼ぶこと
            [[nodiscard]] bool BeginBodyMask(D3DDevice& device) noexcept;

            /// BeginBodyMask と EndBodyMask の間で呼び、深度テストを通過した画素へ 1 を書き込む
            void DrawBodyMaskItem(D3DDevice& device, const DrawItem& item) noexcept;

            /// 描画先・深度・ビューポートを BeginBodyMask 前の状態へ戻す
            void EndBodyMask(D3DDevice& device) noexcept;

            /// ボディマスクを描画しないフレームで呼び、前フレームのマスクを無効化する
            void DropBodyMask() noexcept { m_bodyMaskDrawn = false; }

            /// 現フレームで描画済みのボディマスク (R8_UNORM) を返す。未描画時は nullptr
            [[nodiscard]] const D3DTexture* BodyMask() const noexcept;

        private:
            [[nodiscard]] bool EnsureCopy(ID3D11Texture2D& target) noexcept;

            [[nodiscard]] bool EnsureBodyMask(const D3D11_TEXTURE2D_DESC& target) noexcept;

            D3DShader* m_vs = nullptr;
            D3DShader* m_distortionPs = nullptr;
            D3DShader* m_bodyMaskPs = nullptr;
            std::unique_ptr<D3DPipeline> m_writePipeline;
            std::unique_ptr<D3DPipeline> m_bodyMaskPipelines[2];
            std::unique_ptr<D3DBuffer> m_cb;

            std::unique_ptr<D3DTexture> m_copy;
            D3D11_TEXTURE2D_DESC m_copyDesc{};
            std::unique_ptr<D3DTexture> m_bodyMask;
            bool m_bodyMaskDrawn = false;

            SavedTargets m_saved{};
            bool m_valid = false;
        };
    } // namespace Gfx
} // namespace NS
