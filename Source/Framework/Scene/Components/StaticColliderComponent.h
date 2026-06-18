#pragma once

/// @file StaticColliderComponent.h
/// @brief 静的 AABB collider Component。Owner の Root world 変換を halfExtents に適用した
///        world AABB を返す。LevelPlayScene の collision world 構築に使う

#include "Framework/Math/Math.h"
#include "Framework/Scene/Component.h"

namespace NS::Scene
{
    /// 軸並行 BoundingBox を SceneBase に登録する Component
    /// world 変換を適用するが結果は AABB なので、 回転時は内包する軸並行ボックスになる
    /// Mesh と分離し、 視覚と衝突を独立して調整可能にする
    class StaticColliderComponent : public Component
    {
    public:
        /// 既定 halfExtents {0.5,0.5,0.5} で構築する
        StaticColliderComponent() noexcept;
        /// halfExtents を指定して構築する。 ClampNonNegative で負を 0 にクランプ
        explicit StaticColliderComponent(const NS::Math::Vector3& halfExtents) noexcept;

        /// 半サイズを設定
        void SetHalfExtents(const NS::Math::Vector3& halfExtents) noexcept;

        /// 現在の半サイズ
        [[nodiscard]] NS::Math::Vector3 HalfExtents() const noexcept;

        /// Owner の root world position を center とした AABB を返す
        /// Owner が未登録の場合は origin 中心の AABB を返す (例外を投げない)
        [[nodiscard]] NS::Math::AABB WorldAABB() const noexcept;

    private:
        NS::Math::Vector3 m_halfExtents{0.5f, 0.5f, 0.5f};
    };
} // namespace NS::Scene
