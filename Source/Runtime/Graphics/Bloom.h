#pragma once

#include "Runtime/Core/Math.h"
#include "Runtime/Core/NonCopyable.h"
#include "Runtime/Graphics/D3dCommon.h"

#include <memory>
#include <vector>

namespace NS::Gfx
{

    class Buffer;
    class Pipeline;
    class Shader;
    class Texture;

    //! @brief 光のにじみの設定
    //! @details 物の絵は S 字で 1 以下に書くので、閾値 1 なら 1 を超えて書いたエフェクトだけがにじむ
    struct BloomDesc
    {
        //! これを超えた分の明るさだけをにじませる。色の成分ごとに引く
        float threshold = 1.0f;
        //! @brief にじみを足す強さ
        //! @details 段ごとに超えた分の光を全部持って足し合わせる
        //! 段の数の逆数なら、画面が切り捨てる分と同じ量を周りへ配る
        float intensity = 0.2f;
        //! 縮める段の数。1 段目は描画先の半分の大きさ。1 未満は 1 として扱う
        int levels = 5;
    };

    //! @brief 世界を 1 を超える明るさまで持てる描画先へ描かせ、超えた分を縮めてぼかして元の描画先へ足す
    //! @details BeginWorld で今の描画先を控えて浮動小数の描画先へ差し替える
    //! EndWorld でにじみを足し、控えた描画先へ書き戻す
    //! 深度は控えた描画先の物をそのまま使うので、EndWorld の後に描く線も世界の深度で隠れる
    //! 縮めは 13 点で拾い、戻しは 3×3 の山形で拾って 1 段ずつ足す
    //! 構築時に Gpu() の device を使う。Renderer が未構築なら無効な状態になり、何もしない
    //! @warning Renderer より先に破棄すること
    class Bloom : public NS::Core::NonCopyable
    {
    public:
        //! @brief シェーダ・パイプライン・定数バッファ・サンプラを作る
        //! @param[in] desc 閾値・足す強さ・段の数
        explicit Bloom(const BloomDesc& desc) noexcept;
        ~Bloom();

        //! 構築に成功した場合 true、それ以外の場合は false
        [[nodiscard]] bool IsValid() const noexcept;

        //! @brief 今の描画先を控え、同じ大きさの浮動小数の描画先を clearColor で塗って描画先にする
        //! @details 深度は控えた描画先の物をそのまま差す。大きさが前と違えば浮動小数の描画先を作り直す
        //! 無効な状態・描画先が無い・作り直しに失敗した時は何もせず、世界は今の描画先へそのまま描かれる
        //! @param[in] clearColor 今の描画先を塗った色。空を描かない所に残る
        void BeginWorld(const NS::Core::Color& clearColor) noexcept;

        //! @brief 浮動小数の絵ににじみを足し、1 へ丸めて BeginWorld で控えた描画先へ書く
        //! @details 描画先とビューポートを BeginWorld の前へ戻す。BeginWorld が差し替えなかった時は何もしない
        void EndWorld() noexcept;

    private:
        // 描画先の大きさと段の数に合わせて浮動小数の描画先を揃える。作れなければ false
        [[nodiscard]] bool EnsureTargets(NS::Core::Size2D size) noexcept;

        // 全画面を 1 回描く。source を t0、scene を t1 に差して destination へ書く。scene は無ければ差さない
        void DrawPass(const Shader& pixelShader,
                      const Pipeline& pipeline,
                      const Texture& source,
                      const Texture* scene,
                      ID3D11RenderTargetView* destination,
                      NS::Core::Size2D destinationSize) noexcept;

        BloomDesc m_desc{};

        std::unique_ptr<Shader> m_vs;              // 全画面三角形
        std::unique_ptr<Shader> m_thresholdPs;     // 閾値を超えた分だけを残す
        std::unique_ptr<Shader> m_downPs;          // 13 点で拾って半分に縮める
        std::unique_ptr<Shader> m_upPs;            // 3×3 の山形で拾って 1 段大きい方へ足す
        std::unique_ptr<Shader> m_compositePs;     // 元の絵ににじみを足して 1 へ丸める
        std::unique_ptr<Pipeline> m_writePipeline; // 書き込み先を置き換える
        std::unique_ptr<Pipeline> m_addPipeline;   // 書き込み先へ足す
        std::unique_ptr<Buffer> m_cb;
        ComPtr<ID3D11SamplerState> m_sampler;

        NS::Core::Size2D m_size{0, 0};                  // 浮動小数の描画先の大きさ。0 なら未作成
        std::unique_ptr<Texture> m_scene;               // 世界を描く浮動小数の描画先
        std::unique_ptr<Texture> m_bright;              // 閾値を超えた分。世界と同じ大きさ
        std::vector<std::unique_ptr<Texture>> m_levels; // 縮めた段。先頭が半分の大きさ

        ComPtr<ID3D11RenderTargetView> m_savedTarget; // BeginWorld で控えた描画先
        ComPtr<ID3D11DepthStencilView> m_savedDepth;  // BeginWorld で控えた深度
        D3D11_VIEWPORT m_savedViewport{};
        bool m_active = false; // BeginWorld が差し替えて EndWorld を待っているか
        bool m_valid = false;
    };

} // namespace NS::Gfx
