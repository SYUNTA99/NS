#pragma once

/// @file PlacedVirtualCamera.h
/// @brief NS::Scene::PlacedVirtualCamera — 視点とトリガ範囲を自分で持つ据え置き仮想カメラ
///
/// @details 位置 / 注視点 / up に加え、進入判定用のトリガ AABB と lookAtPlayer を自身で保持する
/// `UpdateActivation(playerPos)` がトリガ内なら自分を active 化し、lookAtPlayer 時は注視点を
/// プレイヤーへ向ける。`SetVcamPriority` を follow より高くしておけば、active な間だけ
/// CameraBrain がこれを選んでブレンドする。`EvaluatePose` は alpha 無視で固定 pose を返す
/// 依存: NS::Math, NS::Scene::VirtualCameraComponent

#include "Framework/Math/Math.h"
#include "Framework/Scene/Components/VirtualCameraComponent.h"
#include "Framework/Scene/Reflection.h"

namespace NS::Scene
{
    /// 据え置きカメラ。視点とトリガを自身で持ち、進入判定で自分を active 化する
    class PlacedVirtualCamera : public VirtualCameraComponent
    {
    public:
        PlacedVirtualCamera() noexcept;

        /// 据え置き位置と注視点を設定する
        void SetView(const NS::Math::Vector3& position, const NS::Math::Vector3& target) noexcept;
        void SetUpDirection(const NS::Math::Vector3& up) noexcept { m_up = up; }

        /// 進入判定用のトリガ AABB を中心と半径で設定する。 半径成分は呼出側が正値に保つ
        void SetTrigger(const NS::Math::Vector3& center, const NS::Math::Vector3& extent) noexcept
        {
            m_triggerCenter = center;
            m_triggerExtent = extent;
        }
        /// true の間、進入中は注視点をプレイヤー位置へ追従させる。位置固定で被写体を追う Mario 系の挙動になる
        void SetLookAtPlayer(bool enable) noexcept { m_lookAtPlayer = enable; }

        /// プレイヤー位置を受け、トリガ AABB 内なら自分を active 化し、外なら非 active にする
        /// lookAtPlayer 時は進入中の注視点をプレイヤーへ更新する。play 中に毎ステップ呼ぶ
        void UpdateActivation(const NS::Math::Vector3& playerPosition) noexcept;

        [[nodiscard]] const NS::Math::Vector3& ViewPosition() const noexcept { return m_position; }
        [[nodiscard]] const NS::Math::Vector3& ViewTarget() const noexcept { return m_target; }

        /// 進入判定トリガ AABB の中心
        [[nodiscard]] const NS::Math::Vector3& TriggerCenter() const noexcept { return m_triggerCenter; }
        /// 進入判定トリガ AABB の半径成分
        [[nodiscard]] const NS::Math::Vector3& TriggerExtent() const noexcept { return m_triggerExtent; }
        /// 進入中にプレイヤーを追視するか
        [[nodiscard]] bool LooksAtPlayer() const noexcept { return m_lookAtPlayer; }

        [[nodiscard]] CameraPose EvaluatePose(float alpha) const noexcept override;

        // up は保存側の CameraVolume に枠が無いので反射しない。編集出来て保存されない欄を作らない
        NS_REFLECT_BEGIN(PlacedVirtualCamera)
        NS_REFLECT_FIELD(m_position, "Camera Pos")
        NS_REFLECT_FIELD(m_target, "Look Target")
        NS_REFLECT_FIELD(m_triggerCenter, "Trigger Center")
        NS_REFLECT_FIELD(m_triggerExtent, "Trigger Extent")
        NS_REFLECT_FIELD(m_lookAtPlayer, "Look At Player")
        NS_REFLECT_ACCESSOR(int, "Priority", VcamPriority(), SetVcamPriority)
        NS_REFLECT_END()

    private:
        NS::Math::Vector3 m_position{0.0f, 5.0f, -10.0f};
        NS::Math::Vector3 m_target{0.0f, 0.0f, 0.0f};
        NS::Math::Vector3 m_up{0.0f, 1.0f, 0.0f};
        NS::Math::Vector3 m_triggerCenter{0.0f, 0.0f, 0.0f};
        NS::Math::Vector3 m_triggerExtent{1.0f, 1.0f, 1.0f};
        bool m_lookAtPlayer = false;
    };
} // namespace NS::Scene
