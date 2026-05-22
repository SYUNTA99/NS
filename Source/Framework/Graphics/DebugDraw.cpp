#include "Framework/Graphics/DebugDraw.h"

#include "Framework/Core/LogCategories.h"
#include "Framework/Core/Logger.h"

#include <cmath>
#include <vector>

namespace
{
    constexpr std::size_t kMaxVertices = 4096;
    constexpr int kCapsuleSegments = 12;

    struct DebugVertex
    {
        NS::Core::Vector3 position;
        NS::Core::Color color;
    };

    std::vector<DebugVertex>& Storage() noexcept
    {
        static std::vector<DebugVertex> g_vertices;
        return g_vertices;
    }

    bool& FlushWarningShown() noexcept
    {
        static bool s_warned = false;
        return s_warned;
    }

    /// 1 frame で 2 vertex 追加。容量超過時は最古の 1 line (2 vertex) を drop。
    void PushLine(const NS::Core::Vector3& a, const NS::Core::Vector3& b, const NS::Core::Color& color) noexcept
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

    void Capsule(const NS::Core::Vector3& base,
                 const NS::Core::Vector3& axis,
                 float radius,
                 const NS::Core::Color& color) noexcept
    {
        // axis は Capsule 中心から top までの方向ベクトル (長さ = halfHeight)
        const NS::Core::Vector3 top = base + axis;
        const NS::Core::Vector3 bottom = base - axis;

        // axis に垂直な 2 方向 (perpA, perpB) を計算
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

        // 上下 2 つの大円 (axis 周り、半径=radius)、各 12 分割
        const float twoPi = 6.2831853f;
        NS::Core::Vector3 prevTop{}, prevBot{};
        for (int i = 0; i <= kCapsuleSegments; ++i)
        {
            const float t = (static_cast<float>(i) / kCapsuleSegments) * twoPi;
            const float ca = std::cos(t) * radius;
            const float sa = std::sin(t) * radius;
            const NS::Core::Vector3 offset{
                perpA.x * ca + perpB.x * sa, perpA.y * ca + perpB.y * sa, perpA.z * ca + perpB.z * sa};
            const NS::Core::Vector3 ptTop = top + offset;
            const NS::Core::Vector3 ptBot = bottom + offset;
            if (i > 0)
            {
                PushLine(prevTop, ptTop, color);
                PushLine(prevBot, ptBot, color);
            }
            prevTop = ptTop;
            prevBot = ptBot;
        }

        // Cylinder 部の 4 縦線 (perpA / -perpA / perpB / -perpB 方向)
        const NS::Core::Vector3 dirs[4] = {{perpA.x * radius, perpA.y * radius, perpA.z * radius},
                                           {-perpA.x * radius, -perpA.y * radius, -perpA.z * radius},
                                           {perpB.x * radius, perpB.y * radius, perpB.z * radius},
                                           {-perpB.x * radius, -perpB.y * radius, -perpB.z * radius}};
        for (const auto& d : dirs)
        {
            PushLine(bottom + d, top + d, color);
        }
    }

    void Flush(Renderer& /*renderer*/, const NS::Core::Matrix& /*viewProjection*/) noexcept
    {
        // GPU 描画接続は  (Player Capsule 表示) と並行で実装する。
        // 蓄積側は完成しているので、現状は 1 度だけ警告して clear する。
        if (!FlushWarningShown())
        {
            NS_LOG_WARN(::NS::Core::LogCat::Graphics,
                        "DebugDraw::Flush は描画未接続。蓄積 {} vertex を drop。",
                        Storage().size());
            FlushWarningShown() = true;
        }
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
