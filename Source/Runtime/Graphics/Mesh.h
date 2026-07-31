#pragma once

#include "Runtime/Core/NonCopyable.h"
#include "Runtime/Graphics/D3dCommon.h"
#include "Runtime/Math/Math.h"

#include <memory>
#include <string>

namespace NS::Graphics
{
    class Buffer;
    class Shader;
    class Mesh;
    class CommandList;

    /// 公開 InputElement 用フォーマット。頂点属性として受け付けるフォーマットの閉集合
    enum class InputElementFormat
    {
        Float2, //!< R32G32_FLOAT
        Float3, //!< R32G32B32_FLOAT
        Float4, //!< R32G32B32A32_FLOAT
        UInt32, //!< R32_UINT
        UInt4,  //!< R32G32B32A32_UINT
    };

    /// @brief 入力レイアウトの1要素。
    /// @details セマンティックインデックスは常に0とし、単一の入力ストリーム（スロット0）を前提とする。
    /// オフセット値は派生クラス側で構造体のメモリ配置から算出して設定する。
    struct InputElement
    {
        std::string semanticName;
        InputElementFormat format = InputElementFormat::Float3;
        unsigned byteOffset = 0;
    };

    /// インデックスバッファの解釈方法を表すプリミティブ形状
    enum class Topology
    {
        TriangleList,
        LineList
    };

    /// @brief 描画可能なジオメトリを表すデータの基底クラス
    /// @details 頂点バッファ、インデックスバッファ、入力レイアウトを管理する。描画発行は自身では行わず、
    /// 公開したデータを `DrawMesh` が読んでバインドと DrawIndexed を行う
    /// 頂点フォーマットの決定やバッファの構築は派生クラス側で行う
    class Mesh : public NS::Core::NonCopyable
    {
    public:
        virtual ~Mesh();

        [[nodiscard]] bool IsValid() const noexcept;

        /// 構築失敗でフォールバックジオメトリに切替わっているか
        [[nodiscard]] bool IsUsingFallback() const noexcept;

        [[nodiscard]] std::size_t VertexCount() const noexcept;
        [[nodiscard]] std::size_t IndexCount() const noexcept;

        /// @brief モデル空間の軸並行境界ボックス。カリングで world 変換して視錐台に掛ける
        /// @details 派生が build 時に頂点から算出して登録する。未登録なら原点の単位ボックス
        [[nodiscard]] const NS::Math::AABB& LocalBounds() const noexcept;

        /// @brief 頂点シェーダーを利用して入力レイアウトを生成する
        /// @param vertexShader 入力レイアウトの生成元となる頂点シェーダー
        /// @note 生成済みの場合や、グラフィックスデバイスが無効な場合は何もしない
        void CreateInputLayout(const Shader& vertexShader) noexcept;

        /// 頂点バッファを返す
        [[nodiscard]] const Buffer* VertexBuffer() const noexcept;
        /// インデックスバッファを返す
        [[nodiscard]] const Buffer* IndexBuffer() const noexcept;
        /// 入力レイアウトを返す
        [[nodiscard]] ID3D11InputLayout* InputLayout() const noexcept;
        /// プリミティブ形状を返す
        [[nodiscard]] Topology GetTopology() const noexcept;

    protected:
        Mesh();

        /// @brief 派生クラスで構築したバッファを基底クラスに登録する
        /// @param vertexBuffer 登録する頂点バッファ
        /// @param indexBuffer 登録するインデックスバッファ
        /// @param vertexCount 頂点数
        /// @param indexCount インデックス数
        /// @param usingFallback 代替ジオメトリとして構築されたかどうかのフラグ
        /// @note どちらかのバッファが未割り当ての場合は無効な状態として扱われる
        void SetGeometry(std::unique_ptr<Buffer> vertexBuffer,
                         std::unique_ptr<Buffer> indexBuffer,
                         std::size_t vertexCount,
                         std::size_t indexCount,
                         bool usingFallback) noexcept;

        /// @brief 派生クラスで定義した頂点フォーマットのレイアウト要素を登録する
        /// @param elements 登録する入力レイアウトの要素配列
        void SetVertexLayout(std::vector<InputElement> elements) noexcept;

        /// @brief 派生クラスで定義したプリミティブ形状を登録する
        /// @param topology 登録するプリミティブ形状
        void SetTopology(Topology topology) noexcept;

        /// @brief 派生クラスが頂点から算出したモデル空間境界ボックスを登録する
        void SetLocalBounds(const NS::Math::AABB& bounds) noexcept;

    private:
        std::unique_ptr<Buffer> m_vb;
        std::unique_ptr<Buffer> m_ib;
        ComPtr<ID3D11InputLayout> m_inputLayout;
        std::vector<InputElement> m_layoutElements; // 派生クラスから登録されたレイアウト記述
        Topology m_topology = Topology::TriangleList;
        std::size_t m_vertexCount = 0;
        std::size_t m_indexCount = 0;
        NS::Math::AABB m_localBounds{}; // モデル空間の境界。派生が build 時に頂点から満たす
        bool m_valid = false;
        bool m_usingFallback = false;
    };

    /// Mesh のデータを読んで bind + DrawIndexed を発行する。Shader/Material の Bind は呼出側責任
    void DrawMesh(CommandList& commands, const Mesh& mesh) noexcept;

} // namespace NS::Graphics
