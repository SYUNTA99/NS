#include "NSlib/Gfx/Render/DebugDraw.h"
#include "NSlib/Core/Types.h"

#include "NSlib/Core/AABB.h"
#include "NSlib/Core/Logger.h"
#include "NSlib/Core/OBB.h"
#include "NSlib/Core/Sphere.h"
#include "NSlib/Gfx/D3DBuffer.h"
#include "NSlib/Gfx/D3DCommandList.h"
#include "NSlib/Gfx/D3dCommon.h"
#include "NSlib/Gfx/D3DGlobal.h"
#include "NSlib/Gfx/D3DMesh.h"
#include "NSlib/Gfx/D3DPipeline.h"
#include "NSlib/Gfx/Render/BuiltinShaders.h"
#include "NSlib/Gfx/D3DDevice.h"
#include "NSlib/Gfx/D3DShader.h"

namespace
{
    constexpr std::size_t k_MaxVertices = 4096;
    constexpr int k_CircleSegments = 12;

    // 頂点バッファの 1 頂点 (POSITION 12 + COLOR 16)。DebugShapes::Vertex と同じ並び
    constexpr std::size_t k_VertexStride = 28;

    // 三角形 12960 枚ぶんの頂点 (約 1 MB)。エディタの面は相手 1 体で 100 枚に満たず、相手の数で溢れない
    constexpr std::size_t k_MaxFaceVertices = 38880;

    // この固定ステップの図形の積み先。自由関数の Line や Circle などは全部ここへ積む
    NS::Gfx::DebugShapes& EditableStepShapes() noexcept
    {
        static NS::Gfx::DebugShapes g_stepShapes;
        return g_stepShapes;
    }
} // namespace

namespace NS
{
    namespace Gfx
    {
        void DebugShapes::Line(const Vector3& a, const Vector3& b, const Color& color)
        {
            if (m_lines.size() + 2 > k_MaxVertices) // 最大容量を超える場合は最も古い線を破棄する
            {
                m_lines.erase(m_lines.begin(), m_lines.begin() + 2);
            }
            m_lines.push_back({a, color});
            m_lines.push_back({b, color});
        }

        void DebugShapes::AABB(const NS::AABB& box, const Color& color)
        {
            NS::OBB obb{};
            obb.center = box.Center;
            obb.halfExtentX = box.Extents.x;
            obb.halfExtentY = box.Extents.y;
            obb.halfExtentZ = box.Extents.z;
            OBB(obb, color);
        }

        void DebugShapes::OBB(const NS::OBB& obb, const Color& color)
        {
            const Vector3 ex = obb.axisX * obb.halfExtentX;
            const Vector3 ey = obb.axisY * obb.halfExtentY;
            const Vector3 ez = obb.axisZ * obb.halfExtentZ;

            const Vector3 c000 = obb.center - ex - ey - ez;
            const Vector3 c100 = obb.center + ex - ey - ez;
            const Vector3 c110 = obb.center + ex + ey - ez;
            const Vector3 c010 = obb.center - ex + ey - ez;
            const Vector3 c001 = obb.center - ex - ey + ez;
            const Vector3 c101 = obb.center + ex - ey + ez;
            const Vector3 c111 = obb.center + ex + ey + ez;
            const Vector3 c011 = obb.center - ex + ey + ez;

            // 前面
            Line(c000, c100, color);
            Line(c100, c110, color);
            Line(c110, c010, color);
            Line(c010, c000, color);

            // 背面
            Line(c001, c101, color);
            Line(c101, c111, color);
            Line(c111, c011, color);
            Line(c011, c001, color);

            // 前面と背面を繋ぐ辺
            Line(c000, c001, color);
            Line(c100, c101, color);
            Line(c110, c111, color);
            Line(c010, c011, color);
        }

        void DebugShapes::Sphere(const NS::Sphere& sphere, const Color& color)
        {
            const Vector3 rx{sphere.radius, 0.0f, 0.0f};
            const Vector3 ry{0.0f, sphere.radius, 0.0f};
            const Vector3 rz{0.0f, 0.0f, sphere.radius};

            Circle(sphere.center, rx, ry, color);
            Circle(sphere.center, ry, rz, color);
            Circle(sphere.center, rz, rx, color);
        }

        // center を中心に u と v が張る平面上の円を積む。u と v は半径ぶん伸ばした直交ベクトルを渡す
        void DebugShapes::Circle(const Vector3& center,
                                 const Vector3& u,
                                 const Vector3& v,
                                 const Color& color)
        {
            constexpr float twoPi = 2.0f * k_Pi;
            Vector3 prev{};
            for (int i = 0; i <= k_CircleSegments; ++i)
            {
                const float t = (static_cast<float>(i) / k_CircleSegments) * twoPi;
                const float ca = std::cos(t);
                const float sa = std::sin(t);
                const Vector3 point{
                    center.x + u.x * ca + v.x * sa, center.y + u.y * ca + v.y * sa, center.z + u.z * ca + v.z * sa};
                if (i > 0)
                {
                    Line(prev, point, color);
                }
                prev = point;
            }
        }

        void DebugShapes::Capsule(const Vector3& base,
                                  const Vector3& axis,
                                  float radius,
                                  const Color& color)
        {
            // 上下端の基準点
            const Vector3 top = base + axis;
            const Vector3 bottom = base - axis;

            // 軸に対して垂直な2つのベクトルを計算する
            Vector3 axisN = axis;
            if (axisN.Length() > 1e-6f)
            {
                axisN.Normalize();
            }

            Vector3 perpA{1.0f, 0.0f, 0.0f};
            if (std::abs(axisN.y) < 0.99f)
            {
                perpA = Vector3{0.0f, 1.0f, 0.0f};
            }

            perpA = perpA.Cross(axisN);
            if (perpA.Length() > 1e-6f)
            {
                perpA.Normalize();
            }
            const Vector3 perpB = axisN.Cross(perpA);

            // 上下端の円
            const Vector3 uA = perpA * radius;
            const Vector3 uB = perpB * radius;
            Circle(top, uA, uB, color);
            Circle(bottom, uA, uB, color);

            // 円柱の側面に沿う線 4 本
            const Vector3 dirs[4] = {uA, -uA, uB, -uB};
            for (const Vector3& d : dirs)
            {
                Line(bottom + d, top + d, color);
            }
        }

        void DebugShapes::Triangle(const Vector3& a,
                                   const Vector3& b,
                                   const Vector3& c,
                                   const Color& color)
        {
            // 線と同じく、上限を超える時は最も古い三角形を捨てる
            if (m_faces.size() + 3 > k_MaxFaceVertices)
            {
                m_faces.erase(m_faces.begin(), m_faces.begin() + 3);
            }
            m_faces.push_back({a, color});
            m_faces.push_back({b, color});
            m_faces.push_back({c, color});
        }

        DebugShapePass::DebugShapePass(const BuiltinShaders& shaders) noexcept
            : m_vs(shaders.debugLineVs), m_ps(shaders.debugLinePs)
        {
            m_lineVb = D3DBuffer::Create(MakeVertexBufferDesc(nullptr, k_MaxVertices, k_VertexStride, D3D11_USAGE_DYNAMIC));
            m_faceVb =
                D3DBuffer::Create(MakeVertexBufferDesc(nullptr, k_MaxFaceVertices, k_VertexStride, D3D11_USAGE_DYNAMIC));
            m_cb = D3DBuffer::Create(MakeConstantBufferDesc(sizeof(Matrix)));
            if (!m_lineVb->IsValid() || !m_faceVb->IsValid() || !m_cb->IsValid())
            {
                NS_LOG_ERROR(Graphics, "DebugShapePass: VB / CB 構築失敗");
                return;
            }

            m_facePipeline = D3DPipeline::Create(
                D3DPipelineDesc{.cull = CullMode::None, .blend = BlendMode::Alpha, .depth = DepthMode::Disabled});
            if (!m_facePipeline->IsValid())
            {
                NS_LOG_ERROR(Graphics, "DebugShapePass: 面の D3DPipeline 構築失敗");
                return;
            }
            m_valid = true;
        }

        DebugShapePass::~DebugShapePass() = default;

        void DebugShapePass::SelectInputLayout(const D3DShader& vertexShader) noexcept
        {
            if (m_layoutBytecodeId == vertexShader.BytecodeId())
            {
                return;
            }
            m_layoutBytecodeId = vertexShader.BytecodeId();
            m_inputLayout.Reset();

            ID3D11Device* d3d11Device = Gpu().device;
            const std::span<const std::byte> bytecode = vertexShader.VertexShaderBytecode();
            if (d3d11Device == nullptr || bytecode.empty())
            {
                NS_LOG_ERROR(Graphics, "DebugShapePass: グローバル Device が無効か、VS バイトコードが空");
                return;
            }
            const D3D11_INPUT_ELEMENT_DESC layout[] = {
                {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
                {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
            };
            const HRESULT hr =
                d3d11Device->CreateInputLayout(layout, 2u, bytecode.data(), bytecode.size(), m_inputLayout.GetAddressOf());
            if (FAILED(hr))
            {
                NS_LOG_ERROR(Graphics, "DebugShapePass: CreateInputLayout 失敗 (hr=0x{:08X})", static_cast<uint>(hr));
                m_inputLayout.Reset();
            }
        }

        void DebugShapePass::Draw(D3DDevice& device, const DebugShapes& shapes, const Matrix& viewProjection) noexcept
        {
            static_assert(sizeof(DebugShapes::Vertex) == k_VertexStride,
                          "Vertex は POSITION(12) + COLOR(16) の 28 byte 前提");

            if (shapes.m_lines.empty() && shapes.m_faces.empty())
            {
                return;
            }
            if (!m_valid || !IsUsable(m_vs) || !IsUsable(m_ps))
            {
                return;
            }
            D3DCommandList& cmd = device.Commands();
            if (cmd.Native() == nullptr)
            {
                return;
            }
            SelectInputLayout(*m_vs);
            if (m_inputLayout == nullptr)
            {
                return;
            }

            cmd.UpdateSubresource(*m_cb, &viewProjection, sizeof(viewProjection));
            cmd.VSSetShader(*m_vs);
            cmd.PSSetShader(*m_ps);
            cmd.SetInputLayout(m_inputLayout.Get());
            cmd.VSSetConstantBuffer(*m_cb, 0u);

            // 面を先に描く。線は深度を書くので、後に描かないと面が線の奥で途切れる
            if (!shapes.m_faces.empty())
            {
                cmd.UpdateSubresource(*m_faceVb, shapes.m_faces.data(), shapes.m_faces.size() * sizeof(DebugShapes::Vertex));
                cmd.SetPipeline(*m_facePipeline);
                cmd.SetVertexBuffer(*m_faceVb, 0u);
                cmd.SetTopology(Topology::TriangleList);
                cmd.Draw(static_cast<uint>(shapes.m_faces.size()));
            }

            if (!shapes.m_lines.empty())
            {
                cmd.UpdateSubresource(*m_lineVb, shapes.m_lines.data(), shapes.m_lines.size() * sizeof(DebugShapes::Vertex));
                cmd.SetPipeline(device.CommonPipeline(BlendMode::Opaque));
                cmd.SetVertexBuffer(*m_lineVb, 0u);
                cmd.SetTopology(Topology::LineList);
                cmd.Draw(static_cast<uint>(shapes.m_lines.size()));
            }
        }

        void DebugShapes::Clear() noexcept
        {
            m_lines.clear();
            m_faces.clear();
        }
    } // namespace Gfx
} // namespace NS

namespace NS
{
    namespace Gfx
    {
        namespace DebugDraw
        {
            void Line(const Vector3& a, const Vector3& b, const Color& color) noexcept
            {
                EditableStepShapes().Line(a, b, color);
            }

            void AABB(const NS::AABB& box, const Color& color) noexcept
            {
                EditableStepShapes().AABB(box, color);
            }

            void OBB(const NS::OBB& obb, const Color& color) noexcept
            {
                EditableStepShapes().OBB(obb, color);
            }

            void Sphere(const NS::Sphere& sphere, const Color& color) noexcept
            {
                EditableStepShapes().Sphere(sphere, color);
            }

            void Circle(const Vector3& center,
                        const Vector3& u,
                        const Vector3& v,
                        const Color& color) noexcept
            {
                EditableStepShapes().Circle(center, u, v, color);
            }

            void Capsule(const Vector3& base,
                         const Vector3& axis,
                         float radius,
                         const Color& color) noexcept
            {
                EditableStepShapes().Capsule(base, axis, radius, color);
            }

            const DebugShapes& StepShapes() noexcept
            {
                return EditableStepShapes();
            }

            void BeginStep() noexcept
            {
                EditableStepShapes().Clear();
            }

            void Clear() noexcept
            {
                EditableStepShapes().Clear();
            }

            std::size_t VertexCount() noexcept
            {
                return EditableStepShapes().VertexCount();
            }

            void Triangle(const Vector3& a,
                          const Vector3& b,
                          const Vector3& c,
                          const Color& color) noexcept
            {
                EditableStepShapes().Triangle(a, b, c, color);
            }

            std::size_t FaceVertexCount() noexcept
            {
                return EditableStepShapes().FaceVertexCount();
            }
        } // namespace DebugDraw
    } // namespace Gfx
} // namespace NS
