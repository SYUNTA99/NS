#pragma once

/// @file Mesh.h
/// @brief NS::Graphics::Mesh — 描画できるジオメトリの基底 (VB / IB を所有し DrawIndexed を発行する)
///
/// @details StaticMesh / SkeletalMesh の共通実体 = GPU 頂点 / index buffer と描画呼出を持つ
/// 頂点フォーマットは派生が決める (StaticMesh は StaticVertex、 SkeletalMesh は SkinnedVertex)
/// 派生は構築した VB / IB を `SetGeometry` で基底に預け、 `Draw` / `IsValid` 等は基底実装を共有する
/// skinning する派生は `Draw` を override して bone palette CB の bind を足す
/// @pre Renderer の DeviceContext を内部保持するため Renderer より先に破棄すること

#include <cstddef>
#include <memory>

struct ID3D11Buffer;

namespace NS::Graphics
{
    class Renderer;
    class VertexBuffer;
    class IndexBuffer;
    class Mesh;

    namespace detail
    {
        /// Material / Renderer 拡張から ID3D11Buffer に直接アクセスするための typed friend accessor
        [[nodiscard]] ID3D11Buffer* GetVertexBuffer(Mesh& mesh) noexcept;
        [[nodiscard]] ID3D11Buffer* GetIndexBuffer(Mesh& mesh) noexcept;
    } // namespace detail

    /// 描画できるジオメトリの基底。 VB / IB を所有し DrawIndexed を 1 回発行する
    /// 頂点フォーマットと入力レイアウトは派生 (StaticMesh / SkeletalMesh) が持つ
    class Mesh
    {
    public:
        virtual ~Mesh();

        Mesh(const Mesh&) = delete;
        Mesh& operator=(const Mesh&) = delete;
        Mesh(Mesh&&) = delete;
        Mesh& operator=(Mesh&&) = delete;

        /// VB / IB が構築済なら true。 fallback geometry に切替わった場合も true (描画は可能)
        /// Device / Context 無効や geometry 未設定なら false
        [[nodiscard]] bool IsValid() const noexcept;

        /// 構築失敗で fallback geometry に切替わっているか。 デバッグ時のジオメトリ欠落検知に使う
        [[nodiscard]] bool IsUsingFallback() const noexcept;

        [[nodiscard]] std::size_t VertexCount() const noexcept;
        [[nodiscard]] std::size_t IndexCount() const noexcept;

        /// VB.Bind(0) + IB.Bind() + IASetPrimitiveTopology(TRIANGLELIST) + DrawIndexed を一括発行する
        /// Shader / Material 側の Bind は呼出側 (MeshRendererComponent) 責任
        /// skinning する派生は override して bone palette CB の bind を足す
        virtual void Draw() noexcept;

    protected:
        Mesh();

        /// 派生が構築した VB / IB と頂点 / index 数を基底に預ける
        /// renderer の DeviceContext を内部保持する。 vb / ib のいずれかが null なら invalid 扱い
        /// `usingFallback` は fallback geometry に切替えた場合に true を渡す
        void SetGeometry(Renderer& renderer,
                         std::unique_ptr<VertexBuffer> vertexBuffer,
                         std::unique_ptr<IndexBuffer> indexBuffer,
                         std::size_t vertexCount,
                         std::size_t indexCount,
                         bool usingFallback) noexcept;

    private:
        struct Impl;
        std::unique_ptr<Impl> m_pImpl;

        friend ID3D11Buffer* detail::GetVertexBuffer(Mesh& mesh) noexcept;
        friend ID3D11Buffer* detail::GetIndexBuffer(Mesh& mesh) noexcept;
    };

} // namespace NS::Graphics
