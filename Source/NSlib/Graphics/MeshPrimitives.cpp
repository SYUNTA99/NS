#include "NSlib/Graphics/MeshPrimitives.h"

#include <algorithm>

namespace NS::Gfx
{
    namespace
    {
        // 1 行 segments + 1 頂点で rows 行並んだ格子を、隣り合う 2 行ずつ三角形 2 枚の帯で繋ぐ
        // 行が下へ進むほど経線を南へ下る並びで、各面が外側を向く
        void AppendLatticeIndices(std::uint32_t rows, std::uint32_t segments, std::vector<std::uint32_t>& outIndices)
        {
            const std::uint32_t stride = segments + 1;
            outIndices.reserve(outIndices.size() + static_cast<std::size_t>(rows - 1) * segments * 6);
            for (std::uint32_t row = 0; row + 1 < rows; ++row)
            {
                for (std::uint32_t segment = 0; segment < segments; ++segment)
                {
                    const std::uint32_t upper = row * stride + segment;
                    const std::uint32_t lower = upper + stride;
                    outIndices.push_back(upper);
                    outIndices.push_back(lower);
                    outIndices.push_back(upper + 1);
                    outIndices.push_back(upper + 1);
                    outIndices.push_back(lower);
                    outIndices.push_back(lower + 1);
                }
            }
        }
    } // namespace

    MeshGeometry MakeCube(const NS::Vector3& extents)
    {
        const float ex = extents.x;
        const float ey = extents.y;
        const float ez = extents.z;

        MeshGeometry geom;
        geom.vertices.reserve(24);

        // 右側面
        geom.vertices.push_back({{ex, -ey, ez}, {0.0f, 1.0f}, {1.0f, 0.0f, 0.0f}});
        geom.vertices.push_back({{ex, ey, ez}, {0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}});
        geom.vertices.push_back({{ex, ey, -ez}, {1.0f, 0.0f}, {1.0f, 0.0f, 0.0f}});
        geom.vertices.push_back({{ex, -ey, -ez}, {1.0f, 1.0f}, {1.0f, 0.0f, 0.0f}});
        // 左側面
        geom.vertices.push_back({{-ex, -ey, -ez}, {0.0f, 1.0f}, {-1.0f, 0.0f, 0.0f}});
        geom.vertices.push_back({{-ex, ey, -ez}, {0.0f, 0.0f}, {-1.0f, 0.0f, 0.0f}});
        geom.vertices.push_back({{-ex, ey, ez}, {1.0f, 0.0f}, {-1.0f, 0.0f, 0.0f}});
        geom.vertices.push_back({{-ex, -ey, ez}, {1.0f, 1.0f}, {-1.0f, 0.0f, 0.0f}});
        // 上面
        geom.vertices.push_back({{-ex, ey, ez}, {0.0f, 1.0f}, {0.0f, 1.0f, 0.0f}});
        geom.vertices.push_back({{-ex, ey, -ez}, {0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}});
        geom.vertices.push_back({{ex, ey, -ez}, {1.0f, 0.0f}, {0.0f, 1.0f, 0.0f}});
        geom.vertices.push_back({{ex, ey, ez}, {1.0f, 1.0f}, {0.0f, 1.0f, 0.0f}});
        // 底面
        geom.vertices.push_back({{-ex, -ey, -ez}, {0.0f, 1.0f}, {0.0f, -1.0f, 0.0f}});
        geom.vertices.push_back({{-ex, -ey, ez}, {0.0f, 0.0f}, {0.0f, -1.0f, 0.0f}});
        geom.vertices.push_back({{ex, -ey, ez}, {1.0f, 0.0f}, {0.0f, -1.0f, 0.0f}});
        geom.vertices.push_back({{ex, -ey, -ez}, {1.0f, 1.0f}, {0.0f, -1.0f, 0.0f}});
        // 手前
        geom.vertices.push_back({{ex, -ey, ez}, {0.0f, 1.0f}, {0.0f, 0.0f, 1.0f}});
        geom.vertices.push_back({{ex, ey, ez}, {0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}});
        geom.vertices.push_back({{-ex, ey, ez}, {1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}});
        geom.vertices.push_back({{-ex, -ey, ez}, {1.0f, 1.0f}, {0.0f, 0.0f, 1.0f}});
        // 奥
        geom.vertices.push_back({{-ex, -ey, -ez}, {0.0f, 1.0f}, {0.0f, 0.0f, -1.0f}});
        geom.vertices.push_back({{-ex, ey, -ez}, {0.0f, 0.0f}, {0.0f, 0.0f, -1.0f}});
        geom.vertices.push_back({{ex, ey, -ez}, {1.0f, 0.0f}, {0.0f, 0.0f, -1.0f}});
        geom.vertices.push_back({{ex, -ey, -ez}, {1.0f, 1.0f}, {0.0f, 0.0f, -1.0f}});

        // 各面が「外側」を向くようにインデックスを配置する
        geom.indices = {
            0,  2,  1,  0,  3,  2,  // +X
            4,  6,  5,  4,  7,  6,  // -X
            8,  10, 9,  8,  11, 10, // +Y
            12, 14, 13, 12, 15, 14, // -Y
            16, 17, 18, 16, 18, 19, // +Z
            20, 21, 22, 20, 22, 23, // -Z
        };
        return geom;
    }

    MeshGeometry MakePlane(const NS::Vector2& extents)
    {
        const float ex = extents.x;
        const float ez = extents.y;

        MeshGeometry geom;
        geom.vertices = {
            {{-ex, 0.0f, ez}, {0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}},
            {{ex, 0.0f, ez}, {1.0f, 0.0f}, {0.0f, 1.0f, 0.0f}},
            {{ex, 0.0f, -ez}, {1.0f, 1.0f}, {0.0f, 1.0f, 0.0f}},
            {{-ex, 0.0f, -ez}, {0.0f, 1.0f}, {0.0f, 1.0f, 0.0f}},
        };
        // 各面が「外側」を向くようにインデックスを配置する
        geom.indices = {0, 1, 2, 0, 2, 3};
        return geom;
    }

    MeshGeometry MakeSlope(float angleDegrees, const NS::Vector3& extents)
    {
        const float ex = extents.x;
        const float ey = extents.y;
        const float ez = extents.z;

        // スロープの高さを求め、指定の最大高さで上限クランプする
        const float angleRad = NS::DegreesToRadians(angleDegrees);
        const float rawHeight = std::tan(angleRad) * (2.0f * ez);
        const float height = std::min(rawHeight, 2.0f * ey);

        const float yBottom = -ey;
        const float yTop = -ey + height;

        // 斜面の法線ベクトルを計算する
        const float c = std::cos(angleRad);
        const float s = std::sin(angleRad);

        MeshGeometry geom;
        geom.vertices.reserve(18);

        // スロープ上面
        geom.vertices.push_back({{-ex, yBottom, -ez}, {0.0f, 1.0f}, {0.0f, c, -s}});
        geom.vertices.push_back({{ex, yBottom, -ez}, {1.0f, 1.0f}, {0.0f, c, -s}});
        geom.vertices.push_back({{ex, yTop, ez}, {1.0f, 0.0f}, {0.0f, c, -s}});
        geom.vertices.push_back({{-ex, yTop, ez}, {0.0f, 0.0f}, {0.0f, c, -s}});

        // 底面
        geom.vertices.push_back({{-ex, yBottom, -ez}, {0.0f, 0.0f}, {0.0f, -1.0f, 0.0f}});
        geom.vertices.push_back({{-ex, yBottom, ez}, {0.0f, 1.0f}, {0.0f, -1.0f, 0.0f}});
        geom.vertices.push_back({{ex, yBottom, ez}, {1.0f, 1.0f}, {0.0f, -1.0f, 0.0f}});
        geom.vertices.push_back({{ex, yBottom, -ez}, {1.0f, 0.0f}, {0.0f, -1.0f, 0.0f}});

        // 前面
        geom.vertices.push_back({{-ex, yBottom, ez}, {0.0f, 1.0f}, {0.0f, 0.0f, 1.0f}});
        geom.vertices.push_back({{-ex, yTop, ez}, {0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}});
        geom.vertices.push_back({{ex, yTop, ez}, {1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}});
        geom.vertices.push_back({{ex, yBottom, ez}, {1.0f, 1.0f}, {0.0f, 0.0f, 1.0f}});

        // 左側面
        geom.vertices.push_back({{-ex, yBottom, -ez}, {0.0f, 1.0f}, {-1.0f, 0.0f, 0.0f}});
        geom.vertices.push_back({{-ex, yBottom, ez}, {1.0f, 1.0f}, {-1.0f, 0.0f, 0.0f}});
        geom.vertices.push_back({{-ex, yTop, ez}, {1.0f, 0.0f}, {-1.0f, 0.0f, 0.0f}});

        // 右側面
        geom.vertices.push_back({{ex, yBottom, -ez}, {0.0f, 1.0f}, {1.0f, 0.0f, 0.0f}});
        geom.vertices.push_back({{ex, yTop, ez}, {1.0f, 0.0f}, {1.0f, 0.0f, 0.0f}});
        geom.vertices.push_back({{ex, yBottom, ez}, {1.0f, 1.0f}, {1.0f, 0.0f, 0.0f}});

        // 各面が「外側」を向くようにインデックスを配置する
        geom.indices = {
            0, 3, 2, 0, 2, 1, 4, 7, 6, 4, 6, 5, 8, 10, 9, 8, 11, 10, 12, 13, 14, 15, 16, 17,
        };

        return geom;
    }

    MeshGeometry MakeSphere(float radius, std::uint32_t rings, std::uint32_t segments)
    {
        rings = std::max(rings, 2u);
        segments = std::max(segments, 3u);

        MeshGeometry geom;
        geom.vertices.reserve(static_cast<std::size_t>(rings + 1) * (segments + 1));

        // 経線の継ぎ目で u が 0 と 1 に割れるよう、一周した先にもう 1 列重ねて置く
        for (std::uint32_t ring = 0; ring <= rings; ++ring)
        {
            const float v = static_cast<float>(ring) / static_cast<float>(rings);
            const float phi = v * NS::k_Pi;
            const float y = std::cos(phi);
            const float ringRadius = std::sin(phi);
            for (std::uint32_t segment = 0; segment <= segments; ++segment)
            {
                const float u = static_cast<float>(segment) / static_cast<float>(segments);
                const float theta = u * NS::k_Pi * 2.0f;
                const NS::Vector3 normal{ringRadius * std::sin(theta), y, ringRadius * std::cos(theta)};
                geom.vertices.push_back({{normal.x * radius, normal.y * radius, normal.z * radius}, {u, v}, normal});
            }
        }

        // 各面が「外側」を向くようにインデックスを配置する。極の 1 段は面積 0 の三角形になる
        AppendLatticeIndices(rings + 1, segments, geom.indices);
        return geom;
    }

    MeshGeometry MakeCapsule(float radius, float halfHeight, std::uint32_t rings, std::uint32_t segments)
    {
        radius = std::max(radius, 0.0f);
        halfHeight = std::max(halfHeight, 0.0f);
        rings = std::max(rings, 1u);
        segments = std::max(segments, 3u);

        // v は経線に沿った長さの比。円柱部で模様が縦に伸びない
        const float quarterArc = radius * NS::k_Pi * 0.5f;
        const float meridian = 2.0f * quarterArc + 2.0f * halfHeight;

        // 上の半球を北極から赤道へ、続けて下の半球を赤道から南極へ置く。赤道は上下で 2 列あり、その間が円柱になる
        const std::uint32_t rows = 2 * (rings + 1);
        const std::uint32_t stride = segments + 1;
        MeshGeometry geom;
        geom.vertices.reserve(static_cast<std::size_t>(rows) * stride);
        for (std::uint32_t row = 0; row < rows; ++row)
        {
            std::uint32_t step = row;
            float centerY = halfHeight;
            float phiStart = 0.0f;
            float arcBefore = 0.0f;
            if (row > rings)
            {
                step = row - (rings + 1);
                centerY = -halfHeight;
                phiStart = NS::k_Pi * 0.5f;
                arcBefore = quarterArc + 2.0f * halfHeight;
            }
            const float t = static_cast<float>(step) / static_cast<float>(rings);
            const float phi = phiStart + t * NS::k_Pi * 0.5f;
            const float y = std::cos(phi);
            const float ringRadius = std::sin(phi);
            float v = 0.0f;
            if (meridian > 0.0f)
            {
                v = (arcBefore + t * quarterArc) / meridian;
            }

            // 経線の継ぎ目で u が 0 と 1 に割れるよう、一周した先にもう 1 列重ねて置く
            for (std::uint32_t segment = 0; segment <= segments; ++segment)
            {
                const float u = static_cast<float>(segment) / static_cast<float>(segments);
                const float theta = u * NS::k_Pi * 2.0f;
                const NS::Vector3 normal{ringRadius * std::sin(theta), y, ringRadius * std::cos(theta)};
                geom.vertices.push_back(
                    {{normal.x * radius, normal.y * radius + centerY, normal.z * radius}, {u, v}, normal});
            }
        }

        // 並びは MakeSphere と同じ。極の 1 段と、円柱の長さ 0 の赤道は面積 0 の三角形になる
        AppendLatticeIndices(rows, segments, geom.indices);
        return geom;
    }

} // namespace NS::Gfx
