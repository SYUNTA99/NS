#include "Runtime/Graphics/DebugDraw.h"

#include "Runtime/Core/Filesystem.h"
#include "Runtime/Core/LogCategories.h"
#include "Runtime/Core/Logger.h"
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

    // 描画用のリソース一式
    struct LineBackend
    {
        std::unique_ptr<NS::Graphics::Shader> vs;
        std::unique_ptr<NS::Graphics::Shader> ps;
        NS::Graphics::ComPtr<ID3D11InputLayout> inputLayout;
        std::unique_ptr<NS::Graphics::Buffer> vb;
        std::unique_ptr<NS::Graphics::Buffer> cb;
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
            return b.valid;
        b.initAttempted = true;

        auto* device = NS::Graphics::Gpu().device;
        if (device == nullptr)
        {
            NS_LOG_ERROR(Graphics, "DebugDraw: グローバル Device が無効");
            return false;
        }

        const auto shaderDir = ::NS::Core::FileSystem::ContentRoot() / "Shaders";
        b.vs = NS::Graphics::Shader::Create(shaderDir / "debug_line.vs.hlsl");
        b.ps = NS::Graphics::Shader::Create(shaderDir / "debug_line.ps.hlsl");
        if (!b.vs->IsValid() || !b.ps->IsValid())
        {
            NS_LOG_ERROR(Graphics, "DebugDraw: shader 構築失敗");
            return false;
        }

        const auto bytecode = b.vs->VertexShaderBytecode();
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

        b.vb = NS::Graphics::Buffer::Create(
            NS::Graphics::MakeVertexBufferDesc(nullptr, k_MaxVertices, sizeof(DebugVertex), D3D11_USAGE_DYNAMIC));
        b.cb = NS::Graphics::Buffer::Create(NS::Graphics::MakeConstantBufferDesc(sizeof(NS::Core::Matrix)));
        if (!b.vb->IsValid() || !b.cb->IsValid())
        {
            NS_LOG_ERROR(Graphics, "DebugDraw: VB / CB 構築失敗");
            return false;
        }

        b.valid = true;
        return true;
    }

    void PushLine(const NS::Core::Vector3& a, const NS::Core::Vector3& b, const NS::Core::Color& color) noexcept
    {
        auto& v = Storage();
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
                PushLine(prev, point, color);
            prev = point;
        }
    }
} // namespace

namespace NS::Graphics::DebugDraw
{
    void Line(const NS::Core::Vector3& a, const NS::Core::Vector3& b, const NS::Core::Color& color) noexcept
    {
        PushLine(a, b, color);
    }

    void AABB(const NS::Core::AABB& box, const NS::Core::Color& color) noexcept
    {
        const float cx = box.Center.x;
        const float cy = box.Center.y;
        const float cz = box.Center.z;
        const float ex = box.Extents.x;
        const float ey = box.Extents.y;
        const float ez = box.Extents.z;

        const NS::Core::Vector3 c000{cx - ex, cy - ey, cz - ez};
        const NS::Core::Vector3 c100{cx + ex, cy - ey, cz - ez};
        const NS::Core::Vector3 c110{cx + ex, cy + ey, cz - ez};
        const NS::Core::Vector3 c010{cx - ex, cy + ey, cz - ez};
        const NS::Core::Vector3 c001{cx - ex, cy - ey, cz + ez};
        const NS::Core::Vector3 c101{cx + ex, cy - ey, cz + ez};
        const NS::Core::Vector3 c111{cx + ex, cy + ey, cz + ez};
        const NS::Core::Vector3 c011{cx - ex, cy + ey, cz + ez};

        // 底面
        PushLine(c000, c100, color);
        PushLine(c100, c101, color);
        PushLine(c101, c001, color);
        PushLine(c001, c000, color);

        // 上面
        PushLine(c010, c110, color);
        PushLine(c110, c111, color);
        PushLine(c111, c011, color);
        PushLine(c011, c010, color);

        // 縦辺
        PushLine(c000, c010, color);
        PushLine(c100, c110, color);
        PushLine(c101, c111, color);
        PushLine(c001, c011, color);
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

        // 側面を繋ぐ辺
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
        const float axisLen = std::sqrt(axisN.x * axisN.x + axisN.y * axisN.y + axisN.z * axisN.z);
        if (axisLen > 1e-6f)
        {
            axisN.x /= axisLen;
            axisN.y /= axisLen;
            axisN.z /= axisLen;
        }
        NS::Core::Vector3 perpA =
            (std::abs(axisN.y) < 0.99f) ? NS::Core::Vector3{0.0f, 1.0f, 0.0f} : NS::Core::Vector3{1.0f, 0.0f, 0.0f};
        perpA = {perpA.y * axisN.z - perpA.z * axisN.y,
                 perpA.z * axisN.x - perpA.x * axisN.z,
                 perpA.x * axisN.y - perpA.y * axisN.x};
        const float plen = std::sqrt(perpA.x * perpA.x + perpA.y * perpA.y + perpA.z * perpA.z);
        if (plen > 1e-6f)
        {
            perpA.x /= plen;
            perpA.y /= plen;
            perpA.z /= plen;
        }
        const NS::Core::Vector3 perpB{axisN.y * perpA.z - axisN.z * perpA.y,
                                      axisN.z * perpA.x - axisN.x * perpA.z,
                                      axisN.x * perpA.y - axisN.y * perpA.x};

        // 上下の円を描画する
        const NS::Core::Vector3 uA{perpA.x * radius, perpA.y * radius, perpA.z * radius};
        const NS::Core::Vector3 uB{perpB.x * radius, perpB.y * radius, perpB.z * radius};
        PushCircle(top, uA, uB, color);
        PushCircle(bottom, uA, uB, color);

        // 円柱部分の側面の辺を描画する
        const NS::Core::Vector3 dirs[4] = {uA, -uA, uB, -uB};
        for (const auto& d : dirs)
        {
            PushLine(bottom + d, top + d, color);
        }
    }

    void Flush(Renderer& renderer, const NS::Core::Matrix& viewProjection) noexcept
    {
        std::vector<DebugVertex>& store = Storage();
        if (store.empty())
            return;

        if (!EnsureBackend())
        {
            Clear();
            return;
        }

        auto& cmd = renderer.Commands();
        if (cmd.Native() == nullptr)
        {
            Clear();
            return;
        }

        LineBackend& b = Backend();
        const std::size_t vertexCount = store.size();

        cmd.UpdateSubresource(*b.cb, &viewProjection, sizeof(viewProjection));
        cmd.UpdateSubresource(*b.vb, store.data(), vertexCount * sizeof(DebugVertex));

        // 蓄積された頂点データを一括で描画する
        cmd.SetPipeline(renderer.CommonPipeline(BlendMode::Opaque));
        cmd.VSSetShader(*b.vs);
        cmd.PSSetShader(*b.ps);
        cmd.SetInputLayout(b.inputLayout.Get());
        cmd.VSSetConstantBuffer(*b.cb, 0u);
        cmd.SetVertexBuffer(*b.vb, 0u);
        cmd.SetTopology(Topology::LineList);
        cmd.Draw(static_cast<unsigned>(vertexCount));

        Clear();
    }

    void Clear() noexcept
    {
        Storage().clear();
    }

    std::size_t VertexCount() noexcept
    {
        return Storage().size();
    }
} // namespace NS::Graphics::DebugDraw
