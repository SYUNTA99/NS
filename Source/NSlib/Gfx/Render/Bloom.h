#pragma once

#include "NSlib/Core/Math.h"
#include "NSlib/Core/NonCopyable.h"
#include "NSlib/Gfx/D3DCommandList.h"
#include "NSlib/Gfx/D3dCommon.h"

#include <memory>
#include <vector>

namespace NS
{
    namespace Gfx
    {
        class D3DBuffer;
        class D3DPipeline;
        class D3DDevice;
        class D3DShader;
        class D3DTexture;
        struct BuiltinShaders;

        //! bloom.hlsli の cbuffer BloomCB とレイアウトを一致させる
        struct alignas(16) BloomCB
        {
            float sourceTexel[2]{};      // 入力テクスチャ 1 テクセルの UV 幅・高さ
            float destinationTexel[2]{}; // 出力テクスチャ 1 テクセルの UV 幅・高さ
            float threshold = 0.0f;
            float intensity = 0.0f;
            float padding[2]{};
        };
        static_assert(sizeof(BloomCB) == 32, "BloomCB は HLSL の cbuffer BloomCB とバイト一致が必要");

        //! 閾値 1.0 ではトーンマップ後の通常描画 (<= 1.0) を除外し HDR 発光のみを抽出する
        struct BloomDesc
        {
            float threshold = 1.0f; // 色成分ごとに減算する輝度閾値
            float intensity = 0.2f; // 各縮小段から加算する強さ
            int levels = 5;         // 縮小段数 (1 段目は 1/2 解像度、1 未満は 1 に切り上げ)
        };

        //! HDR の高輝度領域を縮小・ぼかして元の描画先へ加算する
        //! D3DDevice より先に破棄し、shaders の破棄後は描画を呼ばないこと
        class Bloom : public NonCopyable
        {
        public:
            //! shaders は非所有で参照のみ保持する。シェーダーの有効性は BeginWorld ごとに判定する
            Bloom(const BloomDesc& desc, const BuiltinShaders& shaders) noexcept;
            ~Bloom();

            [[nodiscard]] bool IsValid() const noexcept;

            //! HDR 描画先へ切り替え、深度は元の描画先と共有して clearColor でクリアする。準備失敗時は元の描画先を維持する
            void BeginWorld(D3DDevice& device, const Color& clearColor) noexcept;

            //! ブルームを合成し、BeginWorld 前の描画先とビューポートへ戻す。BeginWorld と同じ D3DDevice を渡すこと
            void EndWorld(D3DDevice& device) noexcept;

        private:
            [[nodiscard]] bool EnsureTargets(Size2D size) noexcept;

            void DrawPass(D3DCommandList& cmd,
                          const D3DShader& pixelShader,
                          const D3DPipeline& pipeline,
                          const D3DTexture& source,
                          const D3DTexture* scene,
                          ID3D11RenderTargetView* destination,
                          Size2D destinationSize) noexcept;

            BloomDesc m_desc{};

            D3DShader* m_vs = nullptr;
            D3DShader* m_thresholdPs = nullptr;
            D3DShader* m_downPs = nullptr;
            D3DShader* m_upPs = nullptr;
            D3DShader* m_compositePs = nullptr;
            std::unique_ptr<D3DPipeline> m_writePipeline;
            std::unique_ptr<D3DPipeline> m_addPipeline;
            std::unique_ptr<D3DBuffer> m_cb;

            Size2D m_size{0, 0};
            std::unique_ptr<D3DTexture> m_scene;
            std::unique_ptr<D3DTexture> m_bright;
            std::vector<std::unique_ptr<D3DTexture>> m_levels;

            SavedTargets m_saved{};
            bool m_active = false;
            bool m_valid = false;
        };
    } // namespace Gfx
} // namespace NS
