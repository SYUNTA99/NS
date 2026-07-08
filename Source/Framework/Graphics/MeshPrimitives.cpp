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

        // +X 右面
        geom.vertices.push_back({{ex, -ey, ez}, {0.0f, 1.0f}, {1.0f, 0.0f, 0.0f}});
        geom.vertices.push_back({{ex, ey, ez}, {0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}});
        geom.vertices.push_back({{ex, ey, -ez}, {1.0f, 0.0f}, {1.0f, 0.0f, 0.0f}});
        geom.vertices.push_back({{ex, -ey, -ez}, {1.0f, 1.0f}, {1.0f, 0.0f, 0.0f}});
        // -X 左面
        geom.vertices.push_back({{-ex, -ey, -ez}, {0.0f, 1.0f}, {-1.0f, 0.0f, 0.0f}});
        geom.vertices.push_back({{-ex, ey, -ez}, {0.0f, 0.0f}, {-1.0f, 0.0f, 0.0f}});
        geom.vertices.push_back({{-ex, ey, ez}, {1.0f, 0.0f}, {-1.0f, 0.0f, 0.0f}});
        geom.vertices.push_back({{-ex, -ey, ez}, {1.0f, 1.0f}, {-1.0f, 0.0f, 0.0f}});
        // +Y 上面
        geom.vertices.push_back({{-ex, ey, ez}, {0.0f, 1.0f}, {0.0f, 1.0f, 0.0f}});
        geom.vertices.push_back({{-ex, ey, -ez}, {0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}});
        geom.vertices.push_back({{ex, ey, -ez}, {1.0f, 0.0f}, {0.0f, 1.0f, 0.0f}});
        geom.vertices.push_back({{ex, ey, ez}, {1.0f, 1.0f}, {0.0f, 1.0f, 0.0f}});
        // -Y 下面
        geom.vertices.push_back({{-ex, -ey, -ez}, {0.0f, 1.0f}, {0.0f, -1.0f, 0.0f}});
        geom.vertices.push_back({{-ex, -ey, ez}, {0.0f, 0.0f}, {0.0f, -1.0f, 0.0f}});
        geom.vertices.push_back({{ex, -ey, ez}, {1.0f, 0.0f}, {0.0f, -1.0f, 0.0f}});
        geom.vertices.push_back({{ex, -ey, -ez}, {1.0f, 1.0f}, {0.0f, -1.0f, 0.0f}});
        // +Z 左手系で手前
        geom.vertices.push_back({{ex, -ey, ez}, {0.0f, 1.0f}, {0.0f, 0.0f, 1.0f}});
        geom.vertices.push_back({{ex, ey, ez}, {0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}});
        geom.vertices.push_back({{-ex, ey, ez}, {1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}});
        geom.vertices.push_back({{-ex, -ey, ez}, {1.0f, 1.0f}, {0.0f, 0.0f, 1.0f}});
        // -Z 左手系で奥
        geom.vertices.push_back({{-ex, -ey, -ez}, {0.0f, 1.0f}, {0.0f, 0.0f, -1.0f}});
        geom.vertices.push_back({{-ex, ey, -ez}, {0.0f, 0.0f}, {0.0f, 0.0f, -1.0f}});
        geom.vertices.push_back({{ex, ey, -ez}, {1.0f, 0.0f}, {0.0f, 0.0f, -1.0f}});
        geom.vertices.push_back({{ex, -ey, -ez}, {1.0f, 1.0f}, {0.0f, 0.0f, -1.0f}});

        // CubeScene の修正後 winding 踏襲。CW=front、X/Y は反転済、Z は元のまま
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
        // 上から見て CW = front 外向きの上面
        geom.indices = {0, 1, 2, 0, 2, 3};
        return geom;
    }

    MeshGeometry MakeWedge(float angleDegrees, const NS::Math::Vector3& extents)
    {
        const float ex = extents.x;
        const float ey = extents.y;
        const float ez = extents.z;

        // 高さ = tan(angle) × 底面奥行 2 * ez。ただし extents.y * 2 を上限にクランプ
        constexpr float kPi = 3.14159265358979323846f;
        const float angleRad = angleDegrees * (kPi / 180.0f);
        const float rawHeight = std::tan(angleRad) * (2.0f * ez);
        const float height = std::min(rawHeight, 2.0f * ey);

        const float yBottom = -ey;
        const float yTop = -ey + height;

        // 斜面 normal: (0, cos, -sin) で +Y +,-Z 向き。Z+ 側に登っていく傾斜
        const float c = std::cos(angleRad);
        const float s = std::sin(angleRad);

        MeshGeometry geom;
        geom.vertices.reserve(18);

        // Slope top quad: 4 vertex、 normal (0, c, -s)
        geom.vertices.push_back({{-ex, yBottom, -ez}, {0.0f, 1.0f}, {0.0f, c, -s}});
        geom.vertices.push_back({{ex, yBottom, -ez}, {1.0f, 1.0f}, {0.0f, c, -s}});
        geom.vertices.push_back({{ex, yTop, ez}, {1.0f, 0.0f}, {0.0f, c, -s}});
        geom.vertices.push_back({{-ex, yTop, ez}, {0.0f, 0.0f}, {0.0f, c, -s}});

        // Bottom quad: y = yBottom 面、 normal (0, -1, 0)
        geom.vertices.push_back({{-ex, yBottom, -ez}, {0.0f, 0.0f}, {0.0f, -1.0f, 0.0f}});
        geom.vertices.push_back({{-ex, yBottom, ez}, {0.0f, 1.0f}, {0.0f, -1.0f, 0.0f}});
        geom.vertices.push_back({{ex, yBottom, ez}, {1.0f, 1.0f}, {0.0f, -1.0f, 0.0f}});
        geom.vertices.push_back({{ex, yBottom, -ez}, {1.0f, 0.0f}, {0.0f, -1.0f, 0.0f}});

        // Front +Z quad: 垂直壁、 normal (0, 0, 1)。 高さ = height
        geom.vertices.push_back({{-ex, yBottom, ez}, {0.0f, 1.0f}, {0.0f, 0.0f, 1.0f}});
        geom.vertices.push_back({{-ex, yTop, ez}, {0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}});
        geom.vertices.push_back({{ex, yTop, ez}, {1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}});
        geom.vertices.push_back({{ex, yBottom, ez}, {1.0f, 1.0f}, {0.0f, 0.0f, 1.0f}});

        // Left -X triangle: 3 vertex、 normal (-1, 0, 0)
        geom.vertices.push_back({{-ex, yBottom, -ez}, {0.0f, 1.0f}, {-1.0f, 0.0f, 0.0f}});
        geom.vertices.push_back({{-ex, yBottom, ez}, {1.0f, 1.0f}, {-1.0f, 0.0f, 0.0f}});
        geom.vertices.push_back({{-ex, yTop, ez}, {1.0f, 0.0f}, {-1.0f, 0.0f, 0.0f}});

        // Right +X triangle: 3 vertex、 normal (1, 0, 0)
        geom.vertices.push_back({{ex, yBottom, -ez}, {0.0f, 1.0f}, {1.0f, 0.0f, 0.0f}});
        geom.vertices.push_back({{ex, yTop, ez}, {1.0f, 0.0f}, {1.0f, 0.0f, 0.0f}});
        geom.vertices.push_back({{ex, yBottom, ez}, {1.0f, 1.0f}, {1.0f, 0.0f, 0.0f}});

        // インデックス: 四角形 3 × 6 + 三角形 2 × 3 = 24 個
        // CW = front 外向き。MakeCube の convention を踏襲
        geom.indices = {
            // Slope quad の vertex 0..3 は外向き +Y, -Z 方向、 上面から見て winding CW
            0,
            3,
            2,
            0,
            2,
            1,
            // Bottom quad の vertex 4..7 は法線 -Y、 下から見ると 4→5→6→7 が CCW なので CW になるよう 4,7,6,4,6,5
            // で張る
            4,
            7,
            6,
            4,
            6,
            5,
            // Back +Z quad の vertex 8..11 は外向き +Z で見える winding
            8,
            10,
            9,
            8,
            11,
            10,
            // Left -X triangle の vertex 12..14 は外向き -X で見える winding
            12,
            13,
            14,
            // Right +X triangle の vertex 15..17 は外向き +X、 +X 側から見て CW
            15,
            16,
            17,
        };

        return geom;
    }

} // namespace NS::Graphics
