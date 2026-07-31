#include "Runtime/Physics/SweptTriangle.h"

#include <cmath>

namespace
{
    using NS::Math::Cross;
    using NS::Math::Dot;
    using NS::Math::Vector3;
    using NS::Physics::Capsule;
    using NS::Physics::Triangle;

    [[nodiscard]] Vector3 NormalizeSafe(const Vector3& v) noexcept
    {
        const float lenSq = v.x * v.x + v.y * v.y + v.z * v.z;
        if (lenSq <= 1e-12f)
            return Vector3{0.0f, 1.0f, 0.0f};
        const float inv = 1.0f / std::sqrt(lenSq);
        return Vector3{v.x * inv, v.y * inv, v.z * inv};
    }

    /// 接触点 `p` が三角形 `tri` 内に含まれるかを barycentric coordinate で判定する
    /// u, v, w の 3 軸成分が全て [0, 1] 範囲内、 かつ合計 1 で内部接触とみなす
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
        constexpr float k_EdgeEpsilon = 1e-4f;
        return (u >= -k_EdgeEpsilon) && (v >= -k_EdgeEpsilon) && (w >= -k_EdgeEpsilon);
    }

    /// `center` 中心、 `radius` 半径の単一 sphere が `motion` だけ移動したときに
    /// triangle と最初に接触する TOI を返す。 非接触 / 平行 / 離反は false
    [[nodiscard]] bool SweptSphereVsTriangle(const Vector3& center,
                                             float radius,
                                             const Vector3& motion,
                                             const Triangle& tri,
                                             const Vector3& normal,
                                             float& outToi) noexcept
    {
        const float signedDist = Dot(normal, center - tri.v0);
        // 開始時点ですでに normal 逆方向の反対側に居る場合は無視する。 wedge slope を
        // 下からくり抜いて当たる挙動は本実装の想定外で wedge 底面・ 背面の triangle が別途存在する
        if (signedDist < -radius)
            return false;

        const float denom = Dot(normal, motion);

        // 開始時点で sphere center が plane から +radius 以下で接触または既にめり込んでいるなら、
        // 動きが離反方向でない限り、 motion 開始時に既に接触して TOI = 0 と扱う
        if (signedDist <= radius)
        {
            // denom > 0 の離反方向で 1 frame で plane を抜けるなら、 これ以上接触しないので skip
            if (denom > 0.0f)
            {
                const float separation = signedDist + denom;
                if (separation > radius)
                    return false;
            }
            const Vector3 projected = center - normal * signedDist;
            if (!PointInTriangle(projected, tri))
                return false;
            outToi = 0.0f;
            return true;
        }

        // 通常の swept 検出: 平面と平行 / 離反する motion は no hit
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
                                const NS::Math::Vector3& motion,
                                const Triangle& tri,
                                float& outToi,
                                NS::Math::Vector3& outNormal) noexcept
    {
        outToi = 1.0f;
        outNormal = NS::Math::Vector3{0.0f, 0.0f, 0.0f};

        const NS::Math::Vector3 normal = NormalizeSafe(Cross(tri.v1 - tri.v0, tri.v2 - tri.v0));

        const auto [top, bottom] = CapsuleEndpoints(capsule);

        float toiTop = 1.0f;
        float toiBottom = 1.0f;
        const bool hitTop = SweptSphereVsTriangle(top, capsule.radius, motion, tri, normal, toiTop);
        const bool hitBottom = SweptSphereVsTriangle(bottom, capsule.radius, motion, tri, normal, toiBottom);

        if (!hitTop && !hitBottom)
            return false;

        const float toi = [&]() -> float {
            if (hitTop && (!hitBottom || toiTop <= toiBottom))
                return toiTop;
            return toiBottom;
        }();
        outToi = toi;
        outNormal = normal;
        return true;
    }
} // namespace NS::Physics
