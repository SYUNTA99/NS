#pragma once

#include "NSlib/Core/AABB.h"
#include "NSlib/Core/Math.h"
#include "NSlib/Core/NonCopyable.h"
#include "NSlib/Core/OBB.h"
#include "NSlib/Core/Sphere.h"
#include "NSlib/Core/Types.h"
#include "NSlib/Gfx/D3dCommon.h"

#include <cstddef>
#include <memory>
#include <vector>

namespace NS
{
    namespace Gfx
    {
        class D3DBuffer;
        class D3DDevice;
        class D3DPipeline;
        class D3DShader;
        struct BuiltinShaders;

        //! 描画後も保持されるデバッグ図形バッファ。Clear で破棄し、上限 (線 4096 頂点・面 12960 三角形) 超過時は古い要素から破棄する
        class DebugShapes
        {
        public:
            void Line(const Vector3& a, const Vector3& b, const Color& color);

            void AABB(const NS::AABB& box, const Color& color);

            void OBB(const NS::OBB& obb, const Color& color);

            void Sphere(const NS::Sphere& sphere, const Color& color);

            //! center を中心に、直交する半径ベクトル u, v が張る平面上の円を追加する
            void Circle(const Vector3& center, const Vector3& u, const Vector3& v, const Color& color);

            //! カプセルの円柱部分の枠線を追加する (両端の半球弧は描かない)。base は中心、axis は中心から半球中心へのベクトル
            void Capsule(const Vector3& base, const Vector3& axis, float radius, const Color& color);

            //! 線より先に両面・深度テストなしで描画する半透明三角形を追加する
            void Triangle(const Vector3& a, const Vector3& b, const Vector3& c, const Color& color);

            void Clear() noexcept;

            [[nodiscard]] std::size_t VertexCount() const noexcept { return m_lines.size(); }

            [[nodiscard]] std::size_t FaceVertexCount() const noexcept { return m_faces.size(); }

        private:
            friend class DebugShapePass;

            struct Vertex
            {
                Vector3 position;
                Color color;
            };

            std::vector<Vertex> m_lines;
            std::vector<Vertex> m_faces;
        };

        //! DebugShapes を描画するパス。D3DDevice より先に破棄し、shaders の破棄後は描画を呼ばないこと
        class DebugShapePass : public NonCopyable
        {
        public:
            //! shaders は非所有で参照のみ保持する
            explicit DebugShapePass(const BuiltinShaders& shaders) noexcept;
            ~DebugShapePass();

            [[nodiscard]] bool IsValid() const noexcept { return m_valid; }

            //! 面を先に、線を後に描画する。shapes の内容は消去しない
            void Draw(D3DDevice& device, const DebugShapes& shapes, const Matrix& viewProjection) noexcept;

        private:
            void SelectInputLayout(const D3DShader& vertexShader) noexcept;

            D3DShader* m_vs = nullptr;
            D3DShader* m_ps = nullptr;
            std::unique_ptr<D3DBuffer> m_lineVb;
            std::unique_ptr<D3DBuffer> m_faceVb;
            std::unique_ptr<D3DBuffer> m_cb;
            std::unique_ptr<D3DPipeline> m_facePipeline;
            ComPtr<ID3D11InputLayout> m_inputLayout;
            uint64 m_layoutBytecodeId = 0;
            bool m_valid = false;
        };
    } // namespace Gfx
} // namespace NS

namespace NS
{
    namespace Gfx
    {
        //! 固定ステップ単位の共有 DebugShapes へ図形を追加する関数群
        namespace DebugDraw
        {
            void Line(const Vector3& a, const Vector3& b, const Color& color) noexcept;

            void AABB(const NS::AABB& box, const Color& color) noexcept;

            void OBB(const NS::OBB& obb, const Color& color) noexcept;

            void Sphere(const NS::Sphere& sphere, const Color& color) noexcept;

            //! center を中心に、直交する半径ベクトル u, v が張る平面上の円を追加する
            void Circle(const Vector3& center,
                        const Vector3& u,
                        const Vector3& v,
                        const Color& color) noexcept;

            //! カプセルの円柱部分の枠線を追加する (両端の半球弧は描かない)。base は中心、axis は中心から半球中心へのベクトル
            void Capsule(const Vector3& base,
                         const Vector3& axis,
                         float radius,
                         const Color& color) noexcept;

            //! 前の固定ステップで蓄積した図形を消去する。固定ステップの先頭で呼ぶ
            void BeginStep() noexcept;

            //! 現在の固定ステップで蓄積された図形を返す
            [[nodiscard]] const DebugShapes& StepShapes() noexcept;

            void Clear() noexcept;

            [[nodiscard]] std::size_t VertexCount() noexcept;

            //! 線より先に両面・深度テストなしで描画する半透明三角形を追加する
            void Triangle(const Vector3& a,
                          const Vector3& b,
                          const Vector3& c,
                          const Color& color) noexcept;

            [[nodiscard]] std::size_t FaceVertexCount() noexcept;
        } // namespace DebugDraw
    } // namespace Gfx
} // namespace NS
