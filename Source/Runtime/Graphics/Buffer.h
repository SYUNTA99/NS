#pragma once
#include "Runtime/Core/NonCopyable.h"
#include "Runtime/Graphics/D3dCommon.h"

#include <cstddef>
#include <memory>
namespace NS::Graphics
{
    class Buffer;
    /// バッファ構築パラメータ。役割は D3D11_BIND_* の bindFlags で表す
    /// 用途別の値は `MakeVertexBufferDesc` 等のヘルパが埋める
    struct BufferDesc
    {
        const void* initialData = nullptr;              ///< 動的バッファ以外では必須
        std::size_t byteSize = 0;                       ///< 定数バッファの場合は内部で16バイト境界に切り上げられる
        std::size_t stride = 0;                         ///< 頂点バッファ利用時の1頂点あたりのバイト数
        DXGI_FORMAT indexFormat = DXGI_FORMAT_R32_UINT; ///< インデックスバッファ利用時のフォーマット
        D3D11_USAGE usage = D3D11_USAGE_DEFAULT;        ///< CPUからのアクセスフラグは本値に依存して自動決定される
        UINT bindFlags = 0;                             ///< バッファの役割を指定する
    };

    /// @brief 頂点 / インデックス / 定数を兼ねる単一 Buffer
    /// @details 役割は bindFlags で決まる。バインドと更新は CommandList 経由。
    /// 定数用途は alignas(16) と手動 padding で 16 バイト境界を守ること。
    class Buffer : public NS::Core::NonCopyable
    {
    public:
        /// バッファを生成する
        [[nodiscard]] static std::unique_ptr<Buffer> Create(const BufferDesc& desc);
        [[nodiscard]] bool IsValid() const noexcept;
        /// OSのグラフィックスAPIと直接やり取りする境界処理でのみ使用する
        [[nodiscard]] ID3D11Buffer* Native() const noexcept;
        [[nodiscard]] std::size_t Stride() const noexcept;
        [[nodiscard]] std::size_t ByteSize() const noexcept;
        [[nodiscard]] DXGI_FORMAT Format() const noexcept;
        /// CPUからの動的な内容更新が可能かどうか
        [[nodiscard]] bool IsDynamic() const noexcept;

    private:
        explicit Buffer(const BufferDesc& desc);
        ComPtr<ID3D11Buffer> m_buffer;
        std::size_t m_stride = 0;
        std::size_t m_byteSize = 0;
        DXGI_FORMAT m_format = DXGI_FORMAT_R32_UINT;
        bool m_dynamic = false;
    };

    /// 頂点バッファ用 BufferDesc を作成する。byteSize = vertexCount * stride、bind = VERTEX
    /// @param data        頂点データの先頭。DEFAULT usage では必須
    /// @param vertexCount 頂点の個数
    /// @param stride      頂点 1 個のバイト数
    /// @param usage       バッファの用途。既定は GPU 読み取り専用の DEFAULT
    [[nodiscard]] BufferDesc MakeVertexBufferDesc(const void* data,
                                                  std::size_t vertexCount,
                                                  std::size_t stride,
                                                  D3D11_USAGE usage = D3D11_USAGE_DEFAULT) noexcept;

    /// インデックスバッファ用 BufferDesc を作成する。byteSize = indexCount * 要素サイズ、bind = INDEX
    /// @param data       index データの先頭。DEFAULT usage では必須
    /// @param indexCount index の個数
    /// @param format     index の型。R16_UINT か R32_UINT
    /// @param usage      バッファの用途。既定は GPU 読み取り専用の DEFAULT
    [[nodiscard]] BufferDesc MakeIndexBufferDesc(const void* data,
                                                 std::size_t indexCount,
                                                 DXGI_FORMAT format = DXGI_FORMAT_R32_UINT,
                                                 D3D11_USAGE usage = D3D11_USAGE_DEFAULT) noexcept;

    /// 定数バッファ用 BufferDesc を作成する。usage = Dynamic、bind = CONSTANT、byteSize はコンストラクタが 16 切り上げ
    /// @param byteSize 構造体のバイト数。内部で 16 バイト境界へ切り上げる
    [[nodiscard]] BufferDesc MakeConstantBufferDesc(std::size_t byteSize) noexcept;
} // namespace NS::Graphics