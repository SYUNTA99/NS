#include "Runtime/Physics/SweptCapsule.h"

#include <algorithm>
#include <cmath>

namespace
{
    using NS::Core::Dot;
    using NS::Core::Vector3;
    using NS::Core::Sphere;
    using NS::Physics::Capsule;

    [[nodiscard]] Vector3 Normalized(const Vector3& v) noexcept
    {
        const float lenSq = Dot(v, v);
        if (lenSq < 1e-12f)
            return Vector3{0.0f, 0.0f, 0.0f};
        const float inv = 1.0f / std::sqrt(lenSq);
        return Vector3{v.x * inv, v.y * inv, v.z * inv};
    }

    //! 非単位入力でも軸端点が歪まないよう axis を正規化する。 零ベクトルは Y 軸にする
    [[nodiscard]] Vector3 NormalizeAxis(const Vector3& axis) noexcept
    {
        const float lenSq = Dot(axis, axis);
        if (lenSq < 1e-12f)
            return Vector3{0.0f, 1.0f, 0.0f};
        const float inv = 1.0f / std::sqrt(lenSq);
        return Vector3{axis.x * inv, axis.y * inv, axis.z * inv};
    }

    [[nodiscard]] Vector3 ClosestOnSegment(const Vector3& p, const Vector3& a, const Vector3& b) noexcept
    {
        const Vector3 ab = b - a;
        const float denom = Dot(ab, ab);
        if (denom < 1e-12f)
            return a;
        const float s = std::clamp(Dot(p - a, ab) / denom, 0.0f, 1.0f);
        return a + ab * s;
    }

    //! a t^2 + b t + c = 0 の [0,1] 内最小実根を返す。 過去 / 範囲外しか無ければ false
    [[nodiscard]] bool SmallestRoot01(float a, float b, float c, float& outT) noexcept
    {
        if (std::abs(a) < 1e-12f)
        {
            if (std::abs(b) < 1e-12f)
                return false;
            const float t = -c / b;
            if (t < 0.0f || t > 1.0f)
                return false;
            outT = t;
            return true;
        }
        const float disc = b * b - 4.0f * a * c;
        if (disc < 0.0f)
            return false;
        const float sq = std::sqrt(disc);
        const float lo = (-b - sq) / (2.0f * a);
        const float hi = (-b + sq) / (2.0f * a);
        if (lo >= 0.0f && lo <= 1.0f)
        {
            outT = lo;
            return true;
        }
        if (hi >= 0.0f && hi <= 1.0f)
        {
            outT = hi;
            return true;
        }
        return false;
    }

    //! 軸線分 [A,B] と半径 R の capsule に、 t が [0,1] の ray P(t)=O+tD が最初に入る t と、
    //! その時の軸上の最近点 Q を返す。 既に内部なら t=0。 当たらなければ false
    //! 軸直交成分の二次式の無限円柱 + 端 cap 球 2 個 の最小 t を採る
    [[nodiscard]] bool RayVsCapsule(const Vector3& O,
                                    const Vector3& D,
                                    const Vector3& A,
                                    const Vector3& B,
                                    float R,
                                    float& outT,
                                    Vector3& outClosest) noexcept
    {
        const Vector3 startClosest = ClosestOnSegment(O, A, B);
        if (Dot(O - startClosest, O - startClosest) <= R * R)
        {
            outT = 0.0f;
            outClosest = startClosest;
            return true;
        }

        const Vector3 d = B - A;
        const float dd = Dot(d, d);
        bool found = false;
        float bestT = 2.0f;
        Vector3 bestQ{};

        // 無限円柱: O-A と D の軸直交成分 w, u について |w + t u|^2 = R^2
        if (dd > 1e-12f)
        {
            const Vector3 m = O - A;
            const Vector3 u = D - d * (Dot(D, d) / dd);
            const Vector3 w = m - d * (Dot(m, d) / dd);
            const float a = Dot(u, u);
            const float b = 2.0f * Dot(w, u);
            const float c = Dot(w, w) - R * R;
            float t = 0.0f;
            if (SmallestRoot01(a, b, c, t))
            {
                const float s = (Dot(m, d) + t * Dot(D, d)) / dd;
                if (s >= 0.0f && s <= 1.0f && t < bestT)
                {
                    bestT = t;
                    bestQ = A + d * s;
                    found = true;
                }
            }
        }

        // 端 cap 球: ray vs sphere(center, R)
        const Vector3 caps[2] = {A, B};
        for (const Vector3& center : caps)
        {
            const Vector3 oc = O - center;
            const float a = Dot(D, D);
            const float b = 2.0f * Dot(oc, D);
            const float c = Dot(oc, oc) - R * R;
            float t = 0.0f;
            if (SmallestRoot01(a, b, c, t) && t < bestT)
            {
                bestT = t;
                bestQ = center;
                found = true;
            }
        }

        if (!found)
            return false;
        outT = bestT;
        outClosest = bestQ;
        return true;
    }
} // namespace

namespace NS::Physics
{
    bool SweptCapsuleVsSphere(const Capsule& capsule,
                              const NS::Core::Vector3& motion,
                              const Sphere& sphere,
                              float& outToi,
                              NS::Core::Vector3& outNormal) noexcept
    {
        outToi = 1.0f;
        outNormal = NS::Core::Vector3{0.0f, 0.0f, 0.0f};

        const Vector3 axis = NormalizeAxis(capsule.axis);
        const Vector3 a = capsule.center - axis * capsule.halfHeight;
        const Vector3 b = capsule.center + axis * capsule.halfHeight;
        const float r = capsule.radius + sphere.radius;

        // 相対系: capsule を静止させ、 球中心を -motion で動かす ray とみなして解析解に帰着する
        const Vector3 relDir{-motion.x, -motion.y, -motion.z};
        float t = 1.0f;
        Vector3 core{};
        if (!RayVsCapsule(sphere.center, relDir, a, b, r, t, core))
            return false;

        const Vector3 sphereAtContact = sphere.center - motion * t;
        outToi = t;
        outNormal = Normalized(core - sphereAtContact);
        return true;
    }

    bool SweptCapsuleVsCapsule(const Capsule& capsule,
                               const NS::Core::Vector3& motion,
                               const Capsule& other,
                               float& outToi,
                               NS::Core::Vector3& outNormal) noexcept
    {
        outToi = 1.0f;
        outNormal = NS::Core::Vector3{0.0f, 0.0f, 0.0f};

        const Vector3 axisSelf = NormalizeAxis(capsule.axis);
        const Vector3 selfBottom = capsule.center - axisSelf * capsule.halfHeight;
        const Vector3 selfTop = capsule.center + axisSelf * capsule.halfHeight;

        const Vector3 axisOther = NormalizeAxis(other.axis);
        const Vector3 otherBottom = other.center - axisOther * other.halfHeight;
        const Vector3 otherTop = other.center + axisOther * other.halfHeight;
        const float r = capsule.radius + other.radius;

        const Vector3 ends[2] = {selfBottom, selfTop};
        bool hit = false;
        float bestT = 1.0f;
        Vector3 bestNormal{};
        for (const Vector3& end : ends)
        {
            float t = 1.0f;
            Vector3 core{};
            if (RayVsCapsule(end, motion, otherBottom, otherTop, r, t, core) && t <= bestT)
            {
                bestT = t;
                const Vector3 endAtContact = end + motion * t;
                bestNormal = Normalized(endAtContact - core);
                hit = true;
            }
        }
        if (!hit)
            return false;
        outToi = bestT;
        outNormal = bestNormal;
        return true;
    }
} // namespace NS::Physics
