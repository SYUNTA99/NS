#pragma once

/// @file Mesh.h
/// @brief NS::Graphics::Mesh — VB + IB + indexCount の最小バンドル。
///
/// @details 固定頂点フォーマット `MeshVertex` (32 byte: position/uv/normal) 前提。
/// Static Buffer 利用、 `MeshDesc::initialData` は ctor 内でコピー。 Submesh /
/// 複数 Material 切替は glTF 対応時に拡張、 cube は単一マテリアル相当。
/// 依存: Renderer の DeviceContext を内部で保持するため Renderer より先に破棄すること。

#include "Framework/Core/Math.h"
#include "Framework/Graphics/ShaderProgram.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <type_traits>
#include <vector>

struct ID3D11Buffer;

namespace NS::Graphics
{

    class Renderer;
    class Mesh;

    namespace detail
    {
        /// Material / Renderer 拡張から ID3D11Buffer に直接アクセスするための typed friend accessor。
        [[nodiscard]] ID3D11Buffer* GetVertexBuffer(Mesh& mesh) noexcept;
        [[nodiscard]] ID3D11Buffer* GetIndexBuffer(Mesh& mesh) noexcept;
    } // namespace detail

    /// 固定頂点フォーマット (32 byte 固定)。
    /// cube と glTF static の共通形式。
    /// ボーン重み付きの SkinnedMeshVertex は将来別型として追加される。
    struct MeshVertex
    {
        NS::Core::Vector3 position;
        NS::Core::Vector2 uv;
        NS::Core::Vector3 normal;
    };
    static_assert(sizeof(MeshVertex) == 32, "MeshVertex は 32 byte 固定 ()");
    static_assert(std::is_standard_layout_v<MeshVertex>,
                  "MeshVertex は offsetof 使用のため標準レイアウト必須 (StandardInputLayout)");

    /// Mesh 構築パラメータ。Static Buffer 前提で initialData は ctor 内でコピーされる。
    /// Index は uint16_t 固定。65535 vertex 超は将来 UInt32 検討。
    struct MeshDesc
    {
        const MeshVertex* vertices = nullptr;
        std::size_t vertexCount = 0;
        const std::uint16_t* indices = nullptr;
        std::size_t indexCount = 0;
    };

    /// VB + IB + indexCount を単一バンドルにまとめた最小 Mesh。
    /// Submesh / 複数 Material 切替は glTF 対応時に拡張、cube は単一マテリアル相当。
    /// 依存: Renderer の DeviceContext を内部で保持するため Renderer より先に破棄すること。
    class Mesh
    {
    public:
        struct Impl;

        Mesh(Renderer& renderer, const MeshDesc& desc);
        ~Mesh();

        Mesh(const Mesh&) = delete;
        Mesh& operator=(const Mesh&) = delete;
        Mesh(Mesh&&) = delete;
        Mesh& operator=(Mesh&&) = delete;

        /// VB / IB が生成されていれば true。MeshDesc 不正や Buffer 作成失敗で false。
        [[nodiscard]] bool IsValid() const noexcept;
        [[nodiscard]] std::size_t VertexCount() const noexcept;
        [[nodiscard]] std::size_t IndexCount() const noexcept;

        /// VB.Bind(0) + IB.Bind() + IASetPrimitiveTopology(TRIANGLELIST) + DrawIndexed(IndexCount, 0, 0) を一括実行
        ///。 ShaderProgram::Bind() と Material 側の SRV/CB Bind は呼出側責任。
        void Draw() noexcept;

        /// MeshVertex に対応する POSITION / TEXCOORD / NORMAL の InputElement 配列を返す。
        /// 戻り値は ShaderProgramDesc::inputLayout にそのまま流用できる。
        [[nodiscard]] static std::vector<InputElement> StandardInputLayout();

    private:
        std::unique_ptr<Impl> m_pImpl;

        friend ID3D11Buffer* detail::GetVertexBuffer(Mesh& mesh) noexcept;
        friend ID3D11Buffer* detail::GetIndexBuffer(Mesh& mesh) noexcept;
    };

} // namespace NS::Graphics
