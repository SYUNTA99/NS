#include "Framework/Graphics/MeshPrimitives.h"

#include <algorithm>
#include <cmath>

namespace NS::Graphics
{
    MeshGeometry MakeCube(const NS::Math::Vector3& extents)
    {
        const float ex = extents.x;
        const float ey = extents.y;
        const float ez = extents.z;

        MeshGeometry geom;
        geom.vertices.reserve(24);

        // +X (右)
        geom.vertices.push_back({{ex, -ey, ez}, {0.0f, 1.0f}, {1.0f, 0.0f, 0.0f}});
        geom.vertices.push_back({{ex, ey, ez}, {0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}});
        geom.vertices.push_back({{ex, ey, -ez}, {1.0f, 0.0f}, {1.0f, 0.0f, 0.0f}});
        geom.vertices.push_back({{ex, -ey, -ez}, {1.0f, 1.0f}, {1.0f, 0.0f, 0.0f}});
        // -X (左)
        geom.vertices.push_back({{-ex, -ey, -ez}, {0.0f, 1.0f}, {-1.0f, 0.0f, 0.0f}});
        geom.vertices.push_back({{-ex, ey, -ez}, {0.0f, 0.0f}, {-1.0f, 0.0f, 0.0f}});
        geom.vertices.push_back({{-ex, ey, ez}, {1.0f, 0.0f}, {-1.0f, 0.0f, 0.0f}});
        geom.vertices.push_back({{-ex, -ey, ez}, {1.0f, 1.0f}, {-1.0f, 0.0f, 0.0f}});
        // +Y (上)
        geom.vertices.push_back({{-ex, ey, ez}, {0.0f, 1.0f}, {0.0f, 1.0f, 0.0f}});
        geom.vertices.push_back({{-ex, ey, -ez}, {0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}});
        geom.vertices.push_back({{ex, ey, -ez}, {1.0f, 0.0f}, {0.0f, 1.0f, 0.0f}});
        geom.vertices.push_back({{ex, ey, ez}, {1.0f, 1.0f}, {0.0f, 1.0f, 0.0f}});
        // -Y (下)
        geom.vertices.push_back({{-ex, -ey, -ez}, {0.0f, 1.0f}, {0.0f, -1.0f, 0.0f}});
        geom.vertices.push_back({{-ex, -ey, ez}, {0.0f, 0.0f}, {0.0f, -1.0f, 0.0f}});
        geom.vertices.push_back({{ex, -ey, ez}, {1.0f, 0.0f}, {0.0f, -1.0f, 0.0f}});
        geom.vertices.push_back({{ex, -ey, -ez}, {1.0f, 1.0f}, {0.0f, -1.0f, 0.0f}});
        // +Z (左手系で手前)
        geom.vertices.push_back({{ex, -ey, ez}, {0.0f, 1.0f}, {0.0f, 0.0f, 1.0f}});
        geom.vertices.push_back({{ex, ey, ez}, {0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}});
        geom.vertices.push_back({{-ex, ey, ez}, {1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}});
        geom.vertices.push_back({{-ex, -ey, ez}, {1.0f, 1.0f}, {0.0f, 0.0f, 1.0f}});
        // -Z (左手系で奥)
        geom.vertices.push_back({{-ex, -ey, -ez}, {0.0f, 1.0f}, {0.0f, 0.0f, -1.0f}});
        geom.vertices.push_back({{-ex, ey, -ez}, {0.0f, 0.0f}, {0.0f, 0.0f, -1.0f}});
        geom.vertices.push_back({{ex, ey, -ez}, {1.0f, 0.0f}, {0.0f, 0.0f, -1.0f}});
        geom.vertices.push_back({{ex, -ey, -ez}, {1.0f, 1.0f}, {0.0f, 0.0f, -1.0f}});

        // CubeScene の修正後 winding 踏襲 (CW=front、X/Y は反転済、Z は元のまま)
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

    MeshGeometry MakePlane(const NS::Math::Vector2& extents)
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
        // 上から見て CW = front 外向き (上面)
        geom.indices = {0, 1, 2, 0, 2, 3};
        return geom;
    }

    MeshGeometry MakeWedge(float angleDegrees, const NS::Math::Vector3& extents)
    {
        const float ex = extents.x;
        const float ey = extents.y;
        const float ez = extents.z;

        // 高さ = tan(angle) × 底面奥行 (2 * ez)、 ただし extents.y * 2 を上限にクランプ
        constexpr float kPi = 3.14159265358979323846f;
        const float angleRad = angleDegrees * (kPi / 180.0f);
        const float rawHeight = std::tan(angleRad) * (2.0f * ez);
        const float height = std::min(rawHeight, 2.0f * ey);

        const float yBottom = -ey;
        const float yTop = -ey + height;

        // 斜面 normal: (0, cos, -sin) で +Y +,-Z 向き (Z+ 側に登っていく傾斜)
        const float c = std::cos(angleRad);
        const float s = std::sin(angleRad);

        MeshGeometry geom;
        geom.vertices.reserve(18);

        // Slope (top) quad: 4 vertex、 normal (0, c, -s)
        // 順序: lowLeft(-ex,yBottom,-ez) → lowRight(ex,yBottom,-ez) → highRight(ex,yTop,ez) → highLeft(-ex,yTop,ez)
        geom.vertices.push_back({{-ex, yBottom, -ez}, {0.0f, 1.0f}, {0.0f, c, -s}});
        geom.vertices.push_back({{ex, yBottom, -ez}, {1.0f, 1.0f}, {0.0f, c, -s}});
        geom.vertices.push_back({{ex, yTop, ez}, {1.0f, 0.0f}, {0.0f, c, -s}});
        geom.vertices.push_back({{-ex, yTop, ez}, {0.0f, 0.0f}, {0.0f, c, -s}});

        // Bottom quad: y = yBottom 面、 normal (0, -1, 0)
        geom.vertices.push_back({{-ex, yBottom, -ez}, {0.0f, 0.0f}, {0.0f, -1.0f, 0.0f}});
        geom.vertices.push_back({{-ex, yBottom, ez}, {0.0f, 1.0f}, {0.0f, -1.0f, 0.0f}});
        geom.vertices.push_back({{ex, yBottom, ez}, {1.0f, 1.0f}, {0.0f, -1.0f, 0.0f}});
        geom.vertices.push_back({{ex, yBottom, -ez}, {1.0f, 0.0f}, {0.0f, -1.0f, 0.0f}});

        // Front (+Z) quad: 垂直壁、 normal (0, 0, 1)。 高さ = height
        geom.vertices.push_back({{-ex, yBottom, ez}, {0.0f, 1.0f}, {0.0f, 0.0f, 1.0f}});
        geom.vertices.push_back({{-ex, yTop, ez}, {0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}});
        geom.vertices.push_back({{ex, yTop, ez}, {1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}});
        geom.vertices.push_back({{ex, yBottom, ez}, {1.0f, 1.0f}, {0.0f, 0.0f, 1.0f}});

        // Left (-X) triangle: 3 vertex、 normal (-1, 0, 0)
        // 頂点: lowBack (-ex, yBottom, -ez), lowFront (-ex, yBottom, ez), highFront (-ex, yTop, ez)
        geom.vertices.push_back({{-ex, yBottom, -ez}, {0.0f, 1.0f}, {-1.0f, 0.0f, 0.0f}});
        geom.vertices.push_back({{-ex, yBottom, ez}, {1.0f, 1.0f}, {-1.0f, 0.0f, 0.0f}});
        geom.vertices.push_back({{-ex, yTop, ez}, {1.0f, 0.0f}, {-1.0f, 0.0f, 0.0f}});

        // Right (+X) triangle: 3 vertex、 normal (1, 0, 0)
        geom.vertices.push_back({{ex, yBottom, -ez}, {0.0f, 1.0f}, {1.0f, 0.0f, 0.0f}});
        geom.vertices.push_back({{ex, yTop, ez}, {1.0f, 0.0f}, {1.0f, 0.0f, 0.0f}});
        geom.vertices.push_back({{ex, yBottom, ez}, {1.0f, 1.0f}, {1.0f, 0.0f, 0.0f}});

        // インデックス: 四角形 3 × 6 + 三角形 2 × 3 = 24 個
        // CW = front 外向き (MakeCube convention 踏襲)
        geom.indices = {
            // Slope quad (vertex 0..3): 外向きは +Y, -Z 方向 — 上面から見て winding CW
            // 順序 lowLeft → highLeft → highRight、 lowLeft → highRight → lowRight
            0,
            3,
            2,
            0,
            2,
            1,
            // Bottom quad (vertex 4..7): 法線 -Y。下から見ると 4→5→6→7 が CCW のため
            // CW になるよう 4,7,6,4,6,5 で張る
            4,
            7,
            6,
            4,
            6,
            5,
            // Back (+Z) quad (vertex 8..11): 外向き +Z で見える winding
            // 8=(-ex,yBottom,ez), 9=(-ex,yTop,ez), 10=(ex,yTop,ez), 11=(ex,yBottom,ez)
            8,
            10,
            9,
            8,
            11,
            10,
            // Left (-X) triangle (vertex 12..14): 外向き -X で見える winding
            // 12=(-ex,yBottom,-ez), 13=(-ex,yBottom,ez), 14=(-ex,yTop,ez)
            12,
            13,
            14,
            // Right (+X) triangle (vertex 15..17): 外向き +X、 +X 側から見て CW
            // 15=(ex,yBottom,-ez), 16=(ex,yTop,ez), 17=(ex,yBottom,ez)
            15,
            16,
            17,
        };

        return geom;
    }

    MeshGeometry MakeCylinder(float radius, float height, int segments)
    {
        if (segments < 3)
            segments = 3;
        if (radius < 0.0f)
            radius = 0.0f;
        if (height < 0.0f)
            height = 0.0f;

        constexpr float kTwoPi = 2.0f * 3.14159265358979323846f;
        const float halfH = height * 0.5f;

        MeshGeometry geom;
        geom.vertices.reserve(static_cast<std::size_t>(segments) * 4u + 2u);
        geom.indices.reserve(static_cast<std::size_t>(segments) * 12u);

        // Top / bottom cap の中心。 cap の per-vertex normal は (0, +1, 0) / (0, -1, 0)
        const std::uint32_t topCenterIdx = static_cast<std::uint32_t>(geom.vertices.size());
        geom.vertices.push_back({{0.0f, halfH, 0.0f}, {0.5f, 0.5f}, {0.0f, 1.0f, 0.0f}});
        const std::uint32_t bottomCenterIdx = static_cast<std::uint32_t>(geom.vertices.size());
        geom.vertices.push_back({{0.0f, -halfH, 0.0f}, {0.5f, 0.5f}, {0.0f, -1.0f, 0.0f}});

        // 側面の per-segment vertex を 4 個ずつ発行する (segments × 4)
        // top cap 用 / bottom cap 用 / 側面の top / 側面の bottom を別 vertex にして per-face normal を許す
        const std::size_t sideStartIdx = geom.vertices.size();
        for (int i = 0; i < segments; ++i)
        {
            const float t = static_cast<float>(i) / static_cast<float>(segments);
            const float angle = t * kTwoPi;
            const float cx = std::cos(angle);
            const float cz = std::sin(angle);
            const float x = cx * radius;
            const float z = cz * radius;
            const float u = t;

            // top cap 用 (normal +Y)
            geom.vertices.push_back({{x, halfH, z}, {0.5f + 0.5f * cx, 0.5f - 0.5f * cz}, {0.0f, 1.0f, 0.0f}});
            // bottom cap 用 (normal -Y)
            geom.vertices.push_back({{x, -halfH, z}, {0.5f + 0.5f * cx, 0.5f + 0.5f * cz}, {0.0f, -1.0f, 0.0f}});
            // 側面 top (radial normal)
            geom.vertices.push_back({{x, halfH, z}, {u, 0.0f}, {cx, 0.0f, cz}});
            // 側面 bottom (radial normal)
            geom.vertices.push_back({{x, -halfH, z}, {u, 1.0f}, {cx, 0.0f, cz}});
        }

        // Index 構築: top cap (fan)、 bottom cap (fan、 逆 winding)、 side quad (4 vertex/quad)
        for (int i = 0; i < segments; ++i)
        {
            const int next = (i + 1) % segments;
            const std::uint32_t topI = static_cast<std::uint32_t>(sideStartIdx + i * 4 + 0);
            const std::uint32_t topNext = static_cast<std::uint32_t>(sideStartIdx + next * 4 + 0);
            const std::uint32_t botI = static_cast<std::uint32_t>(sideStartIdx + i * 4 + 1);
            const std::uint32_t botNext = static_cast<std::uint32_t>(sideStartIdx + next * 4 + 1);
            const std::uint32_t sideTopI = static_cast<std::uint32_t>(sideStartIdx + i * 4 + 2);
            const std::uint32_t sideTopNext = static_cast<std::uint32_t>(sideStartIdx + next * 4 + 2);
            const std::uint32_t sideBotI = static_cast<std::uint32_t>(sideStartIdx + i * 4 + 3);
            const std::uint32_t sideBotNext = static_cast<std::uint32_t>(sideStartIdx + next * 4 + 3);

            // top cap: 上から見て CW (MakeCube convention 踏襲)。 center → next → current
            geom.indices.push_back(topCenterIdx);
            geom.indices.push_back(topNext);
            geom.indices.push_back(topI);

            // bottom cap: 下から見て CW。 center → current → next
            geom.indices.push_back(bottomCenterIdx);
            geom.indices.push_back(botI);
            geom.indices.push_back(botNext);

            // 側面 quad: 外側から見て CW。 sideTopI → sideTopNext → sideBotNext → sideBotI
            geom.indices.push_back(sideTopI);
            geom.indices.push_back(sideTopNext);
            geom.indices.push_back(sideBotNext);
            geom.indices.push_back(sideTopI);
            geom.indices.push_back(sideBotNext);
            geom.indices.push_back(sideBotI);
        }

        return geom;
    }

} // namespace NS::Graphics
