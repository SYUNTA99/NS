#include "Framework/Graphics/DebugDraw.h"

#include "Framework/Graphics/Buffer.h"
#include "Framework/Graphics/CommandList.h"
#include "Framework/Graphics/D3dCommon.h"
#include "Framework/Graphics/GraphicObject.h"
#include "Framework/Graphics/Mesh.h"
#include "Framework/Graphics/Pipeline.h"
#include "Framework/Graphics/Renderer.h"
#include "Framework/Graphics/Shader.h"

#include "Framework/Core/Filesystem.h"
#include "Framework/Core/LogCategories.h"
#include "Framework/Core/Logger.h"

#include <cmath>

namespace
{
    constexpr std::size_t kMaxVertices = 4096;
    constexpr int kCapsuleSegments = 12;

    struct DebugVertex
    {
        NS::Math::Vector3 position;
        NS::Math::Color color;
    };
    // POSITION 12 byte + COLOR 16 byte = 28 byte。 InputLayout の COLOR offset 12 はこの並びに依存する
    static_assert(sizeof(DebugVertex) == 28, "DebugVertex は POSITION(12) + COLOR(16) の 28 byte 前提");

    std::vector<DebugVertex>& Storage() noexcept
    {
        static std::vector<DebugVertex> g_vertices;
        return g_vertices;
    }

    // line list を 1 描画する GPU リソース一式。 初回 Flush 時に遅延生成する
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

    // shader / InputLayout / 動的 VB / CB を生成する。 1 度だけ試行し、 device 無効や失敗は valid=false で抜ける
    bool EnsureBackend() noexcept
    {
        LineBackend& b = Backend();
        if (b.initAttempted)
            return b.valid;
        b.initAttempted = true;

        auto* device = NS::Graphics::Gpu().device;
        if (device == nullptr)
        {
            NS_LOG_ERROR(::NS::Core::LogCat::Graphics, "DebugDraw: グローバル Device が無効");
            return false;
        }

        const auto shaderDir = ::NS::Core::FileSystem::ContentRoot() / "Shaders";
        b.vs = NS::Graphics::Shader::Create(shaderDir / "debug_line.vs.hlsl");
        b.ps = NS::Graphics::Shader::Create(shaderDir / "debug_line.ps.hlsl");
        if (!b.vs->IsValid() || !b.ps->IsValid())
        {
            NS_LOG_ERROR(::NS::Core::LogCat::Graphics, "DebugDraw: shader 構築失敗");
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
            NS_LOG_ERROR(::NS::Core::LogCat::Graphics,
                         "DebugDraw: CreateInputLayout 失敗 (hr=0x{:08X})",
                         static_cast<unsigned>(hr));
            return false;
        }

        b.vb = NS::Graphics::Buffer::Create(
            NS::Graphics::MakeVertexBufferDesc(nullptr, kMaxVertices, sizeof(DebugVertex), D3D11_USAGE_DYNAMIC));
        b.cb = NS::Graphics::Buffer::Create(NS::Graphics::MakeConstantBufferDesc(sizeof(NS::Math::Matrix)));
        if (!b.vb->IsValid() || !b.cb->IsValid())
        {
            NS_LOG_ERROR(::NS::Core::LogCat::Graphics, "DebugDraw: VB / CB 構築失敗");
            return false;
        }

        b.valid = true;
        return true;
    }

    /// 1 frame で 2 vertex 追加。容量超過時は最古の 1 line ぶんの 2 vertex を drop
    void PushLine(const NS::Math::Vector3& a, const NS::Math::Vector3& b, const NS::Math::Color& color) noexcept
    {
        auto& v = Storage();
        if (v.size() + 2 > kMaxVertices)
        {
            v.erase(v.begin(), v.begin() + 2);
        }
        v.push_back({a, color});
        v.push_back({b, color});
    }
} // namespace

namespace NS::Graphics::DebugDraw
{
    void Line(const NS::Math::Vector3& a, const NS::Math::Vector3& b, const NS::Math::Color& color) noexcept
    {
        PushLine(a, b, color);
    }

    void AABB(const NS::Math::AABB& box, const NS::Math::Color& color) noexcept
    {
        const float cx = box.Center.x;
        const float cy = box.Center.y;
        const float cz = box.Center.z;
        const float ex = box.Extents.x;
        const float ey = box.Extents.y;
        const float ez = box.Extents.z;

        const NS::Math::Vector3 c000{cx - ex, cy - ey, cz - ez};
        const NS::Math::Vector3 c100{cx + ex, cy - ey, cz - ez};
        const NS::Math::Vector3 c110{cx + ex, cy + ey, cz - ez};
        const NS::Math::Vector3 c010{cx - ex, cy + ey, cz - ez};
        const NS::Math::Vector3 c001{cx - ex, cy - ey, cz + ez};
        const NS::Math::Vector3 c101{cx + ex, cy - ey, cz + ez};
        const NS::Math::Vector3 c111{cx + ex, cy + ey, cz + ez};
        const NS::Math::Vector3 c011{cx - ex, cy + ey, cz + ez};

        // 底面 4 line
        PushLine(c000, c100, color);
        PushLine(c100, c101, color);
        PushLine(c101, c001, color);
        PushLine(c001, c000, color);

        // 上面 4 line
        PushLine(c010, c110, color);
        PushLine(c110, c111, color);
        PushLine(c111, c011, color);
        PushLine(c011, c010, color);

        // 4 縦辺
        PushLine(c000, c010, color);
        PushLine(c100, c110, color);
        PushLine(c101, c111, color);
        PushLine(c001, c011, color);
    }

    void OBB(const NS::Math::Vector3& center,
             const NS::Math::Vector3& axisX,
             const NS::Math::Vector3& axisY,
             const NS::Math::Vector3& axisZ,
             const NS::Math::Vector3& halfExtents,
             const NS::Math::Color& color) noexcept
    {
        const NS::Math::Vector3 ex = axisX * halfExtents.x;
        const NS::Math::Vector3 ey = axisY * halfExtents.y;
        const NS::Math::Vector3 ez = axisZ * halfExtents.z;

        // 8 隅。 添字は各 axis 方向の符号 -/+
        const NS::Math::Vector3 c000 = center - ex - ey - ez;
        const NS::Math::Vector3 c100 = center + ex - ey - ez;
        const NS::Math::Vector3 c110 = center + ex + ey - ez;
        const NS::Math::Vector3 c010 = center - ex + ey - ez;
        const NS::Math::Vector3 c001 = center - ex - ey + ez;
        const NS::Math::Vector3 c101 = center + ex - ey + ez;
        const NS::Math::Vector3 c111 = center + ex + ey + ez;
        const NS::Math::Vector3 c011 = center - ex + ey + ez;

        // -Z 面の線 4 本
        PushLine(c000, c100, color);
        PushLine(c100, c110, color);
        PushLine(c110, c010, color);
        PushLine(c010, c000, color);

        // +Z 面の線 4 本
        PushLine(c001, c101, color);
        PushLine(c101, c111, color);
        PushLine(c111, c011, color);
        PushLine(c011, c001, color);

        // axisZ 方向 4 稜
        PushLine(c000, c001, color);
        PushLine(c100, c101, color);
        PushLine(c110, c111, color);
        PushLine(c010, c011, color);
    }

    void Capsule(const NS::Math::Vector3& base,
                 const NS::Math::Vector3& axis,
                 float radius,
                 const NS::Math::Color& color) noexcept
    {
        // axis は Capsule 中心から top までの方向ベクトルで長さは halfHeight
        const NS::Math::Vector3 top = base + axis;
        const NS::Math::Vector3 bottom = base - axis;

        // axis に垂直な 2 方向 perpA と perpB を計算
        NS::Math::Vector3 axisN = axis;
        const float axisLen = std::sqrt(axisN.x * axisN.x + axisN.y * axisN.y + axisN.z * axisN.z);
        if (axisLen > 1e-6f)
        {
            axisN.x /= axisLen;
            axisN.y /= axisLen;
            axisN.z /= axisLen;
        }
        NS::Math::Vector3 perpA =
            (std::abs(axisN.y) < 0.99f) ? NS::Math::Vector3{0.0f, 1.0f, 0.0f} : NS::Math::Vector3{1.0f, 0.0f, 0.0f};
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
        const NS::Math::Vector3 perpB{axisN.y * perpA.z - axisN.z * perpA.y,
                                      axisN.z * perpA.x - axisN.x * perpA.z,
                                      axisN.x * perpA.y - axisN.y * perpA.x};

        // axis 周り・半径 radius の上下 2 つの大円を各 12 分割
        const float twoPi = 6.2831853f;
        NS::Math::Vector3 prevTop{}, prevBot{};
        for (int i = 0; i <= kCapsuleSegments; ++i)
        {
            const float t = (static_cast<float>(i) / kCapsuleSegments) * twoPi;
            const float ca = std::cos(t) * radius;
            const float sa = std::sin(t) * radius;
            const NS::Math::Vector3 offset{
                perpA.x * ca + perpB.x * sa, perpA.y * ca + perpB.y * sa, perpA.z * ca + perpB.z * sa};
            const NS::Math::Vector3 ptTop = top + offset;
            const NS::Math::Vector3 ptBot = bottom + offset;
            if (i > 0)
            {
                PushLine(prevTop, ptTop, color);
                PushLine(prevBot, ptBot, color);
            }
            prevTop = ptTop;
            prevBot = ptBot;
        }

        // Cylinder 部の 4 縦線で perpA / -perpA / perpB / -perpB 方向
        const NS::Math::Vector3 dirs[4] = {{perpA.x * radius, perpA.y * radius, perpA.z * radius},
                                           {-perpA.x * radius, -perpA.y * radius, -perpA.z * radius},
                                           {perpB.x * radius, perpB.y * radius, perpB.z * radius},
                                           {-perpB.x * radius, -perpB.y * radius, -perpB.z * radius}};
        for (const auto& d : dirs)
        {
            PushLine(bottom + d, top + d, color);
        }
    }

    void Flush(Renderer& renderer, const NS::Math::Matrix& viewProjection) noexcept
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
        const std::size_t vertexCount = store.size(); // 蓄積側で kMaxVertices に cap 済

        cmd.UpdateBuffer(*b.cb, &viewProjection, sizeof(viewProjection));
        cmd.UpdateBuffer(*b.vb, store.data(), vertexCount * sizeof(DebugVertex));

        cmd.SetPipeline(renderer.CommonPipeline(BlendMode::Opaque));
        cmd.SetShader(*b.vs);
        cmd.SetShader(*b.ps);
        cmd.SetInputLayout(b.inputLayout.Get());
        cmd.SetConstantBuffer(*b.cb, 0u, ShaderType::Vertex);
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
