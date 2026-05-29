#include "Framework/Graphics/MeshPrimitives.h"

#include <algorithm>
#include <cmath>

namespace NS::Graphics
{
    MeshGeometry MakeCube(const NS::Core::Vector3& extents)
    {
        const float ex = extents.x;
        const float ey = extents.y;
        const float ez = extents.z;

        MeshGeometry geom;
        geom.vertices.reserve(24);

        // +X (right)
        geom.vertices.push_back({{ex, -ey, ez}, {0.0f, 1.0f}, {1.0f, 0.0f, 0.0f}});
        geom.vertices.push_back({{ex, ey, ez}, {0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}});
        geom.vertices.push_back({{ex, ey, -ez}, {1.0f, 0.0f}, {1.0f, 0.0f, 0.0f}});
        geom.vertices.push_back({{ex, -ey, -ez}, {1.0f, 1.0f}, {1.0f, 0.0f, 0.0f}});
        // -X (left)
        geom.vertices.push_back({{-ex, -ey, -ez}, {0.0f, 1.0f}, {-1.0f, 0.0f, 0.0f}});
        geom.vertices.push_back({{-ex, ey, -ez}, {0.0f, 0.0f}, {-1.0f, 0.0f, 0.0f}});
        geom.vertices.push_back({{-ex, ey, ez}, {1.0f, 0.0f}, {-1.0f, 0.0f, 0.0f}});
        geom.vertices.push_back({{-ex, -ey, ez}, {1.0f, 1.0f}, {-1.0f, 0.0f, 0.0f}});
        // +Y (top)
        geom.vertices.push_back({{-ex, ey, ez}, {0.0f, 1.0f}, {0.0f, 1.0f, 0.0f}});
        geom.vertices.push_back({{-ex, ey, -ez}, {0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}});
        geom.vertices.push_back({{ex, ey, -ez}, {1.0f, 0.0f}, {0.0f, 1.0f, 0.0f}});
        geom.vertices.push_back({{ex, ey, ez}, {1.0f, 1.0f}, {0.0f, 1.0f, 0.0f}});
        // -Y (bottom)
        geom.vertices.push_back({{-ex, -ey, -ez}, {0.0f, 1.0f}, {0.0f, -1.0f, 0.0f}});
        geom.vertices.push_back({{-ex, -ey, ez}, {0.0f, 0.0f}, {0.0f, -1.0f, 0.0f}});
        geom.vertices.push_back({{ex, -ey, ez}, {1.0f, 0.0f}, {0.0f, -1.0f, 0.0f}});
        geom.vertices.push_back({{ex, -ey, -ez}, {1.0f, 1.0f}, {0.0f, -1.0f, 0.0f}});
        // +Z (front in LH)
        geom.vertices.push_back({{ex, -ey, ez}, {0.0f, 1.0f}, {0.0f, 0.0f, 1.0f}});
        geom.vertices.push_back({{ex, ey, ez}, {0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}});
        geom.vertices.push_back({{-ex, ey, ez}, {1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}});
        geom.vertices.push_back({{-ex, -ey, ez}, {1.0f, 1.0f}, {0.0f, 0.0f, 1.0f}});
        // -Z (back in LH)
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

    MeshGeometry MakePlane(const NS::Core::Vector2& extents)
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

    MeshGeometry MakeWedge(float angleDegrees, const NS::Core::Vector3& extents)
    {
        const float ex = extents.x;
        const float ey = extents.y;
        const float ez = extents.z;

        // 高さ = tan(angle) × 底面奥行 (2 * ez)、 ただし extents.y * 2 を上限にクランプ。
        constexpr float kPi = 3.14159265358979323846f;
        const float angleRad = angleDegrees * (kPi / 180.0f);
        const float rawHeight = std::tan(angleRad) * (2.0f * ez);
        const float height = std::min(rawHeight, 2.0f * ey);

        const float yBottom = -ey;
        const float yTop = -ey + height;

        // 斜面 normal: (0, cos, -sin) で +Y +,-Z 向き (Z+ 側に登っていく傾斜)。
        const float c = std::cos(angleRad);
        const float s = std::sin(angleRad);

        MeshGeometry geom;
        geom.vertices.reserve(18);

        // Slope (top) quad: 4 vertex、 normal (0, c, -s)。
        // 順序: lowLeft (-ex, yBottom, -ez), lowRight (ex, yBottom, -ez),
        //       highRight (ex, yTop, ez), highLeft (-ex, yTop, ez)
        geom.vertices.push_back({{-ex, yBottom, -ez}, {0.0f, 1.0f}, {0.0f, c, -s}});
        geom.vertices.push_back({{ex, yBottom, -ez}, {1.0f, 1.0f}, {0.0f, c, -s}});
        geom.vertices.push_back({{ex, yTop, ez}, {1.0f, 0.0f}, {0.0f, c, -s}});
        geom.vertices.push_back({{-ex, yTop, ez}, {0.0f, 0.0f}, {0.0f, c, -s}});

        // Bottom quad: y = yBottom 面、 normal (0, -1, 0)。
        geom.vertices.push_back({{-ex, yBottom, -ez}, {0.0f, 0.0f}, {0.0f, -1.0f, 0.0f}});
        geom.vertices.push_back({{-ex, yBottom, ez}, {0.0f, 1.0f}, {0.0f, -1.0f, 0.0f}});
        geom.vertices.push_back({{ex, yBottom, ez}, {1.0f, 1.0f}, {0.0f, -1.0f, 0.0f}});
        geom.vertices.push_back({{ex, yBottom, -ez}, {1.0f, 0.0f}, {0.0f, -1.0f, 0.0f}});

        // Front (+Z) quad: 垂直壁、 normal (0, 0, 1)。 高さ = height。
        geom.vertices.push_back({{-ex, yBottom, ez}, {0.0f, 1.0f}, {0.0f, 0.0f, 1.0f}});
        geom.vertices.push_back({{-ex, yTop, ez}, {0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}});
        geom.vertices.push_back({{ex, yTop, ez}, {1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}});
        geom.vertices.push_back({{ex, yBottom, ez}, {1.0f, 1.0f}, {0.0f, 0.0f, 1.0f}});

        // Left (-X) triangle: 3 vertex、 normal (-1, 0, 0)。
        // 頂点: lowBack (-ex, yBottom, -ez), lowFront (-ex, yBottom, ez), highFront (-ex, yTop, ez)
        geom.vertices.push_back({{-ex, yBottom, -ez}, {0.0f, 1.0f}, {-1.0f, 0.0f, 0.0f}});
        geom.vertices.push_back({{-ex, yBottom, ez}, {1.0f, 1.0f}, {-1.0f, 0.0f, 0.0f}});
        geom.vertices.push_back({{-ex, yTop, ez}, {1.0f, 0.0f}, {-1.0f, 0.0f, 0.0f}});

        // Right (+X) triangle: 3 vertex、 normal (1, 0, 0)。
        geom.vertices.push_back({{ex, yBottom, -ez}, {0.0f, 1.0f}, {1.0f, 0.0f, 0.0f}});
        geom.vertices.push_back({{ex, yTop, ez}, {1.0f, 0.0f}, {1.0f, 0.0f, 0.0f}});
        geom.vertices.push_back({{ex, yBottom, ez}, {1.0f, 1.0f}, {1.0f, 0.0f, 0.0f}});

        // Index buffer: 3 quads × 6 + 2 triangles × 3 = 24 indices。
        // CW = front 外向き (MakeCube convention 踏襲)。
        geom.indices = {
            // Slope quad (vertex 0..3): 外向きは +Y, -Z 方向 — 上面から見て winding CW。
            // 順序 lowLeft → highLeft → highRight、 lowLeft → highRight → lowRight。
            0,
            3,
            2,
            0,
            2,
            1,
            // Bottom quad (vertex 4..7): 下面、 法線 -Y、 下から見て CW にしたい。
            // -ex,-ez → -ex,ez → ex,ez → ex,-ez の順で並んでいる。 下から見ると 4→5→6→7 で
            // CCW なので、 CW にするため 4,7,6,4,6,5。
            4,
            7,
            6,
            4,
            6,
            5,
            // Front (+Z) quad (vertex 8..11): 外向き +Z、 前から見て CW。
            // 8=(-ex,yBottom,ez), 9=(-ex,yTop,ez), 10=(ex,yTop,ez), 11=(ex,yBottom,ez)
            8,
            9,
            10,
            8,
            10,
            11,
            // Left (-X) triangle (vertex 12..14): 外向き -X、 -X 側から見て CW。
            // 12=(-ex,yBottom,-ez), 13=(-ex,yBottom,ez), 14=(-ex,yTop,ez)
            12,
            14,
            13,
            // Right (+X) triangle (vertex 15..17): 外向き +X、 +X 側から見て CW。
            // 15=(ex,yBottom,-ez), 16=(ex,yTop,ez), 17=(ex,yBottom,ez)
            15,
            16,
            17,
        };

        return geom;
    }
} // namespace NS::Graphics
