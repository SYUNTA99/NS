#pragma once

#include "NSlib/Core/AABB.h"
#include "NSlib/Core/Math.h"
#include "NSlib/Core/NonCopyable.h"
#include "NSlib/Gfx/D3dCommon.h"

#include "NSlib/Core/Types.h"
#include <memory>
#include <string>
#include <vector>

namespace NS::Gfx
{
        class D3DBuffer;
        class D3DShader;
        class D3DCommandList;

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
            uint byteOffset = 0;
        };

        //! インデックスバッファの解釈方法を表すプリミティブ形状
        enum class Topology
        {
            TriangleList,
            LineList
        };

        //! 頂点とインデックスの GPU データ。描画は DrawMesh が行う
        class D3DMesh : public NonCopyable
        {
        public:
            virtual ~D3DMesh();

            //! 頂点バッファとインデックスバッファが揃っているか
            [[nodiscard]] bool IsValid() const noexcept;

            //! 構築失敗でフォールバックジオメトリに切替わっているか
            [[nodiscard]] bool IsUsingFallback() const noexcept;

            [[nodiscard]] std::size_t VertexCount() const noexcept;
            [[nodiscard]] std::size_t IndexCount() const noexcept;

            //! @brief モデル空間の軸並行境界ボックス。カリングで世界変換して視錐台に掛ける
            //! @details 派生が build 時に頂点から算出して登録する。未登録なら原点の単位ボックス
            [[nodiscard]] const AABB& LocalBounds() const noexcept;

            //! @brief 頂点シェーダーに合う入力レイアウトを選ぶ。無ければ作る
            //! @details レイアウトは頂点シェーダーごとに持ち、そのシェーダーの BytecodeId が作った時と違えば作り直す。
            //! 描く度に呼べば、読み直しも、入力の違う別の頂点シェーダーで描く事も、選んだレイアウトに届く。
            //! 作れなかった時は空を選び、番号が変わるまで作り直さない
            //! @note グラフィックスデバイスが無効な場合は何もしない
            void CreateInputLayout(const D3DShader& vertexShader) noexcept;

            [[nodiscard]] const D3DBuffer* VertexBuffer() const noexcept;
            [[nodiscard]] const D3DBuffer* IndexBuffer() const noexcept;
            //! 直前の CreateInputLayout で選んだレイアウト
            [[nodiscard]] ID3D11InputLayout* InputLayout() const noexcept;
            [[nodiscard]] Topology GetTopology() const noexcept;

        protected:
            D3DMesh();

            //! 成功時だけ両方の GPU バッファを登録する。失敗時は変更せず false
            //! stride は頂点 1 個のバイト数、usingFallback は代替形状かを示す
            [[nodiscard]] bool BuildGeometry(const void* vertices,
                                             std::size_t vertexCount,
                                             std::size_t stride,
                                             const uint32* indices,
                                             std::size_t indexCount,
                                             bool usingFallback) noexcept;

            void SetVertexLayout(std::vector<InputElement> elements) noexcept;

            void SetTopology(Topology topology) noexcept;

            //! @brief 派生クラスが頂点から算出したモデル空間境界ボックスを登録する
            void SetLocalBounds(const AABB& bounds) noexcept;

        private:
            // 頂点シェーダー 1 つに作ったレイアウト。shader は見分けるだけで、指す先を読まない
            struct ShaderInputLayout
            {
                const D3DShader* shader = nullptr;
                uint64 bytecodeId = 0;
                ComPtr<ID3D11InputLayout> layout;
            };

            std::unique_ptr<D3DBuffer> m_vb;
            std::unique_ptr<D3DBuffer> m_ib;
            std::vector<ShaderInputLayout> m_inputLayouts;
            ID3D11InputLayout* m_selectedLayout = nullptr; // m_inputLayouts の中の 1 つ
            std::vector<InputElement> m_layoutElements; // 派生クラスから登録されたレイアウト記述
            Topology m_topology = Topology::TriangleList;
            std::size_t m_vertexCount = 0;
            std::size_t m_indexCount = 0;
            AABB m_localBounds{}; // モデル空間の境界。派生が build 時に頂点から満たす
            bool m_valid = false;
            bool m_usingFallback = false;
        };

        //! D3DMesh のデータを読んで bind + DrawIndexed を発行する。D3DShader/D3DMaterial の Bind は呼出側責任
        void DrawMesh(D3DCommandList& commands, const D3DMesh& mesh) noexcept;
} // namespace NS::Gfx
