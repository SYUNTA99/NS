#include "NSlib/Object/Components/HitSensor.h"

#include "NSlib/Core/Logger.h"
#include "NSlib/Object/Actor.h"
#include "NSlib/Object/Reflection/TypeRegistry.h"
#include "NSlib/Object/Scene/HitSensorDirector.h"
#include "NSlib/Object/Scene/Scene.h"
#include "NSlib/Physics/Capsule.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

namespace NS::Obj
{
    namespace
    {
        // 線分と線分の最も近い点どうしの距離の 2 乗。Ericson の Real-Time Collision Detection 5.1.9 の手順
        [[nodiscard]] float SegmentSegmentDistanceSq(const Vector3& p1,
                                                     const Vector3& q1,
                                                     const Vector3& p2,
                                                     const Vector3& q2) noexcept
        {
            constexpr float k_Tiny = 1.0e-12f;
            const Vector3 d1 = q1 - p1;
            const Vector3 d2 = q2 - p2;
            const Vector3 r = p1 - p2;
            const float a = d1.Dot(d1);
            const float e = d2.Dot(d2);
            const float f = d2.Dot(r);
            float s = 0.0f;
            float t = 0.0f;
            if (a <= k_Tiny && e <= k_Tiny)
            {
                return r.Dot(r);
            }
            if (a <= k_Tiny)
            {
                t = std::clamp(f / e, 0.0f, 1.0f);
            }
            else
            {
                const float c = d1.Dot(r);
                if (e <= k_Tiny)
                {
                    s = std::clamp(-c / a, 0.0f, 1.0f);
                }
                else
                {
                    const float b = d1.Dot(d2);
                    const float denom = a * e - b * b;
                    if (denom > k_Tiny)
                    {
                        s = std::clamp((b * f - c * e) / denom, 0.0f, 1.0f);
                    }
                    t = (b * s + f) / e;
                    if (t < 0.0f)
                    {
                        t = 0.0f;
                        s = std::clamp(-c / a, 0.0f, 1.0f);
                    }
                    else if (t > 1.0f)
                    {
                        t = 1.0f;
                        s = std::clamp((b - c) / a, 0.0f, 1.0f);
                    }
                }
            }
            const Vector3 c1 = p1 + d1 * s;
            const Vector3 c2 = p2 + d2 * t;
            const Vector3 diff = c1 - c2;
            return diff.Dot(diff);
        }

        // 点から向きのある箱までの距離の 2 乗。中なら 0
        [[nodiscard]] float PointBoxDistanceSq(const Vector3& point, const NS::OBB& box) noexcept
        {
            const Vector3 d = point - box.center;
            const std::array<Vector3, 3> axes{box.axisX, box.axisY, box.axisZ};
            const std::array<float, 3> halves{box.halfExtentX, box.halfExtentY, box.halfExtentZ};
            float distanceSq = 0.0f;
            for (std::size_t i = 0; i < 3; ++i)
            {
                const float along = d.Dot(axes[i]);
                const float excess = std::abs(along) - halves[i];
                if (excess > 0.0f)
                {
                    distanceSq += excess * excess;
                }
            }
            return distanceSq;
        }

        // 線分から向きのある箱までの距離の 2 乗
        // 点から凸な形までの距離は線分に沿って凸なので、三分探索で最小を詰める
        [[nodiscard]] float SegmentBoxDistanceSq(const Vector3& a, const Vector3& b, const NS::OBB& box) noexcept
        {
            constexpr int k_Steps = 40;
            float lo = 0.0f;
            float hi = 1.0f;
            for (int i = 0; i < k_Steps; ++i)
            {
                const float m1 = lo + (hi - lo) / 3.0f;
                const float m2 = hi - (hi - lo) / 3.0f;
                const float d1 = PointBoxDistanceSq(a + (b - a) * m1, box);
                const float d2 = PointBoxDistanceSq(a + (b - a) * m2, box);
                if (d1 <= d2)
                {
                    hi = m2;
                }
                else
                {
                    lo = m1;
                }
            }
            const float t = (lo + hi) * 0.5f;
            return std::min(
                {PointBoxDistanceSq(a + (b - a) * t, box), PointBoxDistanceSq(a, box), PointBoxDistanceSq(b, box)});
        }

        // 分離軸の定理で向きのある箱どうしの重なりを見る。15 本の軸のどれかで離れていれば重ならない
        [[nodiscard]] bool BoxesOverlap(const NS::OBB& lhs, const NS::OBB& rhs) noexcept
        {
            const std::array<Vector3, 3> a{lhs.axisX, lhs.axisY, lhs.axisZ};
            const std::array<Vector3, 3> b{rhs.axisX, rhs.axisY, rhs.axisZ};
            const std::array<float, 3> ha{lhs.halfExtentX, lhs.halfExtentY, lhs.halfExtentZ};
            const std::array<float, 3> hb{rhs.halfExtentX, rhs.halfExtentY, rhs.halfExtentZ};
            const Vector3 t = rhs.center - lhs.center;

            const auto separated = [&](const Vector3& axis) {
                if (axis.LengthSquared() < 1.0e-10f)
                {
                    return false; // 平行な辺の外積。他の軸が受け持つ
                }
                float ra = 0.0f;
                float rb = 0.0f;
                for (std::size_t i = 0; i < 3; ++i)
                {
                    ra += ha[i] * std::abs(a[i].Dot(axis));
                    rb += hb[i] * std::abs(b[i].Dot(axis));
                }
                return std::abs(t.Dot(axis)) > ra + rb;
            };

            for (std::size_t i = 0; i < 3; ++i)
            {
                if (separated(a[i]) || separated(b[i]))
                {
                    return false;
                }
            }
            for (std::size_t i = 0; i < 3; ++i)
            {
                for (std::size_t j = 0; j < 3; ++j)
                {
                    if (separated(a[i].Cross(b[j])))
                    {
                        return false;
                    }
                }
            }
            return true;
        }

    } // namespace

    SensorVolume SensorVolume::Sphere(const NS::Vector3& center, float radius) noexcept
    {
        SensorVolume volume{};
        volume.a = center;
        volume.b = center;
        volume.radius = std::max(radius, 0.0f);
        return volume;
    }

    SensorVolume SensorVolume::Capsule(const NS::Phys::Capsule& capsule) noexcept
    {
        NS::Vector3 axis = capsule.axis;
        if (axis.LengthSquared() > 0.0f)
        {
            axis.Normalize();
        }
        SensorVolume volume{};
        volume.a = capsule.center - axis * capsule.halfHeight;
        volume.b = capsule.center + axis * capsule.halfHeight;
        volume.radius = std::max(capsule.radius, 0.0f);
        return volume;
    }

    SensorVolume SensorVolume::Box(const NS::OBB& box) noexcept
    {
        SensorVolume volume{};
        volume.isBox = true;
        volume.box = box;
        volume.a = box.center;
        volume.b = box.center;
        return volume;
    }

    NS::AABB SensorVolume::Bounds() const noexcept
    {
        if (isBox)
        {
            // 各軸へ投影した半幅の和が、その軸並行の箱の半幅になる
            const NS::Vector3 extent{
                std::abs(box.axisX.x) * box.halfExtentX + std::abs(box.axisY.x) * box.halfExtentY +
                    std::abs(box.axisZ.x) * box.halfExtentZ,
                std::abs(box.axisX.y) * box.halfExtentX + std::abs(box.axisY.y) * box.halfExtentY +
                    std::abs(box.axisZ.y) * box.halfExtentZ,
                std::abs(box.axisX.z) * box.halfExtentX + std::abs(box.axisY.z) * box.halfExtentY +
                    std::abs(box.axisZ.z) * box.halfExtentZ,
            };
            return NS::AABB{box.center, extent};
        }
        const NS::Vector3 lo = NS::Vector3::Min(a, b) - NS::Vector3{radius, radius, radius};
        const NS::Vector3 hi = NS::Vector3::Max(a, b) + NS::Vector3{radius, radius, radius};
        return NS::AABB{(lo + hi) * 0.5f, (hi - lo) * 0.5f};
    }

    NS::Vector3 SensorVolume::Center() const noexcept
    {
        if (isBox)
        {
            return box.center;
        }
        return (a + b) * 0.5f;
    }

    bool VolumesOverlap(const SensorVolume& lhs, const SensorVolume& rhs) noexcept
    {
        if (lhs.isBox && rhs.isBox)
        {
            return BoxesOverlap(lhs.box, rhs.box);
        }
        if (lhs.isBox)
        {
            return SegmentBoxDistanceSq(rhs.a, rhs.b, lhs.box) <= rhs.radius * rhs.radius;
        }
        if (rhs.isBox)
        {
            return SegmentBoxDistanceSq(lhs.a, lhs.b, rhs.box) <= lhs.radius * lhs.radius;
        }
        const float reach = lhs.radius + rhs.radius;
        return SegmentSegmentDistanceSq(lhs.a, lhs.b, rhs.a, rhs.b) <= reach * reach;
    }

    HitSensor::HitSensor() noexcept = default;

    void HitSensor::OnStart()
    {
        Scene* scene = OwningScene();
        if (m_registered || scene == nullptr)
        {
            return;
        }
        scene->HitSensors().Register(this);
        m_registered = true;
    }

    void HitSensor::OnEndPlay()
    {
        Scene* scene = OwningScene();
        if (!m_registered || scene == nullptr)
        {
            return;
        }
        scene->HitSensors().Unregister(this);
        m_registered = false;
    }

    ShapeHitSensor::ShapeHitSensor() noexcept = default;

    void ShapeHitSensor::SetSphere(float radius) noexcept
    {
        m_shape = HitSensorShape::Sphere;
        m_radius = std::max(radius, 0.0f);
    }

    void ShapeHitSensor::SetCapsule(float radius, float halfHeight) noexcept
    {
        m_shape = HitSensorShape::Capsule;
        m_radius = std::max(radius, 0.0f);
        m_halfHeight = std::max(halfHeight, 0.0f);
    }

    void ShapeHitSensor::SetBox(const NS::Vector3& halfExtents) noexcept
    {
        m_shape = HitSensorShape::Box;
        m_boxHalfExtents =
            NS::Vector3{std::max(halfExtents.x, 0.0f), std::max(halfExtents.y, 0.0f), std::max(halfExtents.z, 0.0f)};
    }

    SensorVolume ShapeHitSensor::WorldVolume() const noexcept
    {
        NS::Matrix world = NS::Matrix::Identity;
        if (const Actor* owner = Owner())
        {
            world = owner->Root().WorldMatrix();
        }
        const NS::AffineDecomposition parts = NS::DecomposeAffine(world);
        const NS::Vector3 center = NS::Vector3::Transform(m_centerOffset, world);
        switch (m_shape)
        {
        case HitSensorShape::Capsule:
        {
            const NS::Vector3 axis = NS::Vector3::Transform(NS::Vector3::UnitY, parts.rotation);
            const float side = std::max(std::abs(parts.scale.x), std::abs(parts.scale.z));
            return SensorVolume::Capsule(NS::Phys::Capsule{.center = center,
                                                           .axis = axis,
                                                           .halfHeight = m_halfHeight * std::abs(parts.scale.y),
                                                           .radius = m_radius * side});
        }
        case HitSensorShape::Box:
        {
            const NS::Vector3 half{m_boxHalfExtents.x * std::abs(parts.scale.x),
                                   m_boxHalfExtents.y * std::abs(parts.scale.y),
                                   m_boxHalfExtents.z * std::abs(parts.scale.z)};
            return SensorVolume::Box(NS::MakeOBB(center, parts.rotation, half));
        }
        case HitSensorShape::Sphere:
        default:
            return SensorVolume::Sphere(center, m_radius * NS::MaxAbsComponent(parts.scale));
        }
    }

    FollowHitSensor::FollowHitSensor(std::function<SensorVolume()> volume) noexcept : m_volume(std::move(volume)) {}

    SensorVolume FollowHitSensor::WorldVolume() const noexcept
    {
        if (m_volume)
        {
            return m_volume();
        }
        NS_LOG_ERROR(Scene, "FollowHitSensor: 形を答える関数が空。根に長さ 0 の球を返す");
        NS::Vector3 root{0.0f, 0.0f, 0.0f};
        if (const Actor* owner = Owner())
        {
            root = owner->Root().Position();
        }
        return SensorVolume::Sphere(root, 0.0f);
    }

    NS_CLASS(ShapeHitSensor)
} // namespace NS::Obj
