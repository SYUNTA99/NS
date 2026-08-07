#pragma once

#include <cmath>
#include <DirectXCollision.h>
#include <SimpleMath.h>

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

    //! 半直線（レイ）
    using Ray = DirectX::SimpleMath::Ray;

    //! 描画領域（ビューポート）。
    using Viewport = DirectX::SimpleMath::Viewport;

    //! 色（RGBA）
    using Color = DirectX::SimpleMath::Color;

    //! 軸に平行な境界ボックス（AABB）
    using AABB = DirectX::BoundingBox;

    //! @brief 有向境界ボックス（OBB）
    //! @details 回転を伴う境界ボックス。中心座標と、各軸（X, Y, Z）の向き、および中心から各面までの距離で構成される
    struct OBB
    {
        Vector3 center{0.0f, 0.0f, 0.0f};
        Vector3 axisX{1.0f, 0.0f, 0.0f};
        Vector3 axisY{0.0f, 1.0f, 0.0f};
        Vector3 axisZ{0.0f, 0.0f, 1.0f};
        float halfExtentX{0.5f};
        float halfExtentY{0.5f};
        float halfExtentZ{0.5f};
    };

    //! @brief center と radius による球形状
    //! @details 向きを持たない球。center を中心に半径 radius
    struct Sphere
    {
        Vector3 center{0.0f, 0.0f, 0.0f}; // 中心
        float radius = 0.5f;              // 半径
    };

    /// @brief AABB カリング用の視錐台。viewProj から抜いた6平面を内向き正で保持する
    /// @details 平面は正規化しない。内外の符号だけ要る AABB 交差にしか使わないため、正規化は無駄になる
    struct Frustum
    {
        Plane planes[6]{}; // 左右下上 near far の順。各平面は内側が正

        /// @brief 行ベクトル規約 v*M の viewProj から6平面を抽出する。奥行きは D3D の [0,1] 前提
        [[nodiscard]] static Frustum FromViewProjection(const Matrix& m) noexcept
        {
            Frustum f{};
            f.planes[0] = Plane{m._14 + m._11, m._24 + m._21, m._34 + m._31, m._44 + m._41};
            f.planes[1] = Plane{m._14 - m._11, m._24 - m._21, m._34 - m._31, m._44 - m._41};
            f.planes[2] = Plane{m._14 + m._12, m._24 + m._22, m._34 + m._32, m._44 + m._42};
            f.planes[3] = Plane{m._14 - m._12, m._24 - m._22, m._34 - m._32, m._44 - m._42};
            f.planes[4] = Plane{m._13, m._23, m._33, m._43}; // near は z>=0
            f.planes[5] = Plane{m._14 - m._13, m._24 - m._23, m._34 - m._33, m._44 - m._43};
            return f;
        }

        /// @brief AABB が視錐台と交差するか。完全に外なら false、少しでも重なれば true
        /// @details 中心・半径の保守判定で、外を内と誤ることはあっても内を外と切り落とすことはない
        [[nodiscard]] bool Intersects(const AABB& box) const noexcept
        {
            for (const Plane& p : planes)
            {
                const float r =
                    box.Extents.x * std::fabs(p.x) + box.Extents.y * std::fabs(p.y) + box.Extents.z * std::fabs(p.z);
                const float s = p.x * box.Center.x + p.y * box.Center.y + p.z * box.Center.z + p.w;
                if (s + r < 0.0f)
                    return false; // AABB 全体が内向き平面の外側なので視錐台の外
            }
            return true;
        }
    };

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

    //! 円周率（π）
    inline constexpr float k_Pi = 3.14159265358979323846f;

    //! @brief 度数法から弧度法（ラジアン）への変換
    //! @note 型の安全性を高めるため、可能な限り `ToRadians(Degrees)` の使用を推奨する
    [[nodiscard]] constexpr float DegreesToRadians(float deg) noexcept
    {
        return deg * (k_Pi / 180.0f);
    }

    //! @brief 弧度法（ラジアン）から度数法への変換
    //! @note 型の安全性を高めるため、可能な限り `ToDegrees(Radians)` の使用を推奨する
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

    //! 弧度法（ラジアン）を明確に示すための強い型
    struct Radians
    {
        float value;
    };

    //! 度数法（度）を明確に示すための強い型
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

    //! @brief 指定されたサイズの画面アスペクト比（幅 ÷ 高さ）を計算する
    //! @pre s.height は 0 でないこと。
    [[nodiscard]] constexpr float AspectRatio(Size2D s) noexcept
    {
        return static_cast<float>(s.width) / static_cast<float>(s.height);
    }

    //! 指定された値を、下限から上限の範囲内に収める
    template <typename T> [[nodiscard]] constexpr T Clamp(T value, T lo, T hi) noexcept
    {
        if (value < lo)
            return lo;
        if (value > hi)
            return hi;
        return value;
    }

    //! @brief 2つの値の間を線形補間する
    //! @param a 始点（t=0 の時の値）
    //! @param b 終点（t=1 の時の値）
    //! @param t 補間係数（範囲外の値も許容される）
    [[nodiscard]] constexpr float Lerp(float a, float b, float t) noexcept
    {
        return a + (b - a) * t;
    }

} // namespace NS::Core