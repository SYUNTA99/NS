#pragma once

#include "NSlib/Core/AABB.h"
#include "NSlib/Core/Math.h"
#include "NSlib/Core/NonCopyable.h"
#include "NSlib/Graphics/D3dCommon.h"

#include <cstdint>
#include <memory>
#include <string>

namespace NS::Gfx
{
    class Buffer;
    class Shader;
    class CommandList;

    //! 公開 InputElement 用フォーマット。頂点属性として受け付けるフォーマットの閉集合
    enum class InputElementFormat
    {
        Float2, //!< R32G32_FLOAT
        Float3, //!< R32G32B32_FLOAT
        Float4, //!< R32G32B32A32_FLOAT
        UInt32, //!< R32_UINT
        UInt4,  //!< R32G32B32A32_UINT
    };

    //! 入力レイアウトの要素。セマンティック番号とストリームのスロットは 0 固定
    struct InputElement
    {
        std::string semanticName;
        InputElementFormat format = InputElementFormat::Float3;
        unsigned byteOffset = 0;
    };

    //! インデックスバッファの解釈方法を表すプリミティブ形状
    enum class Topology
    {
        TriangleList,
        LineList
    };

    //! 頂点とインデックスの GPU データ。描画は DrawMesh が行う
    class Mesh : public NS::NonCopyable
    {
    public:
        virtual ~Mesh();

        //! 頂点バッファとインデックスバッファが揃っているか
        [[nodiscard]] bool IsValid() const noexcept;

        //! 構築失敗でフォールバックジオメトリに切替わっているか
        [[nodiscard]] bool IsUsingFallback() const noexcept;

        [[nodiscard]] std::size_t VertexCount() const noexcept;
        [[nodiscard]] std::size_t IndexCount() const noexcept;

        //! @brief モデル空間の軸並行境界ボックス。カリングで世界変換して視錐台に掛ける
        //! @details 派生が build 時に頂点から算出して登録する。未登録なら原点の単位ボックス
        [[nodiscard]] const NS::AABB& LocalBounds() const noexcept;

        //! @brief 頂点シェーダーを利用して入力レイアウトを生成する
        //! @note 生成済みの場合や、グラフィックスデバイスが無効な場合は何もしない
        void CreateInputLayout(const Shader& vertexShader) noexcept;

        [[nodiscard]] const Buffer* VertexBuffer() const noexcept;
        [[nodiscard]] const Buffer* IndexBuffer() const noexcept;
        [[nodiscard]] ID3D11InputLayout* InputLayout() const noexcept;
        [[nodiscard]] Topology GetTopology() const noexcept;

    protected:
        Mesh();

        //! 成功時だけ両方の GPU バッファを登録する。失敗時は変更せず false
        //! stride は頂点 1 個のバイト数、usingFallback は代替形状かを示す
        [[nodiscard]] bool BuildGeometry(const void* vertices,
                                         std::size_t vertexCount,
                                         std::size_t stride,
                                         const std::uint32_t* indices,
                                         std::size_t indexCount,
                                         bool usingFallback) noexcept;

        void SetVertexLayout(std::vector<InputElement> elements) noexcept;

        void SetTopology(Topology topology) noexcept;

        //! @brief 派生クラスが頂点から算出したモデル空間境界ボックスを登録する
        void SetLocalBounds(const NS::AABB& bounds) noexcept;

    private:
        std::unique_ptr<Buffer> m_vb;
        std::unique_ptr<Buffer> m_ib;
        ComPtr<ID3D11InputLayout> m_inputLayout;
        std::vector<InputElement> m_layoutElements; // 派生クラスから登録されたレイアウト記述
        Topology m_topology = Topology::TriangleList;
        std::size_t m_vertexCount = 0;
        std::size_t m_indexCount = 0;
        NS::AABB m_localBounds{}; // モデル空間の境界。派生が build 時に頂点から満たす
        bool m_valid = false;
        bool m_usingFallback = false;
    };

    //! Mesh のデータを読んで bind + DrawIndexed を発行する。Shader/Material の Bind は呼出側責任
    void DrawMesh(CommandList& commands, const Mesh& mesh) noexcept;

} // namespace NS::Gfx
