#pragma once
#include "NSlib/Core/NonCopyable.h"
#include "NSlib/Graphics/D3dCommon.h"

#include <cstddef>
#include <memory>
namespace NS
{
    namespace Gfx
    {
        //! バッファ構築パラメータ。役割は D3D11_BIND_* の bindFlags で表す
        //! 用途別の値は MakeVertexBufferDesc 等のヘルパが埋める
        struct D3DBufferDesc
        {
            const void* initialData = nullptr;              //!< 動的バッファ以外では必須
            std::size_t byteSize = 0;                       //!< 定数バッファの場合は内部で16バイト境界に切り上げられる
            std::size_t stride = 0;                         //!< 頂点バッファ利用時の1頂点あたりのバイト数
            DXGI_FORMAT indexFormat = DXGI_FORMAT_R32_UINT; //!< インデックスバッファ利用時のフォーマット
            D3D11_USAGE usage = D3D11_USAGE_DEFAULT;        //!< CPUからのアクセスフラグは本値に依存して自動決定される
            UINT bindFlags = 0;                             //!< バッファの役割を指定する
        };

        //! @brief 頂点 / インデックス / 定数を兼ねる単一 D3DBuffer
        //! @details 役割は bindFlags で決まる。バインドと更新は D3DCommandList 経由
        //! 定数用途は alignas(16) と手動 padding で 16 バイト境界を守ること
        class D3DBuffer : public NonCopyable
        {
        public:
            [[nodiscard]] static std::unique_ptr<D3DBuffer> Create(const D3DBufferDesc& desc);
            //! ID3D11Buffer の生成に成功しているか
            [[nodiscard]] bool IsValid() const noexcept;
            //! OSのグラフィックスAPIと直接やり取りする境界処理でのみ使用する
            [[nodiscard]] ID3D11Buffer* Native() const noexcept;
            //! 頂点 1 個のバイト数
            [[nodiscard]] std::size_t Stride() const noexcept;
            //! 確保したバイト数。定数バッファは 16 バイト境界へ切り上げ済み
            [[nodiscard]] std::size_t ByteSize() const noexcept;
            [[nodiscard]] DXGI_FORMAT Format() const noexcept;
            [[nodiscard]] bool IsDynamic() const noexcept;

        private:
            explicit D3DBuffer(const D3DBufferDesc& desc);
            ComPtr<ID3D11Buffer> m_buffer;
            std::size_t m_stride = 0;
            std::size_t m_byteSize = 0;
            DXGI_FORMAT m_format = DXGI_FORMAT_R32_UINT;
            bool m_dynamic = false;
        };

        //! DEFAULT の場合は data が必須。stride は頂点 1 個のバイト数
        [[nodiscard]] D3DBufferDesc MakeVertexBufferDesc(const void* data,
                                                      std::size_t vertexCount,
                                                      std::size_t stride,
                                                      D3D11_USAGE usage = D3D11_USAGE_DEFAULT) noexcept;

        //! DEFAULT の場合は data が必須。format は R16_UINT または R32_UINT
        [[nodiscard]] D3DBufferDesc MakeIndexBufferDesc(const void* data,
                                                     std::size_t indexCount,
                                                     DXGI_FORMAT format = DXGI_FORMAT_R32_UINT,
                                                     D3D11_USAGE usage = D3D11_USAGE_DEFAULT) noexcept;

        //! 動的な定数バッファ用。byteSize は内部で 16 バイト境界へ切り上げる
        [[nodiscard]] D3DBufferDesc MakeConstantBufferDesc(std::size_t byteSize) noexcept;
    } // namespace Gfx
} // namespace NS
