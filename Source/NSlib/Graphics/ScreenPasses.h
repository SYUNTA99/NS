#pragma once

#include "NSlib/Core/Math.h"
#include "NSlib/Core/NonCopyable.h"
#include "NSlib/Graphics/D3dCommon.h"

#include <memory>

namespace NS::Gfx
{

    class Buffer;
    struct DrawItem;
    class Pipeline;
    class Renderer;
    class Shader;
    class Texture;

    //! @brief 歪みの段に渡す輪。長さはどれも描画先の画素
    struct ScreenRing
    {
        NS::Vector2 centerPixel{};    //!< 中心。左上が原点
        float radiusPixels = 0.0f;    //!< 輪の半径
        float pushPixels = 0.0f;      //!< 輪の真ん中で絵を押し出す長さ。0 以下なら歪めない
        float halfWidthPixels = 0.0f; //!< 輪の半分の幅。0 以下なら歪めない
        bool keepBody = false;        //!< 体の型が 1 の画素を押さない場合 true
    };

    //! @brief 歪みのシェーダーの定数
    //! @details screen_distortion.ps.hlsl の cbuffer b0 とバイト一致させる
    struct alignas(16) ScreenDistortionCB
    {
        float ring[4]{};            //!< 輪の中心の x・y、半径、押し。どれも描画先の画素
        float ringHalfWidth = 0.0f; //!< 輪の半分の幅。描画先の画素
        float keepBody = 0.0f;      //!< 0 より大きいなら、体の型が 1 の画素を押さない
        float padding[2]{};
    };
    static_assert(sizeof(ScreenDistortionCB) == 32, "ScreenDistortionCB は HLSL の cbuffer b0 とバイト一致が必要");

    //! @brief 出来上がった絵に、画面全体に掛ける演出を掛ける
    //! @details 描画先を同じ形の絵へ写し、写しを読んで全画面を 1 回描いて描画先へ書く
    //! 選んだ描く物だけを世界の深度で隠して 1 色で写した体の型も作る
    //! @warning Renderer より先に破棄すること
    class ScreenPasses : public NS::NonCopyable
    {
    public:
        //! シェーダ・パイプライン・定数バッファを作る。Renderer が未構築なら無効な状態になる
        ScreenPasses() noexcept;
        ~ScreenPasses();

        //! 構築に成功した場合 true、それ以外の場合は false
        [[nodiscard]] bool IsValid() const noexcept;

        //! @brief 今の描画先の絵の輪の所を、輪の外へ押し出す
        //! @details 押しか幅が 0 以下・無効な状態・描画先が無い時は何もしない
        //! 描画先・深度・ビューポートは呼ぶ前の物へ戻す
        //! @param[in,out] renderer 描く命令を借りる先。今の描画先とビューポートもここから読む
        void Distort(Renderer& renderer, const ScreenRing& ring) noexcept;

        //! @brief 体の型を 0 で塗り、型を描画先にする
        //! @details 深度は今の物を読むだけ。true を返した時だけ、描いた後に EndBodyMask を呼ぶ
        //! @return 型を描画先にできた場合 true、それ以外の場合は false
        [[nodiscard]] bool BeginBodyMask(Renderer& renderer) noexcept;

        //! @brief 描く物の写る画素のうち、世界の深度で隠れていない所を型に 1 で描く
        //! @details BeginBodyMask と EndBodyMask の間で呼ぶ
        void DrawBodyMaskItem(Renderer& renderer, const DrawItem& item) noexcept;

        //! 描画先・深度・ビューポートを BeginBodyMask の前へ戻し、BodyMask が型を返すようにする
        void EndBodyMask(Renderer& renderer) noexcept;

        //! 体の型を描かないフレームに呼び、前のフレームの型を Distort に読ませない
        void DropBodyMask() noexcept { m_bodyMaskDrawn = false; }

        //! @brief 今のフレームに描いた体の型を返す
        //! @return 1 色 8 ビットの描画先と同じ大きさの型。描いていなければ null
        [[nodiscard]] const Texture* BodyMask() const noexcept;

    private:
        [[nodiscard]] bool EnsureCopy(ID3D11Texture2D& target) noexcept;

        [[nodiscard]] bool EnsureBodyMask(const D3D11_TEXTURE2D_DESC& target) noexcept;

        std::unique_ptr<Shader> m_vs;              // 全画面三角形
        std::unique_ptr<Shader> m_distortionPs;    // 輪の所を押し出す
        std::unique_ptr<Shader> m_bodyMaskPs;      // 写る画素を 1 にする
        std::unique_ptr<Pipeline> m_writePipeline; // 書き込み先を置き換える
        //! 体の型を描く。世界の深度は読むだけ。両面の描く物は添字 1
        std::unique_ptr<Pipeline> m_bodyMaskPipelines[2];
        std::unique_ptr<Buffer> m_cb;

        std::unique_ptr<Texture> m_copy; // 描画先の写し。読みながら同じ描画先へは書けないので取る
        D3D11_TEXTURE2D_DESC m_copyDesc{};
        std::unique_ptr<Texture> m_bodyMask; // 体の型。描画先と同じ大きさ
        bool m_bodyMaskDrawn = false;

        // BeginBodyMask から EndBodyMask の間だけ持つ
        ComPtr<ID3D11RenderTargetView> m_savedTarget;
        ComPtr<ID3D11DepthStencilView> m_savedDepth;
        D3D11_VIEWPORT m_savedViewport{};
        bool m_valid = false;
    };

} // namespace NS::Gfx
