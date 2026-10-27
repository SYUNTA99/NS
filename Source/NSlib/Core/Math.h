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

    //! 半直線
    using Ray = DirectX::SimpleMath::Ray;

    using Viewport = DirectX::SimpleMath::Viewport;

    using Color = DirectX::SimpleMath::Color;

    //! 2つの2次元ベクトルの内積
    [[nodiscard]] inline float Dot(const Vector2& a, const Vector2& b) noexcept
    {
        return a.x * b.x + a.y * b.y;
    }

    //! 2つのベクトルの内積
    [[nodiscard]] inline float Dot(const Vector3& a, const Vector3& b) noexcept
    {
        return a.x * b.x + a.y * b.y + a.z * b.z;
    }

    //! 2つのベクトルの外積
    [[nodiscard]] inline Vector3 Cross(const Vector3& a, const Vector3& b) noexcept
    {
        return Vector3{a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
    }

    inline constexpr float k_Pi = 3.14159265358979323846f;

    //! @brief 度をラジアンへ変換する
    //! @note 単位の取り違えを型で防ぐ ToRadians(Degrees) を使う
    [[nodiscard]] constexpr float DegreesToRadians(float deg) noexcept
    {
        return deg * (k_Pi / 180.0f);
    }

    //! @brief ラジアンを度へ変換する
    //! @note 単位の取り違えを型で防ぐ ToDegrees(Radians) を使う
    [[nodiscard]] constexpr float RadiansToDegrees(float rad) noexcept
    {
        return rad * (180.0f / k_Pi);
    }

    //! @brief 度の Euler 角 (x=Pitch, y=Yaw, z=Roll) を回転 Quaternion へ変換する
    [[nodiscard]] inline Quaternion EulerDegreesToQuaternion(const Vector3& eulerDegrees) noexcept
    {
        return Quaternion::CreateFromYawPitchRoll(Vector3{
            DegreesToRadians(eulerDegrees.x), DegreesToRadians(eulerDegrees.y), DegreesToRadians(eulerDegrees.z)});
    }

    //! @brief 回転 Quaternion を度の Euler 角 (x=Pitch, y=Yaw, z=Roll) へ変換する
    [[nodiscard]] inline Vector3 QuaternionToEulerDegrees(const Quaternion& rotation) noexcept
    {
        const Vector3 euler = rotation.ToEuler();
        return Vector3{RadiansToDegrees(euler.x), RadiansToDegrees(euler.y), RadiansToDegrees(euler.z)};
    }

    //! @brief アフィン行列を分解した scale / rotation / translation
    struct AffineDecomposition
    {
        Vector3 scale{1.0f, 1.0f, 1.0f};
        Quaternion rotation = Quaternion::Identity;
        Vector3 translation{0.0f, 0.0f, 0.0f};
    };

    //! @brief アフィン行列を scale / rotation / translation へ分解する
    [[nodiscard]] inline AffineDecomposition DecomposeAffine(const Matrix& matrix) noexcept
    {
        AffineDecomposition out;
        Matrix copy = matrix;
        copy.Decompose(out.scale, out.rotation, out.translation);
        return out;
    }

    //! ラジアンを明示する強い型
    struct Radians
    {
        float value;
    };

    //! 度を明示する強い型
    struct Degrees
    {
        float value;
    };

    //! 度数法から弧度法への変換
    [[nodiscard]] constexpr Radians ToRadians(Degrees d) noexcept
    {
        return Radians{d.value * (k_Pi / 180.0f)};
    }

    //! 弧度法から度数法への変換
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

    //! 2次元の幅と高さを明確に示すための強い型。負の値は無効として扱う
    struct Size2D
    {
        int width;
        int height;
    };

    [[nodiscard]] constexpr bool operator==(Size2D a, Size2D b) noexcept
    {
        return a.width == b.width && a.height == b.height;
    }

    //! @brief 幅を高さで割ったアスペクト比を返す
    //! @pre s.height は 0 でないこと
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

    //! t は 0〜1 の範囲外も受け付ける
    [[nodiscard]] constexpr float Lerp(float a, float b, float t) noexcept
    {
        return a + (b - a) * t;
    }

    //! 両端で傾き 0 の補間重み。t の範囲 0〜1 は丸めない
    [[nodiscard]] constexpr float SmoothStep(float t) noexcept
    {
        return t * t * (3.0f - 2.0f * t);
    }

    //! 同じ位置と種から同じ値を返す、状態を持たないハッシュ
    [[nodiscard]] constexpr uint32 NoiseHash(int32 position, uint32 seed) noexcept
    {
        // 掛ける数は上位と下位のビットがよく混ざる奇数。足して右へずらして排他的論理和を取り、位置の近さを壊す
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

    //! 同じ位置と種から連続した -1〜1 のノイズを返す。格子上の傾きは 0
    //! x は格子間隔を 1 とする位置 (時刻 × 周波数)
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

    //! 3 成分が全部有限の場合 true、非数か無限大が 1 つでもあれば false
    [[nodiscard]] inline bool IsFinite(const Vector3& v) noexcept
    {
        return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
    }

    //! 有限で 0 より大きい場合 true、それ以外の場合は false
    [[nodiscard]] inline bool IsPositiveFinite(float value) noexcept
    {
        return std::isfinite(value) && value > 0.0f;
    }

    //! 有限で 0 以上の場合 true、それ以外の場合は false
    [[nodiscard]] inline bool IsNonNegativeFinite(float value) noexcept
    {
        return std::isfinite(value) && value >= 0.0f;
    }

    //! @brief 3 成分の絶対値のうち最大のもの。非一様な拡縮から球の半径を決める時に使う
    [[nodiscard]] inline float MaxAbsComponent(const Vector3& v) noexcept
    {
        return std::max({std::abs(v.x), std::abs(v.y), std::abs(v.z)});
    }

    //! @brief 0 除算よけ・微小量判定の下限
    inline constexpr float k_Epsilon = 1e-4f;

    //! @brief スカラ値が 0 近傍（絶対値が tolerance 未満）か判定する
    [[nodiscard]] inline bool IsNearlyZero(float value, float tolerance = k_Epsilon) noexcept
    {
        return std::abs(value) < tolerance;
    }

    //! @brief 2 次元のベクトルが 0 近傍（長さ 2 乗が toleranceSq 未満）か判定する
    [[nodiscard]] inline bool IsNearlyZero(const Vector2& v, float toleranceSq = k_Epsilon * k_Epsilon) noexcept
    {
        return v.LengthSquared() < toleranceSq;
    }

    //! @brief 3 次元のベクトルが 0 近傍（長さ 2 乗が toleranceSq 未満）か判定する
    [[nodiscard]] inline bool IsNearlyZero(const Vector3& v, float toleranceSq = k_Epsilon * k_Epsilon) noexcept
    {
        return v.LengthSquared() < toleranceSq;
    }

    //! @brief 2 つのスカラ値がほぼ等しいか判定する
    [[nodiscard]] inline bool IsNearlyEqual(float a, float b, float tolerance = k_Epsilon) noexcept
    {
        return std::abs(a - b) <= tolerance;
    }

    //! @brief 2 つの単位ベクトルがほぼ同じ向き（平行）か判定する
    [[nodiscard]] inline bool IsNearlyParallel(const Vector3& a, const Vector3& b, float tolerance = k_Epsilon) noexcept
    {
        return Dot(a, b) > 1.0f - tolerance;
    }

    //! @brief XZ 平面の長さ 2 乗（水平成分）
    [[nodiscard]] inline float HorizontalLengthSquared(const Vector3& v) noexcept
    {
        return v.x * v.x + v.z * v.z;
    }

    //! @brief XZ 平面の長さ（水平成分）
    [[nodiscard]] inline float HorizontalLength(const Vector3& v) noexcept
    {
        return std::sqrt(HorizontalLengthSquared(v));
    }

    //! @brief 水平成分の長さが 0 近傍か判定する
    [[nodiscard]] inline bool IsNearlyZeroHorizontal(const Vector3& v, float toleranceSq = k_Epsilon * k_Epsilon) noexcept
    {
        return HorizontalLengthSquared(v) < toleranceSq;
    }

    //! @brief スカラ値が正（0 より大）か判定する。0 以下および NaN の場合は false
    [[nodiscard]] inline bool IsPositive(float value) noexcept
    {
        return value > 0.0f;
    }

    //! @brief スカラ値が非負（0 以上）か判定する。負および NaN の場合は false
    [[nodiscard]] inline bool IsNonNegative(float value) noexcept
    {
        return value >= 0.0f;
    }

    //! @brief a が b より大きいか判定する。a <= b またはいずれかが NaN の場合は false
    [[nodiscard]] inline bool IsGreaterThan(float a, float b) noexcept
    {
        return a > b;
    }

    //! @brief 値が閉区間 [minVal, maxVal] に入っているか判定する。範囲外および NaN の場合は false
    [[nodiscard]] inline bool IsInRange(float value, float minVal, float maxVal) noexcept
    {
        return value >= minVal && value <= maxVal;
    }

    //! @brief 全成分が等しい 3 次元スケールベクトルを返す
    [[nodiscard]] inline Vector3 UniformScale(float value) noexcept
    {
        return Vector3{value, value, value};
    }

    //! @brief ベクトルを正規化して返す。長さが 0 近傍の場合は fallback を返す
    [[nodiscard]] inline Vector3 NormalizedOr(const Vector3& v, const Vector3& fallback) noexcept
    {
        const float length = v.Length();
        if (IsNearlyZero(length))
        {
            return fallback;
        }
        return v / length;
    }

    //! @brief +Y 軸を指定した向きへ回す回転を返す
    [[nodiscard]] inline Quaternion TurnUpTo(const Vector3& direction) noexcept
    {
        return Quaternion::FromToRotation(Vector3{0.0f, 1.0f, 0.0f}, direction);
    }

    //! @brief +Z 軸（法線）を指定した向きへ回す回転を返す
    [[nodiscard]] inline Quaternion TurnNormalTo(const Vector3& direction) noexcept
    {
        return Quaternion::FromToRotation(Vector3{0.0f, 0.0f, 1.0f}, direction);
    }

    //! @brief 3 次のイーズアウト補間 (1 - (1 - t)^3)。t は 0〜1 にクランプする
    [[nodiscard]] inline float EaseOutCubic(float t) noexcept
    {
        const float clamped = std::clamp(t, 0.0f, 1.0f);
        const float rest = 1.0f - clamped;
        return 1.0f - rest * rest * rest;
    }

    //! @brief 有限値かつ正の有意な大きさ（k_Epsilon より大）を持つか判定する
    [[nodiscard]] inline bool HasPositiveMagnitude(float value, float minThreshold = k_Epsilon) noexcept
    {
        return std::isfinite(value) && value > minThreshold;
    }

    //! @brief XZ 平面の内積（水平成分）
    [[nodiscard]] inline float DotHorizontal(const Vector3& a, const Vector3& b) noexcept
    {
        return a.x * b.x + a.z * b.z;
    }

    //! @brief 2 つの単位ベクトルがなす角度（ラジアン、0〜π）を返す
    [[nodiscard]] inline float AngleBetween(const Vector3& a, const Vector3& b) noexcept
    {
        return std::acos(std::clamp(Dot(a, b), -1.0f, 1.0f));
    }

    //! @brief ベクトルから単位法線方向の成分を取り除いた射影ベクトルを返す
    [[nodiscard]] inline Vector3 RejectUnit(const Vector3& v, const Vector3& unitNormal) noexcept
    {
        return v - unitNormal * Dot(v, unitNormal);
    }

    //! @brief XZ 平面へ畳んだ向きを正規化する
    //! @param[in] v 元のベクトル。y 成分は捨てる
    //! @param[out] outDirection 正規化した向き。y は 0。失敗した場合は書き換えない
    //! @return 正規化できた場合 true、長さが k_Epsilon 未満の場合は false
    [[nodiscard]] inline bool TryNormalizeHorizontal(const Vector3& v, Vector3& outDirection) noexcept
    {
        const float lengthSq = HorizontalLengthSquared(v);
        if (lengthSq < k_Epsilon * k_Epsilon)
        {
            return false;
        }

        const float invLength = 1.0f / std::sqrt(lengthSq);
        outDirection = Vector3{v.x * invLength, 0.0f, v.z * invLength};
        return true;
    }

} // namespace NS
