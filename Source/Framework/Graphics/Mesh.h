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

#include <Framework/Core/NonCopyable.h>
#include <Framework/Graphics/D3dCommon.h>

namespace NS::Graphics
{
    class Renderer;
    class Buffer;
    class Shader;
    class Mesh;

    namespace detail
    {
        /// Material / Renderer 拡張から Buffer / InputLayout に直接アクセスするための typed friend accessor
        [[nodiscard]] const Buffer* GetVertexBuffer(Mesh& mesh) noexcept;
        [[nodiscard]] const Buffer* GetIndexBuffer(Mesh& mesh) noexcept;
        [[nodiscard]] ID3D11InputLayout* GetInputLayout(Mesh& mesh) noexcept;
    } // namespace detail

    /// 公開 InputElement 用フォーマット。D3D11 / DXGI を漏らさない独自 enum
    enum class InputElementFormat
    {
        Float2, ///< R32G32_FLOAT
        Float3, ///< R32G32B32_FLOAT
        Float4, ///< R32G32B32A32_FLOAT
        UInt32, ///< R32_UINT
        UInt4,  ///< R32G32B32A32_UINT (4 ボーン index)
    };

    /// InputLayout の 1 要素。SemanticIndex は常に 0、InputSlot 0 単一 stream 前提
    /// StaticMesh::StandardInputLayout() / SkeletalMesh::SkinnedInputLayout() が offsetof で byteOffset を埋める
    struct InputElement
    {
        std::string semanticName;
        InputElementFormat format = InputElementFormat::Float3;
        unsigned byteOffset = 0;
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

        /// 自分の頂点レイアウトと頂点 Shader の VS バイトコードから ID3D11InputLayout を生成する
        /// 生成済 / device 無効 / layout 未設定 / VS バイトコード空 (頂点 Shader でない等) なら no-op (冪等)
        /// 直接描画される mesh に対し描画前に 1 度呼ぶ (MeshRendererComponent が Material 経由で呼ぶ)
        void CreateInputLayout(const Shader& vertexShader) noexcept;

        /// InputLayout 生成済なら IASetInputLayout、 renderer 経由で VB(0) + IB の bind +
        /// IASetPrimitiveTopology(TRIANGLELIST) + DrawIndexed を一括発行する
        /// Shader / Material 側の Bind は呼出側 (MeshRendererComponent) 責任
        /// skinning する派生は override して bone palette CB の bind を足す
        virtual void Draw(Renderer& renderer) noexcept;

    protected:
        Mesh();

        /// 派生が構築した VB / IB と頂点 / index 数を基底に預ける
        /// InputLayout 生成用にグローバル Device を内部保持する。 vb / ib のいずれかが null なら invalid 扱い
        /// `usingFallback` は fallback geometry に切替えた場合に true を渡す
        void SetGeometry(std::unique_ptr<Buffer> vertexBuffer,
                         std::unique_ptr<Buffer> indexBuffer,
                         std::size_t vertexCount,
                         std::size_t indexCount,
                         bool usingFallback) noexcept;

        /// 派生が自分の頂点フォーマットの InputElement 配列を基底に渡す (CreateInputLayout が使う)
        void SetVertexLayout(std::vector<InputElement> elements) noexcept;

    private:
        std::unique_ptr<Buffer> m_vb;
        std::unique_ptr<Buffer> m_ib;
        ComPtr<ID3D11Device> m_device;
        ComPtr<ID3D11InputLayout> m_inputLayout;
        std::vector<InputElement> m_layoutElements;
        std::size_t m_vertexCount = 0;
        std::size_t m_indexCount = 0;
        bool m_valid = false;
        bool m_usingFallback = false;

        friend const Buffer* detail::GetVertexBuffer(Mesh& mesh) noexcept;
        friend const Buffer* detail::GetIndexBuffer(Mesh& mesh) noexcept;
        friend ID3D11InputLayout* detail::GetInputLayout(Mesh& mesh) noexcept;
    };

} // namespace NS::Graphics
