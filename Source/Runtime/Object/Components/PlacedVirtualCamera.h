#pragma once

#include "Runtime/Core/Math.h"
#include "Runtime/Object/Components/VirtualCameraComponent.h"
#include "Runtime/Object/Reflection/Reflection.h"

namespace NS::Object
{
    /// @brief 視点とトリガ範囲を自分で持つ据え置き仮想カメラ
    /// @details 視点位置は owner の Transform が持ち、ギズモや transform 編集がそのままカメラ移動になる
    /// 注視点 / up / 進入判定用のトリガ AABB / lookAtPlayer は自身で保持する
    /// `UpdateActivation(playerPos)` がトリガ内なら自分を active 化し、lookAtPlayer 時は注視点を
    /// プレイヤーへ向ける。`SetVcamPriority` を follow より高くしておけば、active な間だけ
    /// CameraBrain がこれを選んでブレンドする。`EvaluatePose` は alpha 無視で固定 pose を返す
    /// 依存: NS::Core, NS::Object::VirtualCameraComponent
    class PlacedVirtualCamera : public VirtualCameraComponent
    {
    public:
        PlacedVirtualCamera() noexcept;

        /// 据え置き位置と注視点を設定する。位置は owner の Transform へ書く。owner 不在なら注視点のみ
        void SetView(const NS::Core::Vector3& position, const NS::Core::Vector3& target) noexcept;
        void SetUpDirection(const NS::Core::Vector3& up) noexcept { m_up = up; }

        /// 進入判定用のトリガ AABB を中心と半径で設定する。 半径成分は呼出側が正値に保つ
        void SetTrigger(const NS::Core::Vector3& center, const NS::Core::Vector3& extent) noexcept
        {
            m_triggerCenter = center;
            m_triggerExtent = extent;
        }
        /// true の間、進入中は注視点をプレイヤー位置へ追従させる。位置固定で被写体を追う Mario 系の挙動になる
        void SetLookAtPlayer(bool enable) noexcept { m_lookAtPlayer = enable; }

        /// プレイヤー位置がトリガ AABB 内なら自分を active 化し、外なら非 active にする
        /// lookAtPlayer 時は進入中の注視点をプレイヤーへ更新する。play 中に毎ステップ呼ぶ
        void UpdateActivation(const NS::Core::Vector3& playerPosition) noexcept;

        /// 視点の world 位置。owner の Transform から読む。owner 不在は既定位置
        [[nodiscard]] NS::Core::Vector3 ViewPosition() const noexcept;
        [[nodiscard]] const NS::Core::Vector3& ViewTarget() const noexcept { return m_target; }

        /// 進入判定トリガ AABB の中心
        [[nodiscard]] const NS::Core::Vector3& TriggerCenter() const noexcept { return m_triggerCenter; }
        /// 進入判定トリガ AABB の半径成分
        [[nodiscard]] const NS::Core::Vector3& TriggerExtent() const noexcept { return m_triggerExtent; }
        /// 進入中にプレイヤーを追視するか
        [[nodiscard]] bool LooksAtPlayer() const noexcept { return m_lookAtPlayer; }

        [[nodiscard]] CameraPose EvaluatePose(float alpha) const noexcept override;

        // 視点位置は owner Transform 所有なのでリフレクションしない。transform 編集の経路と二重にしない
        NS_REFLECT_BEGIN(PlacedVirtualCamera, VirtualCameraComponent)
        NS_REFLECT_FIELD(m_target, "注視点")
        NS_REFLECT_FIELD(m_up, "上方向")
        NS_REFLECT_FIELD(m_triggerCenter, "トリガー中心")
        NS_REFLECT_FIELD(m_triggerExtent, "トリガー半径")
        NS_REFLECT_FIELD(m_lookAtPlayer, "プレイヤー追視")
        NS_REFLECT_ACCESSOR(int, "優先度", VcamPriority(), SetVcamPriority)
        NS_REFLECT_END()

    private:
        NS::Core::Vector3 m_target{0.0f, 0.0f, 0.0f};        // 注視点
        NS::Core::Vector3 m_up{0.0f, 1.0f, 0.0f};            // アップベクトル
        NS::Core::Vector3 m_triggerCenter{0.0f, 0.0f, 0.0f}; // 進入判定トリガ AABB の中心
        NS::Core::Vector3 m_triggerExtent{1.0f, 1.0f, 1.0f}; // トリガ AABB の半径成分
        bool m_lookAtPlayer = false;                         // 進入中にプレイヤーを追視するか
    };
} // namespace NS::Object
