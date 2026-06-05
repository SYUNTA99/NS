#pragma once

/// @file Buffer.h
/// @brief NS::Graphics::VertexBuffer / IndexBuffer / ConstantBuffer — D3D11 Buffer ラッパ
///
/// @details Static / Dynamic の使い分けは `BufferUsage` で指定。 ConstantBuffer は
/// 常に Dynamic 固定、 byteSize は内部で 16-byte 境界に切り上げ。 ConstantBuffer の
/// 型付き更新 `detail::UpdateConstantBuffer<T>` は `sizeof(T) % 16 == 0` をコンパイル時に強制する
/// Index 幅は `IndexFormat` (UInt16 / UInt32)、 公開ヘッダから DXGI_FORMAT を漏らさない
/// ConstantBuffer のバインド/更新は Renderer 経由 (`Renderer::BindConstantBuffer` / `UpdateBuffer`)

#include <cstddef>
#include <memory>

struct ID3D11Buffer;
struct ID3D11DeviceContext;

namespace NS::Graphics
{

    class Renderer;
    class VertexBuffer;
    class IndexBuffer;
    class ConstantBuffer;
    enum class ShaderStage : unsigned;

    namespace detail
    {
        [[nodiscard]] ID3D11Buffer* GetNative(VertexBuffer& vb) noexcept;
        [[nodiscard]] ID3D11Buffer* GetNative(IndexBuffer& ib) noexcept;
        [[nodiscard]] ID3D11Buffer* GetNative(ConstantBuffer& cb) noexcept;

        /// VertexBuffer を context の slot にバインドする (IASetVertexBuffers)。 無効な vb / context==nullptr は no-op
        void BindVertexBuffer(ID3D11DeviceContext* context, VertexBuffer& vb, unsigned slot) noexcept;

        /// Dynamic VertexBuffer を Map/Discard で更新する。 Static や容量超過は NS_LOG_ERROR + no-op
        void UpdateVertexBufferRaw(ID3D11DeviceContext* context,
                                   VertexBuffer& vb,
                                   const void* data,
                                   std::size_t bytes) noexcept;

        /// IndexBuffer を context にバインドする (IASetIndexBuffer、 幅は Format から決定)。 無効/null は no-op
        void BindIndexBuffer(ID3D11DeviceContext* context, IndexBuffer& ib) noexcept;

        /// ConstantBuffer を context のステージ (VS/PS/GS、 HasStage で選択) にバインドする
        /// 無効な cb または context==nullptr は no-op。 Renderer::BindConstantBuffer が本関数を呼ぶ
        void BindConstantBuffer(ID3D11DeviceContext* context,
                                ConstantBuffer& cb,
                                unsigned slot,
                                ShaderStage stages) noexcept;

        /// ConstantBuffer を Map/Discard で更新する (bytes は 16-byte 倍数かつ ByteSize 以内)
        void UpdateConstantBufferRaw(ID3D11DeviceContext* context,
                                     ConstantBuffer& cb,
                                     const void* data,
                                     std::size_t bytes) noexcept;

        /// 型付き ConstantBuffer 更新。 sizeof(T) が 16-byte 倍数であることをコンパイル時に強制する
        template <typename T>
        void UpdateConstantBuffer(ID3D11DeviceContext* context, ConstantBuffer& cb, const T& data) noexcept
        {
            static_assert((sizeof(T) % 16) == 0,
                          "ConstantBuffer の更新は sizeof(T) が 16 byte 倍数で alignas(16) 必須");
            UpdateConstantBufferRaw(context, cb, &data, sizeof(T));
        }
    } // namespace detail

    /// バッファの D3D11 USAGE 切替。ConstantBuffer は常に Dynamic 固定なので Desc 引数なし
    enum class BufferUsage
    {
        Static,  ///< D3D11_USAGE_DEFAULT (Update 不可、固定メッシュ向け)
        Dynamic, ///< D3D11_USAGE_DYNAMIC + CPU_ACCESS_WRITE (Map/Unmap で更新)
    };

    /// IndexBuffer のインデックス幅。公開ヘッダから DXGI_FORMAT を漏らさない独自 enum
    enum class IndexFormat
    {
        UInt16,
        UInt32,
    };

    /// ConstantBuffer::Bind の対象ステージ bitflag
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

    struct VertexBufferDesc
    {
        const void* initialData = nullptr;
        std::size_t vertexCount = 0;
        std::size_t stride = 0;
        BufferUsage usage = BufferUsage::Static;
    };

    struct IndexBufferDesc
    {
        const void* initialData = nullptr;
        std::size_t indexCount = 0;
        IndexFormat format = IndexFormat::UInt32;
        BufferUsage usage = BufferUsage::Static;
    };

    /// 頂点バッファ。stride は 1 頂点バイト数、 バインド/更新は Renderer 経由 (本型は GPU バッファのみ保持)
    /// Static 用途で更新を呼ぶと NS_LOG_ERROR + 早期 return (D3D11 ランタイムエラー回避)
    class VertexBuffer
    {
    public:
        struct Impl;

        VertexBuffer(Renderer& renderer, const VertexBufferDesc& desc);
        ~VertexBuffer();

        VertexBuffer(const VertexBuffer&) = delete;
        VertexBuffer& operator=(const VertexBuffer&) = delete;
        VertexBuffer(VertexBuffer&&) = delete;
        VertexBuffer& operator=(VertexBuffer&&) = delete;

        [[nodiscard]] bool IsValid() const noexcept;
        [[nodiscard]] std::size_t Stride() const noexcept;
        [[nodiscard]] std::size_t VertexCount() const noexcept;

    private:
        std::unique_ptr<Impl> m_pImpl;

        friend ID3D11Buffer* detail::GetNative(VertexBuffer& vb) noexcept;
        friend void detail::BindVertexBuffer(ID3D11DeviceContext* context, VertexBuffer& vb, unsigned slot) noexcept;
        friend void detail::UpdateVertexBufferRaw(ID3D11DeviceContext* context,
                                                  VertexBuffer& vb,
                                                  const void* data,
                                                  std::size_t bytes) noexcept;
    };

    /// インデックスバッファ。Format で R16/R32 を切替、 バインドは Renderer 経由 (本型は GPU バッファのみ保持)
    class IndexBuffer
    {
    public:
        struct Impl;

        IndexBuffer(Renderer& renderer, const IndexBufferDesc& desc);
        ~IndexBuffer();

        IndexBuffer(const IndexBuffer&) = delete;
        IndexBuffer& operator=(const IndexBuffer&) = delete;
        IndexBuffer(IndexBuffer&&) = delete;
        IndexBuffer& operator=(IndexBuffer&&) = delete;

        [[nodiscard]] bool IsValid() const noexcept;
        [[nodiscard]] IndexFormat Format() const noexcept;
        [[nodiscard]] std::size_t IndexCount() const noexcept;

    private:
        std::unique_ptr<Impl> m_pImpl;

        friend ID3D11Buffer* detail::GetNative(IndexBuffer& ib) noexcept;
        friend void detail::BindIndexBuffer(ID3D11DeviceContext* context, IndexBuffer& ib) noexcept;
    };

    /// 定数バッファ。常に Dynamic、byteSize は 16-byte 境界に切り上げて確保される
    /// バインド/更新は Renderer 経由 (`Renderer::BindConstantBuffer` / `UpdateBuffer`)、 本型は GPU バッファのみ保持
    /// alignas(16) と手動 padding (Vector3 a; float _pad;) で internal alignment を担保すること
    class ConstantBuffer
    {
    public:
        struct Impl;

        ConstantBuffer(Renderer& renderer, std::size_t byteSize);
        ~ConstantBuffer();

        ConstantBuffer(const ConstantBuffer&) = delete;
        ConstantBuffer& operator=(const ConstantBuffer&) = delete;
        ConstantBuffer(ConstantBuffer&&) = delete;
        ConstantBuffer& operator=(ConstantBuffer&&) = delete;

        [[nodiscard]] bool IsValid() const noexcept;
        [[nodiscard]] std::size_t ByteSize() const noexcept;

    private:
        std::unique_ptr<Impl> m_pImpl;

        friend ID3D11Buffer* detail::GetNative(ConstantBuffer& cb) noexcept;
        friend void detail::BindConstantBuffer(ID3D11DeviceContext* context,
                                               ConstantBuffer& cb,
                                               unsigned slot,
                                               ShaderStage stages) noexcept;
        friend void detail::UpdateConstantBufferRaw(ID3D11DeviceContext* context,
                                                    ConstantBuffer& cb,
                                                    const void* data,
                                                    std::size_t bytes) noexcept;
    };

} // namespace NS::Graphics
