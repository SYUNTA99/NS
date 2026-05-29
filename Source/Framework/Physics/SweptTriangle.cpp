#include "Framework/Physics/SweptTriangle.h"

#include <algorithm>
#include <cmath>

namespace
{
    using NS::Core::Vector3;
    using NS::Physics::Triangle;

    [[nodiscard]] float Dot(const Vector3& a, const Vector3& b) noexcept
    {
        return a.x * b.x + a.y * b.y + a.z * b.z;
    }

    [[nodiscard]] Vector3 Cross(const Vector3& a, const Vector3& b) noexcept
    {
        return Vector3{a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
    }

    [[nodiscard]] Vector3 NormalizeSafe(const Vector3& v) noexcept
    {
        const float lenSq = v.x * v.x + v.y * v.y + v.z * v.z;
        if (lenSq <= 1e-12f)
            return Vector3{0.0f, 1.0f, 0.0f};
        const float inv = 1.0f / std::sqrt(lenSq);
        return Vector3{v.x * inv, v.y * inv, v.z * inv};
    }

    /// 接触点 `p` が三角形 `tri` 内に含まれるかを barycentric coordinate で判定する。
    /// 3 軸成分 (u, v, w) が全て [0, 1] 範囲内、 かつ合計 1 で内部接触とみなす。
    [[nodiscard]] bool PointInTriangle(const Vector3& p, const Triangle& tri) noexcept
    {
        const Vector3 v0 = tri.v1 - tri.v0;
        const Vector3 v1 = tri.v2 - tri.v0;
        const Vector3 v2 = p - tri.v0;
        const float d00 = Dot(v0, v0);
        const float d01 = Dot(v0, v1);
        const float d11 = Dot(v1, v1);
        const float d20 = Dot(v2, v0);
        const float d21 = Dot(v2, v1);
        const float denom = d00 * d11 - d01 * d01;
        if (std::abs(denom) < 1e-12f)
            return false;
        const float inv = 1.0f / denom;
        const float v = (d11 * d20 - d01 * d21) * inv;
        const float w = (d00 * d21 - d01 * d20) * inv;
        const float u = 1.0f - v - w;
        constexpr float kEdgeEpsilon = 1e-4f;
        return (u >= -kEdgeEpsilon) && (v >= -kEdgeEpsilon) && (w >= -kEdgeEpsilon);
    }

    /// 単一 sphere (`center` 中心、 `radius` 半径) が `motion` だけ移動したときに
    /// triangle と最初に接触する TOI を返す。 非接触 / 平行 / 離反は false。
    [[nodiscard]] bool SweptSphereVsTriangle(const Vector3& center,
                                             float radius,
                                             const Vector3& motion,
                                             const Triangle& tri,
                                             const Vector3& normal,
                                             float& outToi) noexcept
    {
        const float signedDist = Dot(normal, center - tri.v0);
        // 開始時点ですでに反対側 (normal 逆方向) に居る場合は無視する。 wedge slope を
        // 下からくり抜いて当たる挙動は本実装の想定外 (wedge 底面・ 背面の triangle が別途存在)。
        if (signedDist < -radius)
            return false;

        const float denom = Dot(normal, motion);

        // 開始時点で sphere center が plane から +radius 以下 (接触 or 既にめり込み) なら、
        // 動きが離反方向でない限り、 motion 開始時に既に接触 (TOI = 0) と扱う。
        if (signedDist <= radius)
        {
            // 離反方向 (denom > 0) かつ 1 frame で plane を抜けるなら、 これ以上接触しないので skip。
            if (denom > 0.0f)
            {
                const float separation = signedDist + denom;
                if (separation > radius)
                    return false;
            }
            // sphere 中心を triangle 平面に射影した点が三角形内か確認する。
            const Vector3 projected = center - normal * signedDist;
            if (!PointInTriangle(projected, tri))
                return false;
            outToi = 0.0f;
            return true;
        }

        // 通常の swept 検出: 平面と平行 / 離反する motion は no hit。
        if (denom >= -1e-9f)
            return false;

        const float toi = (radius - signedDist) / denom;
        if (toi < 0.0f || toi > 1.0f)
            return false;

        const Vector3 contactCenter = center + motion * toi;
        const Vector3 contactPoint = contactCenter - normal * radius;
        if (!PointInTriangle(contactPoint, tri))
            return false;

        outToi = toi;
        return true;
    }
} // namespace

namespace NS::Physics
{
    bool SweptCapsuleVsTriangle(const Capsule& capsule,
                                const NS::Core::Vector3& motion,
                                const Triangle& tri,
                                float& outToi,
                                NS::Core::Vector3& outNormal) noexcept
    {
        outToi = 1.0f;
        outNormal = NS::Core::Vector3{0.0f, 0.0f, 0.0f};

        const NS::Core::Vector3 normal = NormalizeSafe(Cross(tri.v1 - tri.v0, tri.v2 - tri.v0));

        // 公開 Capsule 型は axis 単位長を強制しないため、 ここで正規化する (非単位入力で TOI が歪む防止)。
        NS::Core::Vector3 axis = capsule.axis;
        const float axisLenSq = axis.x * axis.x + axis.y * axis.y + axis.z * axis.z;
        if (axisLenSq > 1e-12f)
        {
            const float invLen = 1.0f / std::sqrt(axisLenSq);
            axis.x *= invLen;
            axis.y *= invLen;
            axis.z *= invLen;
        }
        else
        {
            axis = NS::Core::Vector3{0.0f, 1.0f, 0.0f};
        }

        const NS::Core::Vector3 top = capsule.center + axis * capsule.halfHeight;
        const NS::Core::Vector3 bottom = capsule.center - axis * capsule.halfHeight;

        float toiTop = 1.0f;
        float toiBottom = 1.0f;
        const bool hitTop = SweptSphereVsTriangle(top, capsule.radius, motion, tri, normal, toiTop);
        const bool hitBottom = SweptSphereVsTriangle(bottom, capsule.radius, motion, tri, normal, toiBottom);

        if (!hitTop && !hitBottom)
            return false;

        const float toi = (hitTop && (!hitBottom || toiTop <= toiBottom)) ? toiTop : toiBottom;
        outToi = toi;
        outNormal = normal;
        return true;
    }
} // namespace NS::Physics
