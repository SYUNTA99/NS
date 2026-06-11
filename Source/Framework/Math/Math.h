#pragma once

/// @file Math.h
/// @brief NS::Math 数学型エイリアスとヘルパー
///
/// SimpleMath の型を using-alias で `NS::Math` に露出する単一ヘッダ
/// 座標系は LH

#include <SimpleMath.h>

#include <DirectXCollision.h>

namespace NS::Math
{

    /// 2D ベクトル
    using Vector2 = DirectX::SimpleMath::Vector2;

    /// 3D ベクトル
    using Vector3 = DirectX::SimpleMath::Vector3;

    /// 4D ベクトル。同次座標・カラー前段に使う
    using Vector4 = DirectX::SimpleMath::Vector4;

    /// 4x4 行列。行列演算の本体型
    using Matrix = DirectX::SimpleMath::Matrix;

    /// Matrix の別名。CB レイアウト等で 4x4 と明示したい時に使う
    using Matrix4x4 = DirectX::SimpleMath::Matrix;

    /// クォータニオン。回転表現の本体型
    using Quaternion = DirectX::SimpleMath::Quaternion;

    /// 平面
    using Plane = DirectX::SimpleMath::Plane;

    /// レイ
    using Ray = DirectX::SimpleMath::Ray;

    /// ビューポート定義
    using Viewport = DirectX::SimpleMath::Viewport;

    /// RGBA カラー
    using Color = DirectX::SimpleMath::Color;

    /// 軸並行バウンディングボックス。Center+Extents 表現
    using AABB = DirectX::BoundingBox;

    /// 円周率
    inline constexpr float kPi = 3.14159265358979323846f;

    /// 度 → ラジアン変換。 強い型を使う場合は ToRadians(Degrees) を優先
    [[nodiscard]] constexpr float DegreesToRadians(float deg) noexcept
    {
        return deg * (kPi / 180.0f);
    }

    /// ラジアン → 度変換。 強い型を使う場合は ToDegrees(Radians) を優先
    [[nodiscard]] constexpr float RadiansToDegrees(float rad) noexcept
    {
        return rad * (180.0f / kPi);
    }

    /// 弧度法の強い型。暗黙変換禁止。値は `value` メンバで取り出す
    struct Radians
    {
        float value;
    };

    /// 度数法の強い型。暗黙変換禁止。ToRadians() 経由で変換する
    struct Degrees
    {
        float value;
    };

    /// Degrees → Radians 明示変換。 sin/cos など radians 期待 API への入口
    [[nodiscard]] constexpr Radians ToRadians(Degrees d) noexcept
    {
        return Radians{d.value * (kPi / 180.0f)};
    }

    /// Radians → Degrees 明示変換。 デバッグ表示
    [[nodiscard]] constexpr Degrees ToDegrees(Radians r) noexcept
    {
        return Degrees{r.value * (180.0f / kPi)};
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

    /// 2D ピクセルサイズの強い型。int 保持、負値は無効。暗黙変換禁止
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

    /// アスペクト比 (width / height) を float で返す
    /// @pre s.height != 0
    [[nodiscard]] constexpr float AspectRatio(Size2D s) noexcept
    {
        return static_cast<float>(s.width) / static_cast<float>(s.height);
    }

    /// `value` を `[lo, hi]` の範囲にクランプする
    template <typename T> [[nodiscard]] constexpr T Clamp(T value, T lo, T hi) noexcept
    {
        return (value < lo) ? lo : (value > hi) ? hi : value;
    }

    /// 線形補間。t=0 で a、t=1 で b。範囲外は外挿
    [[nodiscard]] constexpr float Lerp(float a, float b, float t) noexcept
    {
        return a + (b - a) * t;
    }

} // namespace NS::Math
