#pragma once

#include <SimpleMath.h>
#include <cmath>
#include <limits>

namespace NS::Core
{

    //! 2次元ベクトル
    using Vector2 = DirectX::SimpleMath::Vector2;

    //! 3次元ベクトル
    using Vector3 = DirectX::SimpleMath::Vector3;

    //! 4次元ベクトル
    using Vector4 = DirectX::SimpleMath::Vector4;

    //! 4x4の変換行列
    using Matrix = DirectX::SimpleMath::Matrix;

    //! @brief Matrixのエイリアス
    //! @note 4x4の行列であることをコード上で明示したい場合に使用する
    using Matrix4x4 = DirectX::SimpleMath::Matrix;

    //! クォータニオン
    using Quaternion = DirectX::SimpleMath::Quaternion;

    //! 3次元空間上の平面
    using Plane = DirectX::SimpleMath::Plane;

    //! 半直線
    using Ray = DirectX::SimpleMath::Ray;

    //! ビューポート
    using Viewport = DirectX::SimpleMath::Viewport;

    //! 色
    using Color = DirectX::SimpleMath::Color;

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

    //! 円周率
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
    [[nodiscard]] constexpr bool operator!=(Radians a, Radians b) noexcept
    {
        return !(a == b);
    }
    [[nodiscard]] constexpr bool operator==(Degrees a, Degrees b) noexcept
    {
        return a.value == b.value;
    }
    [[nodiscard]] constexpr bool operator!=(Degrees a, Degrees b) noexcept
    {
        return !(a == b);
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
    [[nodiscard]] constexpr bool operator!=(Size2D a, Size2D b) noexcept
    {
        return !(a == b);
    }

    //! @brief 幅を高さで割ったアスペクト比を返す
    //! @pre s.height は 0 でないこと
    [[nodiscard]] constexpr float AspectRatio(Size2D s) noexcept
    {
        return static_cast<float>(s.width) / static_cast<float>(s.height);
    }

    //! 指定された値を、下限から上限の範囲内に収める
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

    //! @brief 2つの値の間を線形補間する
    //! @param[in] a t=0 のときの値
    //! @param[in] b t=1 のときの値
    //! @param[in] t 補間係数。0〜1 の外も受け付ける
    [[nodiscard]] constexpr float Lerp(float a, float b, float t) noexcept
    {
        return a + (b - a) * t;
    }

    //! @brief 0 除算よけの下限
    //! @details 長さ 2 乗と比べる側は k_Epsilon * k_Epsilon と書く。意味を持つ許容誤差には使わない
    inline constexpr float k_Epsilon = 1e-4f;

    //! 1.0f と次に表現できる float との差
    inline constexpr float k_FloatEpsilon = std::numeric_limits<float>::epsilon();

    //! @brief XZ 平面へ畳んだ向きを正規化する
    //! @param[in] v 元のベクトル。y 成分は捨てる
    //! @param[out] outDirection 正規化した向き。y は 0。失敗した場合は書き換えない
    //! @return 正規化できた場合 true、長さが k_Epsilon 未満の場合は false
    //! @details 除算よけの下限を関数の中へ閉じる。呼ぶ側は下限の値を知らずに済む
    [[nodiscard]] inline bool TryNormalizeHorizontal(const Vector3& v, Vector3& outDirection) noexcept
    {
        const float lengthSq = v.x * v.x + v.z * v.z;
        if (lengthSq < k_Epsilon * k_Epsilon)
        {
            return false;
        }

        const float invLength = 1.0f / std::sqrt(lengthSq);
        outDirection = Vector3{v.x * invLength, 0.0f, v.z * invLength};
        return true;
    }

} // namespace NS::Core