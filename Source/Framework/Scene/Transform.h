#pragma once

/// @file Transform.h
/// @brief NS::Scene::Transform — 階層構造を持つ位置/回転/スケール。
///
/// 親子関係を `SetParent` で構築し、`WorldMatrix()` で root から再計算した world 変換を返す。
/// 前フレーム値 (`PreviousPosition` / `PreviousRotation` / `PreviousScale`) を保持し、
/// `Snapshot()` で現在値を一括退避する。可変フレーム描画側は `NS::Core::FrameTimer::Alpha()` で
/// previous-current を補間して jitter なしの軌道を再現する。

#include "Framework/Core/Math.h"

#include <vector>

namespace NS::Scene
{

    /// 階層 Transform。所有関係は持たない (Transform 間は raw 参照、寿命は GameObject が支配)。
    class Transform
    {
    public:
        Transform() noexcept = default;
        ~Transform() noexcept;

        Transform(const Transform&) = delete;
        Transform& operator=(const Transform&) = delete;
        Transform(Transform&&) = delete;
        Transform& operator=(Transform&&) = delete;

        /// Local position (親空間)。Snapshot で previous に退避。
        void SetPosition(const NS::Core::Vector3& position) noexcept;
        /// Local rotation (親空間、quaternion)。
        void SetRotation(const NS::Core::Quaternion& rotation) noexcept;
        /// Local scale。default は (1,1,1)。
        void SetScale(const NS::Core::Vector3& scale) noexcept;

        [[nodiscard]] const NS::Core::Vector3& Position() const noexcept { return m_position; }
        [[nodiscard]] const NS::Core::Quaternion& Rotation() const noexcept { return m_rotation; }
        [[nodiscard]] const NS::Core::Vector3& Scale() const noexcept { return m_scale; }

        /// 前フレーム時点の値 (補間用)。Snapshot 前は default 値 or 直近 Snapshot 値。
        [[nodiscard]] const NS::Core::Vector3& PreviousPosition() const noexcept { return m_previousPosition; }
        [[nodiscard]] const NS::Core::Quaternion& PreviousRotation() const noexcept { return m_previousRotation; }
        [[nodiscard]] const NS::Core::Vector3& PreviousScale() const noexcept { return m_previousScale; }

        /// 現在 PRS を previous に退避する。Scene::OnUpdate 末尾で LevelEditorScene が
        /// 全 Transform に対して一括実行する。Component の OnUpdate 末尾で
        /// 個別実行するのは禁止 (階層 parent-child の世代不一致防止)。
        void Snapshot() noexcept;

        /// Local 行列 (Scale * Rotate * Translate)。
        [[nodiscard]] NS::Core::Matrix LocalMatrix() const noexcept;
        /// World 行列 (Local * parent.World、row-major LH)。親が無ければ Local と同値。
        [[nodiscard]] NS::Core::Matrix WorldMatrix() const noexcept;
        /// Alpha 補間付き Local 行列。alpha=1 で現在 PRS、alpha=0 で previous PRS。
        [[nodiscard]] NS::Core::Matrix InterpolatedLocalMatrix(float alpha) const noexcept;
        /// Alpha 補間付き World 行列。階層全体を補間値で再計算する。
        [[nodiscard]] NS::Core::Matrix InterpolatedWorldMatrix(float alpha) const noexcept;

        /// 親を切替える。null で root 化。サイクル検出は呼出側責任 ( では行わない)。
        void SetParent(Transform* parent) noexcept;
        [[nodiscard]] Transform* Parent() const noexcept { return m_parent; }
        [[nodiscard]] const std::vector<Transform*>& Children() const noexcept { return m_children; }

    private:
        NS::Core::Vector3 m_position{0.0f, 0.0f, 0.0f};
        NS::Core::Quaternion m_rotation{};
        NS::Core::Vector3 m_scale{1.0f, 1.0f, 1.0f};

        NS::Core::Vector3 m_previousPosition{0.0f, 0.0f, 0.0f};
        NS::Core::Quaternion m_previousRotation{};
        NS::Core::Vector3 m_previousScale{1.0f, 1.0f, 1.0f};

        Transform* m_parent = nullptr;
        std::vector<Transform*> m_children;

        void DetachFromParent() noexcept;
    };

} // namespace NS::Scene
