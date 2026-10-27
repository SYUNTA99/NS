#pragma once

#include "NSlib/Core/Types.h"
#include <SimpleMath.h>
#include <algorithm>
#include <cmath>

namespace NS
{

    using Vector2 = DirectX::SimpleMath::Vector2;
    using Vector3 = DirectX::SimpleMath::Vector3;
    using Vector4 = DirectX::SimpleMath::Vector4;
    using Matrix = DirectX::SimpleMath::Matrix;
    using Quaternion = DirectX::SimpleMath::Quaternion;
    using Plane = DirectX::SimpleMath::Plane;
    using Ray = DirectX::SimpleMath::Ray;
    using Viewport = DirectX::SimpleMath::Viewport;
    using Color = DirectX::SimpleMath::Color;

    [[nodiscard]] inline float Dot(const Vector2& a, const Vector2& b) noexcept
    {
        return a.x * b.x + a.y * b.y;
    }

    [[nodiscard]] inline float Dot(const Vector3& a, const Vector3& b) noexcept
    {
        return a.x * b.x + a.y * b.y + a.z * b.z;
    }

    [[nodiscard]] inline Vector3 Cross(const Vector3& a, const Vector3& b) noexcept
    {
        return Vector3{a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
    }

    inline constexpr float k_Pi = 3.14159265358979323846f;

    /// 単位の取り違えを型で防ぐ場合は ToRadians(Degrees) を使う
    [[nodiscard]] constexpr float DegreesToRadians(float deg) noexcept
    {
        return deg * (k_Pi / 180.0f);
    }

    /// 単位の取り違えを型で防ぐ場合は ToDegrees(Radians) を使う
    [[nodiscard]] constexpr float RadiansToDegrees(float rad) noexcept
    {
        return rad * (180.0f / k_Pi);
    }

    /// 度の Euler 角 (x=Pitch, y=Yaw, z=Roll) を Quaternion へ変換する
    [[nodiscard]] inline Quaternion EulerDegreesToQuaternion(const Vector3& eulerDegrees) noexcept
    {
        return Quaternion::CreateFromYawPitchRoll(Vector3{
            DegreesToRadians(eulerDegrees.x), DegreesToRadians(eulerDegrees.y), DegreesToRadians(eulerDegrees.z)});
    }

    /// Quaternion を度の Euler 角 (x=Pitch, y=Yaw, z=Roll) へ変換する
    [[nodiscard]] inline Vector3 QuaternionToEulerDegrees(const Quaternion& rotation) noexcept
    {
        const Vector3 euler = rotation.ToEuler();
        return Vector3{RadiansToDegrees(euler.x), RadiansToDegrees(euler.y), RadiansToDegrees(euler.z)};
    }

    struct AffineDecomposition
    {
        Vector3 scale{1.0f, 1.0f, 1.0f};
        Quaternion rotation = Quaternion::Identity;
        Vector3 translation{0.0f, 0.0f, 0.0f};
    };

    [[nodiscard]] inline AffineDecomposition DecomposeAffine(const Matrix& matrix) noexcept
    {
        AffineDecomposition out;
        Matrix copy = matrix;
        copy.Decompose(out.scale, out.rotation, out.translation);
        return out;
    }

    struct Radians
    {
        float value;
    };

    struct Degrees
    {
        float value;
    };

    [[nodiscard]] constexpr Radians ToRadians(Degrees d) noexcept
    {
        return Radians{d.value * (k_Pi / 180.0f)};
    }

    [[nodiscard]] constexpr Degrees ToDegrees(Radians r) noexcept
    {
        return Degrees{r.value * (180.0f / k_Pi)};
    }

    [[nodiscard]] constexpr bool operator==(Radians a, Radians b) noexcept
    {
        return a.value == b.value;
    }
    [[nodiscard]] constexpr bool operator==(Degrees a, Degrees b) noexcept
    {
        return a.value == b.value;
    }

    /// 負の値は無効として扱う
    struct Size2D
    {
        int width;
        int height;
    };

    [[nodiscard]] constexpr bool operator==(Size2D a, Size2D b) noexcept
    {
        return a.width == b.width && a.height == b.height;
    }

    /// width / height を返す。s.height は 0 でないこと
    [[nodiscard]] constexpr float AspectRatio(Size2D s) noexcept
    {
        return static_cast<float>(s.width) / static_cast<float>(s.height);
    }

    template <typename T> [[nodiscard]] constexpr T Clamp(T value, T lo, T hi) noexcept
    {
        if (value < lo)
        {
            return lo;
        }
        if (value > hi)
        {
            return hi;
        }
        return value;
    }

    /// t は [0, 1] の範囲外もクランプせず外挿する
    [[nodiscard]] constexpr float Lerp(float a, float b, float t) noexcept
    {
        return a + (b - a) * t;
    }

    /// 両端で傾き 0 のエルミート補間。t は [0, 1] にクランプしない
    [[nodiscard]] constexpr float SmoothStep(float t) noexcept
    {
        return t * t * (3.0f - 2.0f * t);
    }

    [[nodiscard]] constexpr uint32 NoiseHash(int32 position, uint32 seed) noexcept
    {
        constexpr uint32 k_Mix1 = 0xB5297A4Du;
        constexpr uint32 k_Mix2 = 0x68E31DA4u;
        constexpr uint32 k_Mix3 = 0x1B56C4E9u;
        uint32 mixed = static_cast<uint32>(position);
        mixed *= k_Mix1;
        mixed += seed;
        mixed ^= mixed >> 8;
        mixed += k_Mix2;
        mixed ^= mixed << 8;
        mixed *= k_Mix3;
        mixed ^= mixed >> 8;
        return mixed;
    }

    /// [-1, 1] の連続した 1 次元バリューノイズ。x は格子間隔を 1 とする位置
    [[nodiscard]] inline float ValueNoise1D(float x, uint32 seed) noexcept
    {
        const float cell = std::floor(x);
        const int32 index = static_cast<int32>(cell);
        const float t = x - cell;
        constexpr float k_HashToUnit = 1.0f / 4294967295.0f;
        const float a = static_cast<float>(NoiseHash(index, seed)) * k_HashToUnit * 2.0f - 1.0f;
        const float b = static_cast<float>(NoiseHash(index + 1, seed)) * k_HashToUnit * 2.0f - 1.0f;
        return Clamp(Lerp(a, b, SmoothStep(t)), -1.0f, 1.0f);
    }

    [[nodiscard]] inline bool IsFinite(const Vector3& v) noexcept
    {
        return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
    }

    [[nodiscard]] inline bool IsPositiveFinite(float value) noexcept
    {
        return std::isfinite(value) && value > 0.0f;
    }

    [[nodiscard]] inline bool IsNonNegativeFinite(float value) noexcept
    {
        return std::isfinite(value) && value >= 0.0f;
    }

    [[nodiscard]] inline float MaxAbsComponent(const Vector3& v) noexcept
    {
        return std::max({std::abs(v.x), std::abs(v.y), std::abs(v.z)});
    }

    inline constexpr float k_Epsilon = 1e-4f;

    [[nodiscard]] inline bool IsNearlyZero(float value, float tolerance = k_Epsilon) noexcept
    {
        return std::abs(value) < tolerance;
    }

    [[nodiscard]] inline bool IsNearlyZero(const Vector2& v, float toleranceSq = k_Epsilon * k_Epsilon) noexcept
    {
        return v.LengthSquared() < toleranceSq;
    }

    [[nodiscard]] inline bool IsNearlyZero(const Vector3& v, float toleranceSq = k_Epsilon * k_Epsilon) noexcept
    {
        return v.LengthSquared() < toleranceSq;
    }

    [[nodiscard]] inline bool IsNearlyEqual(float a, float b, float tolerance = k_Epsilon) noexcept
    {
        return std::abs(a - b) <= tolerance;
    }

    /// 2 つの単位ベクトルがほぼ同じ向きか判定する
    [[nodiscard]] inline bool IsNearlyParallel(const Vector3& a, const Vector3& b, float tolerance = k_Epsilon) noexcept
    {
        return Dot(a, b) > 1.0f - tolerance;
    }

    [[nodiscard]] inline float HorizontalLengthSquared(const Vector3& v) noexcept
    {
        return v.x * v.x + v.z * v.z;
    }

    [[nodiscard]] inline float HorizontalLength(const Vector3& v) noexcept
    {
        return std::sqrt(HorizontalLengthSquared(v));
    }

    [[nodiscard]] inline float DotHorizontal(const Vector3& a, const Vector3& b) noexcept
    {
        return a.x * b.x + a.z * b.z;
    }

    [[nodiscard]] inline Vector3 HorizontalPart(const Vector3& v) noexcept
    {
        return Vector3{v.x, 0.0f, v.z};
    }

    /// XZ 平面上で from を Y 軸まわりに回して to へ重ねる角度（ラジアン、[-π, π]、+X から -Z 向きが正）
    [[nodiscard]] inline float SignedHorizontalAngle(const Vector3& from, const Vector3& to) noexcept
    {
        return std::atan2(from.z * to.x - from.x * to.z, DotHorizontal(from, to));
    }

    /// 2 つの単位ベクトルのなす角（ラジアン、[0, π]）
    [[nodiscard]] inline float AngleBetweenUnit(const Vector3& a, const Vector3& b) noexcept
    {
        return std::acos(std::clamp(Dot(a, b), -1.0f, 1.0f));
    }

    [[nodiscard]] inline bool IsNearlyZeroHorizontal(const Vector3& v, float toleranceSq = k_Epsilon * k_Epsilon) noexcept
    {
        return HorizontalLengthSquared(v) < toleranceSq;
    }

    [[nodiscard]] inline bool IsPositive(float value) noexcept
    {
        return value > 0.0f;
    }

    [[nodiscard]] inline bool IsNonNegative(float value) noexcept
    {
        return value >= 0.0f;
    }

    [[nodiscard]] inline bool IsGreaterThan(float a, float b) noexcept
    {
        return a > b;
    }

    [[nodiscard]] inline bool IsInRange(float value, float minVal, float maxVal) noexcept
    {
        return value >= minVal && value <= maxVal;
    }

    [[nodiscard]] inline Vector3 UniformScale(float value) noexcept
    {
        return Vector3{value, value, value};
    }

    /// +Y 軸を direction へ回す回転を返す
    [[nodiscard]] inline Quaternion TurnUpTo(const Vector3& direction) noexcept
    {
        return Quaternion::FromToRotation(Vector3{0.0f, 1.0f, 0.0f}, direction);
    }

    /// +Z 軸を direction へ回す回転を返す
    [[nodiscard]] inline Quaternion TurnNormalTo(const Vector3& direction) noexcept
    {
        return Quaternion::FromToRotation(Vector3{0.0f, 0.0f, 1.0f}, direction);
    }

    /// t は [0, 1] にクランプされる
    [[nodiscard]] inline float EaseOutCubic(float t) noexcept
    {
        const float clamped = std::clamp(t, 0.0f, 1.0f);
        const float rest = 1.0f - clamped;
        return 1.0f - rest * rest * rest;
    }

    [[nodiscard]] inline bool HasPositiveMagnitude(float value, float minThreshold = k_Epsilon) noexcept
    {
        return std::isfinite(value) && value > minThreshold;
    }

    [[nodiscard]] inline bool HasPositiveMagnitude(const Vector3& v, float minThresholdSq = k_Epsilon * k_Epsilon) noexcept
    {
        return HasPositiveMagnitude(v.LengthSquared(), minThresholdSq);
    }

    [[nodiscard]] inline bool HasPositiveMagnitudeHorizontal(const Vector3& v,
                                                             float minThresholdSq = k_Epsilon * k_Epsilon) noexcept
    {
        return HasPositiveMagnitude(HorizontalLengthSquared(v), minThresholdSq);
    }

    /// 長さが k_Epsilon 以下または非有限の場合は fallback を返す
    [[nodiscard]] inline Vector3 NormalizedOr(const Vector3& v, const Vector3& fallback) noexcept
    {
        const float length = v.Length();
        if (!HasPositiveMagnitude(length))
        {
            return fallback;
        }
        return v / length;
    }

    /// v から単位法線 unitNormal 方向の成分を取り除いたベクトルを返す
    [[nodiscard]] inline Vector3 RejectUnit(const Vector3& v, const Vector3& unitNormal) noexcept
    {
        return v - unitNormal * Dot(v, unitNormal);
    }

    /// XZ 平面へ射影して正規化する。長さが k_Epsilon 以下または非有限の場合は outDirection を変更せず false を返す
    [[nodiscard]] inline bool TryNormalizeHorizontal(const Vector3& v, Vector3& outDirection) noexcept
    {
        const float length = std::hypot(v.x, v.z);
        if (!HasPositiveMagnitude(length))
        {
            return false;
        }

        outDirection = Vector3{v.x / length, 0.0f, v.z / length};
        return true;
    }

} // namespace NS
