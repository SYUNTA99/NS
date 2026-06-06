#pragma once

/// @file Buffer.h
/// @brief NS::Graphics::Buffer — 単一の D3D11 Buffer ラッパ (頂点 / index / 定数を bindFlags で兼用)
///
/// @details 役割は `BufferDesc::bindFlags` (D3D11_BIND_VERTEX_BUFFER 等) で決まる。 生成は
/// `MakeVertexBufferDesc` / `MakeIndexBufferDesc` / `MakeConstantBufferDesc` ヘルパ経由が簡便
/// (byteSize / bindFlags を用途別に埋める)。 定数バッファは byteSize を 16-byte 境界へ切り上げ、
/// usage は Dynamic 前提で生成する。 Static バッファは initialData 必須 (未初期化 GPU 読み回避)
/// バインド / 更新は context を持つ Renderer 経由 (`Renderer::BindVertexBuffer` / `UpdateBuffer` 等)
/// Graphics は exposed-D3D lean 設計のため `ID3D11Buffer*` を `Native()` で公開する

#include <cstddef>

#include <d3d11.h>
#include <wrl/client.h>

namespace NS::Graphics
{

    class Renderer;
    class Buffer;

    /// バッファの D3D11 USAGE 切替。ConstantBuffer は Dynamic 前提
    enum class BufferUsage
    {
        Static,  ///< D3D11_USAGE_DEFAULT (Update 不可、 固定データ向け)
        Dynamic, ///< D3D11_USAGE_DYNAMIC + CPU_ACCESS_WRITE (Map/Discard で更新)
    };

    /// index 幅。公開ヘッダから DXGI_FORMAT を漏らさない独自 enum
    enum class IndexFormat
    {
        UInt16,
        UInt32,
    };

    /// 定数バッファ Bind の対象ステージ bitflag
    enum class ShaderStage : unsigned
    {
        None = 0,
        Vertex = 1u << 0,
        Pixel = 1u << 1,
        Geometry = 1u << 2,
        All = Vertex | Pixel | Geometry,
    };

    [[nodiscard]] constexpr ShaderStage operator|(ShaderStage a, ShaderStage b) noexcept
    {
        return static_cast<ShaderStage>(static_cast<unsigned>(a) | static_cast<unsigned>(b));
    }
    [[nodiscard]] constexpr ShaderStage operator&(ShaderStage a, ShaderStage b) noexcept
    {
        return static_cast<ShaderStage>(static_cast<unsigned>(a) & static_cast<unsigned>(b));
    }
    [[nodiscard]] constexpr bool HasStage(ShaderStage set, ShaderStage flag) noexcept
    {
        return (static_cast<unsigned>(set) & static_cast<unsigned>(flag)) != 0u;
    }

    /// バッファ構築パラメータ。役割は bindFlags (D3D11_BIND_*) で表す
    /// 用途別の埋め込みは `MakeVertexBufferDesc` 等のヘルパが行う
    struct BufferDesc
    {
        const void* initialData = nullptr;             ///< 初期データ。Static は必須
        std::size_t byteSize = 0;                      ///< 総バイト数 (定数は内部で 16 切り上げ)
        std::size_t stride = 0;                        ///< 頂点 1 個のバイト数 (頂点用)
        IndexFormat indexFormat = IndexFormat::UInt32; ///< index 幅 (index 用)
        BufferUsage usage = BufferUsage::Static;       ///< USAGE 切替
        UINT bindFlags = 0;                            ///< D3D11_BIND_VERTEX_BUFFER 等 (役割)
    };

    /// 頂点 / index / 定数を兼ねる単一 Buffer。役割は bindFlags で決まる
    /// バインド / 更新は Renderer 経由 (本型は GPU バッファ + メタのみ保持、 context は持たない)
    /// alignas(16) と手動 padding (Vector3 a; float _pad;) で定数の internal alignment を担保すること
    class Buffer
    {
    public:
        Buffer(Renderer& renderer, const BufferDesc& desc);

        Buffer(const Buffer&) = delete;
        Buffer& operator=(const Buffer&) = delete;
        Buffer(Buffer&&) = delete;
        Buffer& operator=(Buffer&&) = delete;

        /// CreateBuffer 成功で true。device 無効 / byteSize=0 / Static で initialData 欠如時 false
        [[nodiscard]] bool IsValid() const noexcept;

        /// 内部 ID3D11Buffer。継ぎ目で raw D3D を扱う Renderer / detail が使う
        [[nodiscard]] ID3D11Buffer* Native() const noexcept;

        [[nodiscard]] std::size_t Stride() const noexcept;
        [[nodiscard]] std::size_t ByteSize() const noexcept;
        [[nodiscard]] IndexFormat Format() const noexcept;
        [[nodiscard]] BufferUsage Usage() const noexcept;

    private:
        Microsoft::WRL::ComPtr<ID3D11Buffer> m_buffer;
        std::size_t m_stride = 0;
        std::size_t m_byteSize = 0;
        IndexFormat m_format = IndexFormat::UInt32;
        BufferUsage m_usage = BufferUsage::Static;
    };

    /// 頂点バッファ用 BufferDesc を組む (byteSize = vertexCount * stride、 bind = VERTEX)
    [[nodiscard]] BufferDesc MakeVertexBufferDesc(const void* data,
                                                  std::size_t vertexCount,
                                                  std::size_t stride,
                                                  BufferUsage usage = BufferUsage::Static) noexcept;

    /// index バッファ用 BufferDesc を組む (byteSize = indexCount * 要素サイズ、 bind = INDEX)
    [[nodiscard]] BufferDesc MakeIndexBufferDesc(const void* data,
                                                 std::size_t indexCount,
                                                 IndexFormat format = IndexFormat::UInt32,
                                                 BufferUsage usage = BufferUsage::Static) noexcept;

    /// 定数バッファ用 BufferDesc を組む (usage = Dynamic、 bind = CONSTANT、 byteSize は ctor が 16 切り上げ)
    [[nodiscard]] BufferDesc MakeConstantBufferDesc(std::size_t byteSize) noexcept;

} // namespace NS::Graphics
