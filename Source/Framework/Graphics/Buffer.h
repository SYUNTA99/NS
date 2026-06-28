#pragma once

/// @file Buffer.h
/// @brief NS::Graphics::Buffer — 単一の D3D11 Buffer ラッパ (頂点 / index / 定数を bindFlags で兼用)
///
/// @details 役割は `BufferDesc::bindFlags` (D3D11_BIND_VERTEX_BUFFER 等) で決まる。 生成は
/// `MakeVertexBufferDesc` / `MakeIndexBufferDesc` / `MakeConstantBufferDesc` ヘルパ経由が簡便
/// (byteSize / bindFlags を用途別に埋める)。 定数バッファは byteSize を 16-byte 境界へ切り上げ、
/// usage は Dynamic 前提で生成する。 非 Dynamic バッファは initialData 必須 (未初期化 GPU 読み回避)
/// バインド / 更新は CommandList 経由 (`renderer.Commands().SetVertexBuffer` / `UpdateBuffer` 等)
/// D3D11 型を公開する設計のため `ID3D11Buffer*` を `Native()` で公開する

#include <cstddef>
#include <memory>

#include "Framework/Core/NonCopyable.h"
#include "Framework/Graphics/D3dCommon.h"

namespace NS::Graphics
{

    class Buffer;

    /// バッファ構築パラメータ。役割は bindFlags (D3D11_BIND_*) で表す
    /// 用途別の埋め込みは `MakeVertexBufferDesc` 等のヘルパが行う
    struct BufferDesc
    {
        /// 初期データ。非 Dynamic は必須
        const void* initialData = nullptr;
        /// 総バイト数。定数は内部で 16 byte 境界へ切り上げる
        std::size_t byteSize = 0;
        /// 頂点 1 個のバイト数。頂点用途で使う
        std::size_t stride = 0;
        /// index 幅。index 用途で使う
        DXGI_FORMAT indexFormat = DXGI_FORMAT_R32_UINT;
        /// USAGE 切替。CPUAccessFlags は usage から導出する
        D3D11_USAGE usage = D3D11_USAGE_DEFAULT;
        /// D3D11_BIND_VERTEX_BUFFER 等の役割を表す
        UINT bindFlags = 0;
    };

    /// 頂点 / index / 定数を兼ねる単一 Buffer。役割は bindFlags で決まり、バインド / 更新は CommandList 経由
    /// 定数用途は alignas(16) と手動 padding で 16 byte 境界を担保すること
    class Buffer : public NS::Core::NonCopyable
    {
    public:
        /// BufferDesc から Buffer を生成する。 生成失敗でも非 null を返し IsValid() が false になる
        [[nodiscard]] static std::unique_ptr<Buffer> Create(const BufferDesc& desc);

        /// CreateBuffer 成功で true。device 無効 / byteSize=0 / 非 Dynamic で initialData 欠如時 false
        [[nodiscard]] bool IsValid() const noexcept;

        /// 内部 ID3D11Buffer。継ぎ目で生 D3D を扱う Renderer / detail が使う
        [[nodiscard]] ID3D11Buffer* Native() const noexcept;

        [[nodiscard]] std::size_t Stride() const noexcept;
        [[nodiscard]] std::size_t ByteSize() const noexcept;
        [[nodiscard]] DXGI_FORMAT Format() const noexcept;

        /// Map/Discard 更新が可能か (CPU 書込可 = D3D11_USAGE_DYNAMIC)。UpdateBuffer の可否判定に使う
        [[nodiscard]] bool IsDynamic() const noexcept;

    private:
        explicit Buffer(const BufferDesc& desc);

        ComPtr<ID3D11Buffer> m_buffer;
        std::size_t m_stride = 0;
        std::size_t m_byteSize = 0;
        DXGI_FORMAT m_format = DXGI_FORMAT_R32_UINT;
        bool m_dynamic = false;
    };

    /// 頂点バッファ用 BufferDesc を組む (byteSize = vertexCount * stride、 bind = VERTEX)
    [[nodiscard]] BufferDesc MakeVertexBufferDesc(const void* data,
                                                  std::size_t vertexCount,
                                                  std::size_t stride,
                                                  D3D11_USAGE usage = D3D11_USAGE_DEFAULT) noexcept;

    /// index バッファ用 BufferDesc を組む (byteSize = indexCount * 要素サイズ、 bind = INDEX)
    [[nodiscard]] BufferDesc MakeIndexBufferDesc(const void* data,
                                                 std::size_t indexCount,
                                                 DXGI_FORMAT format = DXGI_FORMAT_R32_UINT,
                                                 D3D11_USAGE usage = D3D11_USAGE_DEFAULT) noexcept;

    /// 定数バッファ用 BufferDesc を組む (usage = Dynamic、 bind = CONSTANT、 byteSize はコンストラクタが 16 切り上げ)
    [[nodiscard]] BufferDesc MakeConstantBufferDesc(std::size_t byteSize) noexcept;

} // namespace NS::Graphics
