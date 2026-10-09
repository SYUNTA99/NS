#pragma once

#include "NSlib/Core/Math.h"
#include "NSlib/Core/NonCopyable.h"

#include <vector>

namespace NS::Obj
{

    //! @brief 階層構造を持つ位置 / 回転 / スケール
    //! @details 親子関係を SetParent で構築し、WorldMatrix() で root から再計算した world 変換を返す
    //! 前フレーム値を保持し Snapshot() で現在値を一括退避する
    //! 描画側は世界が回っている間 FrameTimer::Alpha() で previous-current を補間し、
    //! ガタつきのない軌道を再現する
    //! 所有関係は持たず、Transform 間は生参照で寿命は Actor が支配する
    class Transform : public NS::NonCopyable
    {
    public:
        Transform() noexcept = default;
        ~Transform() noexcept;

        //! 親空間の Local position。Snapshot で previous に退避
        void SetPosition(const NS::Vector3& position) noexcept;
        //! 親空間の Local rotation で quaternion 表現
        void SetRotation(const NS::Quaternion& rotation) noexcept;
        //! Local scale。default は各軸 1
        void SetScale(const NS::Vector3& scale) noexcept;
        //! @brief 今の位置と前フレームの位置を同じだけずらす
        //! @details 補間には動きとして映らない。前フレームから今までに動いた分はそのまま補間される
        void ShiftPosition(const NS::Vector3& delta) noexcept;

        [[nodiscard]] const NS::Vector3& Position() const noexcept { return m_position; }
        [[nodiscard]] const NS::Quaternion& Rotation() const noexcept { return m_rotation; }
        [[nodiscard]] const NS::Vector3& Scale() const noexcept { return m_scale; }

        //! 補間用の前フレーム時点の値。Snapshot 前は default 値か直近 Snapshot 値
        [[nodiscard]] const NS::Vector3& PreviousPosition() const noexcept { return m_previousPosition; }
        [[nodiscard]] const NS::Quaternion& PreviousRotation() const noexcept { return m_previousRotation; }
        [[nodiscard]] const NS::Vector3& PreviousScale() const noexcept { return m_previousScale; }

        //! 現在 PRS を previous に退避する。Scene::OnUpdate 先頭で全 Transform に一括実行する
        //! SubObject の OnUpdate 内で個別実行すると parent-child の世代がずれるため禁止
        void Snapshot() noexcept;

        //! Local 行列。Scale * Rotate * Translate の合成
        [[nodiscard]] NS::Matrix LocalMatrix() const noexcept;
        //! World 行列で Local * parent.World を row-major LH で合成する。親が無ければ Local と同値
        [[nodiscard]] NS::Matrix WorldMatrix() const noexcept;
        //! Alpha 補間付き Local 行列。alpha=1 で現在 PRS、alpha=0 で previous PRS
        //! 割合 1 以上は前の値を読まず LocalMatrix と同じ値を返す
        [[nodiscard]] NS::Matrix InterpolatedLocalMatrix(float alpha) const noexcept;
        //! Alpha 補間付き World 行列。階層全体を補間値で再計算する
        [[nodiscard]] NS::Matrix InterpolatedWorldMatrix(float alpha) const noexcept;

        //! 親を切替える。null で root 化。サイクル検出は呼出側責任で現状では行わない
        void SetParent(Transform* parent) noexcept;
        [[nodiscard]] Transform* Parent() const noexcept { return m_parent; }
        [[nodiscard]] const std::vector<Transform*>& Children() const noexcept { return m_children; }

    private:
        NS::Vector3 m_position{0.0f, 0.0f, 0.0f}; // 親空間の Local position
        NS::Quaternion m_rotation{};              // 親空間の Local rotation
        NS::Vector3 m_scale{1.0f, 1.0f, 1.0f};    // Local scale

        NS::Vector3 m_previousPosition{0.0f, 0.0f, 0.0f}; // 前フレームの position
        NS::Quaternion m_previousRotation{};              // 前フレームの rotation
        NS::Vector3 m_previousScale{1.0f, 1.0f, 1.0f};    // 前フレームの scale

        Transform* m_parent = nullptr;      // 親 (非所有、無ければ root)
        std::vector<Transform*> m_children; // 子リスト (非所有)

        void DetachFromParent() noexcept;
    };

} // namespace NS::Obj
