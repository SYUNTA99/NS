#include "NSlib/Gfx/DebugDraw.h"
#include "NSlib/Core/Types.h"

#include "NSlib/Core/AABB.h"
#include "NSlib/Core/Logger.h"
#include "NSlib/Core/OBB.h"
#include "NSlib/Core/Sphere.h"
#include "NSlib/Gfx/D3DBuffer.h"
#include "NSlib/Gfx/D3DCommandList.h"
#include "NSlib/Gfx/D3dCommon.h"
#include "NSlib/Gfx/GraphicObject.h"
#include "NSlib/Gfx/D3DMesh.h"
#include "NSlib/Gfx/D3DPipeline.h"
#include "NSlib/Gfx/Renderer.h"
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
    NS::Gfx::DebugShapes& StepShapes() noexcept
    {
        static NS::Gfx::DebugShapes g_stepShapes;
        return g_stepShapes;
    }

    // 描画用のリソース一式
    struct LineBackend
    {
        NS::Gfx::ComPtr<ID3D11InputLayout> inputLayout;
        std::unique_ptr<NS::Gfx::D3DBuffer> vb;
        std::unique_ptr<NS::Gfx::D3DBuffer> faceVb;
        std::unique_ptr<NS::Gfx::D3DBuffer> cb;
        // 面は奥の側も見せるので裏表とも描く。深度を見ないので、物の表面と重なった面もちらつかずに透ける
        std::unique_ptr<NS::Gfx::D3DPipeline> facePipeline;
        bool initAttempted = false;
        bool valid = false;
    };

    LineBackend& Backend() noexcept
    {
        static LineBackend backend;
        return backend;
    }

    // 入力レイアウトは初回の vs のバイトコードで作り、読み直しても作り直さない
    // debug_line.vs.hlsl の入力の並びを変えた時は、起動し直すまで合わない
    bool EnsureBackend(const NS::Gfx::D3DShader& vs, const NS::Gfx::D3DShader& ps) noexcept
    {
        LineBackend& b = Backend();
        if (b.initAttempted)
        {
            return b.valid;
        }
        b.initAttempted = true;

        ID3D11Device* device = NS::Gfx::Gpu().device;
        if (device == nullptr)
        {
            NS_LOG_ERROR(Graphics, "DebugDraw: グローバル Device が無効");
            return false;
        }

        if (!vs.IsValid() || !ps.IsValid())
        {
            NS_LOG_ERROR(Graphics, "DebugDraw: shader 構築失敗");
            return false;
        }

        const std::span<const std::byte> bytecode = vs.VertexShaderBytecode();
        const D3D11_INPUT_ELEMENT_DESC layout[] = {
            {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
        };
        const HRESULT hr =
            device->CreateInputLayout(layout, 2u, bytecode.data(), bytecode.size(), b.inputLayout.GetAddressOf());
        if (FAILED(hr))
        {
            NS_LOG_ERROR(Graphics, "DebugDraw: CreateInputLayout 失敗 (hr=0x{:08X})", static_cast<NS::uint>(hr));
            return false;
        }

        b.vb = NS::Gfx::D3DBuffer::Create(
            NS::Gfx::MakeVertexBufferDesc(nullptr, k_MaxVertices, k_VertexStride, D3D11_USAGE_DYNAMIC));
        b.faceVb = NS::Gfx::D3DBuffer::Create(
            NS::Gfx::MakeVertexBufferDesc(nullptr, k_MaxFaceVertices, k_VertexStride, D3D11_USAGE_DYNAMIC));
        b.cb = NS::Gfx::D3DBuffer::Create(NS::Gfx::MakeConstantBufferDesc(sizeof(NS::Matrix)));
        if (!b.vb->IsValid() || !b.faceVb->IsValid() || !b.cb->IsValid())
        {
            NS_LOG_ERROR(Graphics, "DebugDraw: VB / CB 構築失敗");
            return false;
        }

        b.facePipeline = NS::Gfx::D3DPipeline::Create(NS::Gfx::D3DPipelineDesc{.cull = NS::Gfx::CullMode::None,
                                                                         .blend = NS::Gfx::BlendMode::Alpha,
                                                                         .depth = NS::Gfx::DepthMode::Disabled});
        if (!b.facePipeline->IsValid())
        {
            NS_LOG_ERROR(Graphics, "DebugDraw: 面の D3DPipeline 構築失敗");
            return false;
        }

        b.valid = true;
        return true;
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

        void DebugShapes::Draw(Renderer& renderer, const Matrix& viewProjection) const noexcept
        {
            static_assert(sizeof(Vertex) == k_VertexStride, "Vertex は POSITION(12) + COLOR(16) の 28 byte 前提");

            if (m_lines.empty() && m_faces.empty())
            {
                return;
            }

            // シェーダーは AssetManager が捨てると消えるので、静的な資源に控えず毎回 Renderer から引く
            const BuiltinShaders& shaders = renderer.Shaders();
            if (shaders.debugLineVs == nullptr || shaders.debugLinePs == nullptr)
            {
                return;
            }
            if (!EnsureBackend(*shaders.debugLineVs, *shaders.debugLinePs))
            {
                return;
            }

            D3DCommandList& cmd = renderer.Commands();
            if (cmd.Native() == nullptr)
            {
                return;
            }

            LineBackend& b = Backend();
            cmd.UpdateSubresource(*b.cb, &viewProjection, sizeof(viewProjection));
            cmd.VSSetShader(*shaders.debugLineVs);
            cmd.PSSetShader(*shaders.debugLinePs);
            cmd.SetInputLayout(b.inputLayout.Get());
            cmd.VSSetConstantBuffer(*b.cb, 0u);

            // 面を先に描く。線は深度を書くので、後に描かないと面が線の奥で途切れる
            if (!m_faces.empty())
            {
                cmd.UpdateSubresource(*b.faceVb, m_faces.data(), m_faces.size() * sizeof(Vertex));
                cmd.SetPipeline(*b.facePipeline);
                cmd.SetVertexBuffer(*b.faceVb, 0u);
                cmd.SetTopology(Topology::TriangleList);
                cmd.Draw(static_cast<uint>(m_faces.size()));
            }

            // 溜めた頂点データを一括で描画する
            if (!m_lines.empty())
            {
                cmd.UpdateSubresource(*b.vb, m_lines.data(), m_lines.size() * sizeof(Vertex));
                cmd.SetPipeline(renderer.CommonPipeline(BlendMode::Opaque));
                cmd.SetVertexBuffer(*b.vb, 0u);
                cmd.SetTopology(Topology::LineList);
                cmd.Draw(static_cast<uint>(m_lines.size()));
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
                StepShapes().Line(a, b, color);
            }

            void AABB(const NS::AABB& box, const Color& color) noexcept
            {
                StepShapes().AABB(box, color);
            }

            void OBB(const NS::OBB& obb, const Color& color) noexcept
            {
                StepShapes().OBB(obb, color);
            }

            void Sphere(const NS::Sphere& sphere, const Color& color) noexcept
            {
                StepShapes().Sphere(sphere, color);
            }

            void Circle(const Vector3& center,
                        const Vector3& u,
                        const Vector3& v,
                        const Color& color) noexcept
            {
                StepShapes().Circle(center, u, v, color);
            }

            void Capsule(const Vector3& base,
                         const Vector3& axis,
                         float radius,
                         const Color& color) noexcept
            {
                StepShapes().Capsule(base, axis, radius, color);
            }

            void Draw(Renderer& renderer, const Matrix& viewProjection) noexcept
            {
                StepShapes().Draw(renderer, viewProjection);
            }

            void BeginStep() noexcept
            {
                StepShapes().Clear();
            }

            void Clear() noexcept
            {
                StepShapes().Clear();
            }

            std::size_t VertexCount() noexcept
            {
                return StepShapes().VertexCount();
            }

            void Triangle(const Vector3& a,
                          const Vector3& b,
                          const Vector3& c,
                          const Color& color) noexcept
            {
                StepShapes().Triangle(a, b, c, color);
            }

            std::size_t FaceVertexCount() noexcept
            {
                return StepShapes().FaceVertexCount();
            }
        } // namespace DebugDraw
    } // namespace Gfx
} // namespace NS
