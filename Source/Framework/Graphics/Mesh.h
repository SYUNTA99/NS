#pragma once

/// @file Mesh.h
/// @brief NS::Graphics::Mesh — 描画できるジオメトリの基底 (VB / IB / InputLayout を所有し DrawIndexed を発行する)
///
/// @details StaticMesh / SkeletalMesh の共通実体 = GPU 頂点 / index buffer と入力レイアウト、 描画呼出を持つ
/// 頂点フォーマットは派生が決める (StaticMesh は StaticVertex、 SkeletalMesh は SkinnedVertex)
/// 派生は構築した VB / IB を `SetGeometry` で基底に預け、 自分の頂点レイアウトを `SetVertexLayout` で渡す
/// 入力レイアウトは `CreateInputLayout(shader)` で Shader の VS バイトコードから生成する (VS 入力シグネチャ突合に必要)
/// `Draw` / `IsValid` 等は基底実装を共有する。 skinning する派生は `Draw` を override して bone palette CB の bind
/// を足す
/// @details GPU バインドは `Draw(Renderer&)` に渡す Renderer 経由で行い、 Mesh は DeviceContext を保持しない
/// (InputLayout 生成用に Device のみ保持する)

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include "Framework/Core/NonCopyable.h"
#include "Framework/Graphics/D3dCommon.h"

namespace NS::Graphics
{
    class Renderer;
    class Buffer;
    class Shader;
    class Mesh;

    /// 公開 InputElement 用フォーマット。頂点属性として受け付けるフォーマットの閉集合
    enum class InputElementFormat
    {
        /// R32G32_FLOAT
        Float2,
        /// R32G32B32_FLOAT
        Float3,
        /// R32G32B32A32_FLOAT
        Float4,
        /// R32_UINT
        UInt32,
        /// R32G32B32A32_UINT。4 ボーン index 用
        UInt4,
    };

    /// InputLayout の 1 要素。SemanticIndex は常に 0、InputSlot 0 単一 stream 前提
    /// StaticMesh::StandardInputLayout() / SkeletalMesh::SkinnedInputLayout() が offsetof で byteOffset を埋める
    struct InputElement
    {
        std::string semanticName;
        InputElementFormat format = InputElementFormat::Float3;
        unsigned byteOffset = 0;
    };

    /// プリミティブ形状。Mesh の index buffer の解釈方法 (描画元が持つ属性)
    enum class Topology
    {
        TriangleList,
        LineList
    };

    /// 描画できるジオメトリの基底。 VB / IB / InputLayout を所有し DrawIndexed を 1 回発行する
    /// 頂点フォーマットは派生 (StaticMesh / SkeletalMesh) が `SetVertexLayout` で渡す
    class Mesh : public NS::Core::NonCopyable
    {
    public:
        virtual ~Mesh();

        /// VB / IB が構築済なら true。 fallback geometry に切替わった場合も true (描画は可能)
        /// Device / Context 無効や geometry 未設定なら false。 InputLayout 未生成でも true (Draw は layout 無しで発行)
        [[nodiscard]] bool IsValid() const noexcept;

        /// 構築失敗で fallback geometry に切替わっているか。 デバッグ時のジオメトリ欠落検知に使う
        [[nodiscard]] bool IsUsingFallback() const noexcept;

        [[nodiscard]] std::size_t VertexCount() const noexcept;
        [[nodiscard]] std::size_t IndexCount() const noexcept;

        /// VS バイトコードから InputLayout を生成する。生成済・device 無効・layout 未設定なら無操作 (冪等)
        void CreateInputLayout(const Shader& vertexShader) noexcept;

        /// VB + IB + InputLayout を bind して DrawIndexed を発行する。Shader/Material の Bind は呼出側責任
        virtual void Draw(Renderer& renderer) noexcept;

        /// 頂点バッファ (非所有)。InstanceBatcher 等エンジン内部の継ぎ目とテスト向け、未構築なら nullptr
        [[nodiscard]] const Buffer* VertexBuffer() const noexcept;
        /// index バッファ (非所有)。同上
        [[nodiscard]] const Buffer* IndexBuffer() const noexcept;
        /// 生成済 InputLayout (非所有)。CreateInputLayout 前は nullptr
        [[nodiscard]] ID3D11InputLayout* InputLayout() const noexcept;

    protected:
        Mesh();

        /// 派生が構築した VB / IB を基底に預ける。vb / ib のいずれかが null なら invalid 扱い
        void SetGeometry(std::unique_ptr<Buffer> vertexBuffer,
                         std::unique_ptr<Buffer> indexBuffer,
                         std::size_t vertexCount,
                         std::size_t indexCount,
                         bool usingFallback) noexcept;

        /// 派生が自分の頂点フォーマットの InputElement 配列を基底に渡す (CreateInputLayout が使う)
        void SetVertexLayout(std::vector<InputElement> elements) noexcept;

        /// 派生が自分のプリミティブ形状を渡す (既定 TriangleList、line mesh は LineList)
        void SetTopology(Topology topology) noexcept;

    private:
        std::unique_ptr<Buffer> m_vb;
        std::unique_ptr<Buffer> m_ib;
        ComPtr<ID3D11Device> m_device;
        ComPtr<ID3D11InputLayout> m_inputLayout;
        std::vector<InputElement> m_layoutElements;
        Topology m_topology = Topology::TriangleList;
        std::size_t m_vertexCount = 0;
        std::size_t m_indexCount = 0;
        bool m_valid = false;
        bool m_usingFallback = false;
    };

} // namespace NS::Graphics
