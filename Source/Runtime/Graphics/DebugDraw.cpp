#include "Runtime/Graphics/DebugDraw.h"

#include "Runtime/Core/AABB.h"
#include "Runtime/Core/Logger.h"
#include "Runtime/Core/OBB.h"
#include "Runtime/Core/Sphere.h"
#include "Runtime/Graphics/Buffer.h"
#include "Runtime/Graphics/CommandList.h"
#include "Runtime/Graphics/D3dCommon.h"
#include "Runtime/Graphics/GraphicObject.h"
#include "Runtime/Graphics/Mesh.h"
#include "Runtime/Graphics/Pipeline.h"
#include "Runtime/Graphics/Renderer.h"
#include "Runtime/Graphics/Shader.h"

namespace
{
    constexpr std::size_t k_MaxVertices = 4096;
    constexpr int k_CircleSegments = 12;

    struct DebugVertex
    {
        NS::Core::Vector3 position;
        NS::Core::Color color;
    };

    static_assert(sizeof(DebugVertex) == 28, "DebugVertex は POSITION(12) + COLOR(16) の 28 byte 前提");

    std::vector<DebugVertex>& Storage() noexcept
    {
        static std::vector<DebugVertex> g_vertices;
        return g_vertices;
    }

    // 三角形 12960 枚ぶんの頂点 (約 1 MB)。エディタの面は相手 1 体で 100 枚に満たず、相手の数で溢れない
    constexpr std::size_t k_MaxFaceVertices = 38880;

    std::vector<DebugVertex>& FaceStorage() noexcept
    {
        static std::vector<DebugVertex> g_faceVertices;
        return g_faceVertices;
    }

    // 描画用のリソース一式
    struct LineBackend
    {
        std::unique_ptr<NS::Gfx::Shader> vs;
        std::unique_ptr<NS::Gfx::Shader> ps;
        NS::Gfx::ComPtr<ID3D11InputLayout> inputLayout;
        std::unique_ptr<NS::Gfx::Buffer> vb;
        std::unique_ptr<NS::Gfx::Buffer> faceVb;
        std::unique_ptr<NS::Gfx::Buffer> cb;
        // 面は奥の側も見せるので裏表とも描く。深度を見ないので、物の表面と重なった面もちらつかずに透ける
        std::unique_ptr<NS::Gfx::Pipeline> facePipeline;
        bool initAttempted = false;
        bool valid = false;
    };

    LineBackend& Backend() noexcept
    {
        static LineBackend backend;
        return backend;
    }

    bool EnsureBackend() noexcept
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

        b.vs = NS::Gfx::Shader::CreateBuiltin("debug_line.vs.hlsl");
        b.ps = NS::Gfx::Shader::CreateBuiltin("debug_line.ps.hlsl");
        if (!b.vs->IsValid() || !b.ps->IsValid())
        {
            NS_LOG_ERROR(Graphics, "DebugDraw: shader 構築失敗");
            return false;
        }

        const std::span<const std::byte> bytecode = b.vs->VertexShaderBytecode();
        const D3D11_INPUT_ELEMENT_DESC layout[] = {
            {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
        };
        const HRESULT hr =
            device->CreateInputLayout(layout, 2u, bytecode.data(), bytecode.size(), b.inputLayout.GetAddressOf());
        if (FAILED(hr))
        {
            NS_LOG_ERROR(Graphics, "DebugDraw: CreateInputLayout 失敗 (hr=0x{:08X})", static_cast<unsigned>(hr));
            return false;
        }

        b.vb = NS::Gfx::Buffer::Create(
            NS::Gfx::MakeVertexBufferDesc(nullptr, k_MaxVertices, sizeof(DebugVertex), D3D11_USAGE_DYNAMIC));
        b.faceVb = NS::Gfx::Buffer::Create(
            NS::Gfx::MakeVertexBufferDesc(nullptr, k_MaxFaceVertices, sizeof(DebugVertex), D3D11_USAGE_DYNAMIC));
        b.cb = NS::Gfx::Buffer::Create(NS::Gfx::MakeConstantBufferDesc(sizeof(NS::Core::Matrix)));
        if (!b.vb->IsValid() || !b.faceVb->IsValid() || !b.cb->IsValid())
        {
            NS_LOG_ERROR(Graphics, "DebugDraw: VB / CB 構築失敗");
            return false;
        }

        b.facePipeline = NS::Gfx::Pipeline::Create(NS::Gfx::PipelineDesc{.cull = NS::Gfx::CullMode::None,
                                                                         .blend = NS::Gfx::BlendMode::Alpha,
                                                                         .depth = NS::Gfx::DepthMode::Disabled});
        if (!b.facePipeline->IsValid())
        {
            NS_LOG_ERROR(Graphics, "DebugDraw: 面の Pipeline 構築失敗");
            return false;
        }

        b.valid = true;
        return true;
    }

    void PushLine(const NS::Core::Vector3& a, const NS::Core::Vector3& b, const NS::Core::Color& color) noexcept
    {
        std::vector<DebugVertex>& v = Storage();
        if (v.size() + 2 > k_MaxVertices) // 最大容量を超える場合は最も古い線を破棄する
        {
            v.erase(v.begin(), v.begin() + 2);
        }
        v.push_back({a, color});
        v.push_back({b, color});
    }

    // center を中心に u と v が張る平面上の円を積む。u と v は半径ぶん伸ばした直交ベクトルを渡す
    void PushCircle(const NS::Core::Vector3& center,
                    const NS::Core::Vector3& u,
                    const NS::Core::Vector3& v,
                    const NS::Core::Color& color) noexcept
    {
        constexpr float twoPi = 2.0f * NS::Core::k_Pi;
        NS::Core::Vector3 prev{};
        for (int i = 0; i <= k_CircleSegments; ++i)
        {
            const float t = (static_cast<float>(i) / k_CircleSegments) * twoPi;
            const float ca = std::cos(t);
            const float sa = std::sin(t);
            const NS::Core::Vector3 point{
                center.x + u.x * ca + v.x * sa, center.y + u.y * ca + v.y * sa, center.z + u.z * ca + v.z * sa};
            if (i > 0)
            {
                PushLine(prev, point, color);
            }
            prev = point;
        }
    }
} // namespace

namespace NS::Gfx::DebugDraw
{
    void Line(const NS::Core::Vector3& a, const NS::Core::Vector3& b, const NS::Core::Color& color) noexcept
    {
        PushLine(a, b, color);
    }

    void AABB(const NS::Core::AABB& box, const NS::Core::Color& color) noexcept
    {
        NS::Core::OBB obb{};
        obb.center = box.Center;
        obb.halfExtentX = box.Extents.x;
        obb.halfExtentY = box.Extents.y;
        obb.halfExtentZ = box.Extents.z;
        OBB(obb, color);
    }

    void OBB(const NS::Core::OBB& obb, const NS::Core::Color& color) noexcept
    {
        const NS::Core::Vector3 ex = obb.axisX * obb.halfExtentX;
        const NS::Core::Vector3 ey = obb.axisY * obb.halfExtentY;
        const NS::Core::Vector3 ez = obb.axisZ * obb.halfExtentZ;

        const NS::Core::Vector3 c000 = obb.center - ex - ey - ez;
        const NS::Core::Vector3 c100 = obb.center + ex - ey - ez;
        const NS::Core::Vector3 c110 = obb.center + ex + ey - ez;
        const NS::Core::Vector3 c010 = obb.center - ex + ey - ez;
        const NS::Core::Vector3 c001 = obb.center - ex - ey + ez;
        const NS::Core::Vector3 c101 = obb.center + ex - ey + ez;
        const NS::Core::Vector3 c111 = obb.center + ex + ey + ez;
        const NS::Core::Vector3 c011 = obb.center - ex + ey + ez;

        // 前面
        PushLine(c000, c100, color);
        PushLine(c100, c110, color);
        PushLine(c110, c010, color);
        PushLine(c010, c000, color);

        // 背面
        PushLine(c001, c101, color);
        PushLine(c101, c111, color);
        PushLine(c111, c011, color);
        PushLine(c011, c001, color);

        // 前面と背面を繋ぐ辺
        PushLine(c000, c001, color);
        PushLine(c100, c101, color);
        PushLine(c110, c111, color);
        PushLine(c010, c011, color);
    }

    void Sphere(const NS::Core::Sphere& sphere, const NS::Core::Color& color) noexcept
    {
        const NS::Core::Vector3 rx{sphere.radius, 0.0f, 0.0f};
        const NS::Core::Vector3 ry{0.0f, sphere.radius, 0.0f};
        const NS::Core::Vector3 rz{0.0f, 0.0f, sphere.radius};

        PushCircle(sphere.center, rx, ry, color);
        PushCircle(sphere.center, ry, rz, color);
        PushCircle(sphere.center, rz, rx, color);
    }

    void Circle(const NS::Core::Vector3& center,
                const NS::Core::Vector3& u,
                const NS::Core::Vector3& v,
                const NS::Core::Color& color) noexcept
    {
        PushCircle(center, u, v, color);
    }

    void Capsule(const NS::Core::Vector3& base,
                 const NS::Core::Vector3& axis,
                 float radius,
                 const NS::Core::Color& color) noexcept
    {
        // 上下端の基準点
        const NS::Core::Vector3 top = base + axis;
        const NS::Core::Vector3 bottom = base - axis;

        // 軸に対して垂直な2つのベクトルを計算する
        NS::Core::Vector3 axisN = axis;
        if (axisN.Length() > 1e-6f)
        {
            axisN.Normalize();
        }

        NS::Core::Vector3 perpA{1.0f, 0.0f, 0.0f};
        if (std::abs(axisN.y) < 0.99f)
        {
            perpA = NS::Core::Vector3{0.0f, 1.0f, 0.0f};
        }

        perpA = perpA.Cross(axisN);
        if (perpA.Length() > 1e-6f)
        {
            perpA.Normalize();
        }
        const NS::Core::Vector3 perpB = axisN.Cross(perpA);

        // 上下端の円
        const NS::Core::Vector3 uA = perpA * radius;
        const NS::Core::Vector3 uB = perpB * radius;
        PushCircle(top, uA, uB, color);
        PushCircle(bottom, uA, uB, color);

        // 円柱の側面に沿う線 4 本
        const NS::Core::Vector3 dirs[4] = {uA, -uA, uB, -uB};
        for (const NS::Core::Vector3& d : dirs)
        {
            PushLine(bottom + d, top + d, color);
        }
    }

    void Flush(Renderer& renderer, const NS::Core::Matrix& viewProjection) noexcept
    {
        std::vector<DebugVertex>& store = Storage();
        std::vector<DebugVertex>& faces = FaceStorage();
        if (store.empty() && faces.empty())
        {
            return;
        }

        if (!EnsureBackend())
        {
            Clear();
            return;
        }

        CommandList& cmd = renderer.Commands();
        if (cmd.Native() == nullptr)
        {
            Clear();
            return;
        }

        LineBackend& b = Backend();
        cmd.UpdateSubresource(*b.cb, &viewProjection, sizeof(viewProjection));
        cmd.VSSetShader(*b.vs);
        cmd.PSSetShader(*b.ps);
        cmd.SetInputLayout(b.inputLayout.Get());
        cmd.VSSetConstantBuffer(*b.cb, 0u);

        // 面を先に描く。線は深度を書くので、後に描かないと面が線の奥で途切れる
        if (!faces.empty())
        {
            cmd.UpdateSubresource(*b.faceVb, faces.data(), faces.size() * sizeof(DebugVertex));
            cmd.SetPipeline(*b.facePipeline);
            cmd.SetVertexBuffer(*b.faceVb, 0u);
            cmd.SetTopology(Topology::TriangleList);
            cmd.Draw(static_cast<unsigned>(faces.size()));
        }

        // 蓄積された頂点データを一括で描画する
        if (!store.empty())
        {
            cmd.UpdateSubresource(*b.vb, store.data(), store.size() * sizeof(DebugVertex));
            cmd.SetPipeline(renderer.CommonPipeline(BlendMode::Opaque));
            cmd.SetVertexBuffer(*b.vb, 0u);
            cmd.SetTopology(Topology::LineList);
            cmd.Draw(static_cast<unsigned>(store.size()));
        }

        Clear();
    }

    void BeginStep() noexcept
    {
        Storage().clear();
        FaceStorage().clear();
    }

    void Clear() noexcept
    {
        Storage().clear();
        FaceStorage().clear();
    }

    std::size_t VertexCount() noexcept
    {
        return Storage().size();
    }

    void Triangle(const NS::Core::Vector3& a,
                  const NS::Core::Vector3& b,
                  const NS::Core::Vector3& c,
                  const NS::Core::Color& color) noexcept
    {
        std::vector<DebugVertex>& faces = FaceStorage();
        // 線と同じく、上限を超える時は最も古い三角形を捨てる
        if (faces.size() + 3 > k_MaxFaceVertices)
        {
            faces.erase(faces.begin(), faces.begin() + 3);
        }
        faces.push_back({a, color});
        faces.push_back({b, color});
        faces.push_back({c, color});
    }

    std::size_t FaceVertexCount() noexcept
    {
        return FaceStorage().size();
    }
} // namespace NS::Gfx::DebugDraw
