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

        //! @brief 光のにじみの設定
        //! @details 物の絵は S 字で 1 以下に書くので、閾値 1 なら 1 を超えて書いたエフェクトだけがにじむ
        struct BloomDesc
        {
            //! これを超えた分の明るさだけをにじませる。色の成分ごとに引く
            float threshold = 1.0f;
            //! 各ぼかし段から加算する光の強さ
            float intensity = 0.2f;
            //! 縮める段の数。1 段目は描画先の半分の大きさ。1 未満は 1 として扱う
            int levels = 5;
        };

        //! @brief HDR の明るい部分をぼかして元の描画先へ加算する
        //! @details BeginWorld と EndWorld で描画先を切り替える。深度は元の描画先を共有する
        //! @warning D3DDevice より先に破棄すること。シェーダーの持ち主が捨てた後は描画を呼ばない
        class Bloom : public NonCopyable
        {
        public:
            //! @brief パイプライン・定数バッファを作る
            //! @param[in] desc 閾値・足す強さ・段の数
            //! @param[in] shaders fullscreenVs と bloom の 4 つを指すだけで持たない
            //! 使えるかは BeginWorld の度に決め、どれかが使えなければそのフレームは元の描画先で続ける
            Bloom(const BloomDesc& desc, const BuiltinShaders& shaders) noexcept;
            ~Bloom();

            //! パイプラインと定数バッファを作れた場合 true、それ以外の場合は false
            [[nodiscard]] bool IsValid() const noexcept;

            //! HDR 描画へ切り替える。準備に失敗した場合は元の描画先で続ける
            //! 深度は共有し、clearColor で背景を塗る
            void BeginWorld(D3DDevice& device, const Color& clearColor) noexcept;

            //! にじみを加算し、元の描画先とビューポートへ戻す。BeginWorld と同じ D3DDevice を渡す
            void EndWorld(D3DDevice& device) noexcept;

        private:
            // 描画先の大きさと段の数に合わせて浮動小数の描画先を揃える。作れなければ false
            [[nodiscard]] bool EnsureTargets(Size2D size) noexcept;

            // 全画面を 1 回描く。source を t0、scene を t1 に差して destination へ書く。scene は無ければ差さない
            void DrawPass(D3DCommandList& cmd,
                          const D3DShader& pixelShader,
                          const D3DPipeline& pipeline,
                          const D3DTexture& source,
                          const D3DTexture* scene,
                          ID3D11RenderTargetView* destination,
                          Size2D destinationSize) noexcept;

            BloomDesc m_desc{};

            D3DShader* m_vs = nullptr;                    // 全画面三角形
            D3DShader* m_thresholdPs = nullptr;           // 閾値を超えた分だけを残す
            D3DShader* m_downPs = nullptr;                // 13 点で拾って半分に縮める
            D3DShader* m_upPs = nullptr;                  // 3×3 の山形で拾って 1 段大きい方へ足す
            D3DShader* m_compositePs = nullptr;           // 元の絵ににじみを足して 1 へ丸める
            std::unique_ptr<D3DPipeline> m_writePipeline; // 書き込み先を置き換える
            std::unique_ptr<D3DPipeline> m_addPipeline;   // 書き込み先へ足す
            std::unique_ptr<D3DBuffer> m_cb;

            Size2D m_size{0, 0};                        // 浮動小数の描画先の大きさ。0 なら未作成
            std::unique_ptr<D3DTexture> m_scene;               // 世界を描く浮動小数の描画先
            std::unique_ptr<D3DTexture> m_bright;              // 閾値を超えた分。世界と同じ大きさ
            std::vector<std::unique_ptr<D3DTexture>> m_levels; // 縮めた段。先頭が半分の大きさ

            SavedTargets m_saved{}; // BeginWorld で控えた描画先・深度・ビューポート
            bool m_active = false; // BeginWorld が差し替えて EndWorld を待っているか
            bool m_valid = false;
        };
    } // namespace Gfx
} // namespace NS
