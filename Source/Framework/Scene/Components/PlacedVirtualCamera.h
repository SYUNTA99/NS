#pragma once

/// @file PlacedVirtualCamera.h
/// @brief NS::Scene::PlacedVirtualCamera — 固定 pose を返す配置型の仮想カメラ
///
/// @details 位置 / 注視点 / up を直接保持し、`EvaluatePose` はそれをそのまま返す
/// (alpha は無視)。エリア進入トリガ等で `SetActive` を切替え、`SetVcamPriority` を
/// follow より高くしておくと、active な間だけ CameraBrain がこれを選んでブレンドする
/// 依存: NS::Math, NS::Scene::VirtualCameraComponent

#include "Framework/Math/Math.h"
#include "Framework/Scene/Components/VirtualCameraComponent.h"

namespace NS::Scene
{
    /// 据え置きカメラ。指定位置から注視点を見る pose を固定で返す
    class PlacedVirtualCamera : public VirtualCameraComponent
    {
    public:
        PlacedVirtualCamera() noexcept;

        /// 据え置き位置と注視点を設定する
        void SetView(const NS::Math::Vector3& position, const NS::Math::Vector3& target) noexcept;
        void SetUpDirection(const NS::Math::Vector3& up) noexcept { m_up = up; }

        [[nodiscard]] const NS::Math::Vector3& ViewPosition() const noexcept { return m_position; }
        [[nodiscard]] const NS::Math::Vector3& ViewTarget() const noexcept { return m_target; }

        [[nodiscard]] CameraPose EvaluatePose(float alpha) const noexcept override;

    private:
        NS::Math::Vector3 m_position{0.0f, 5.0f, -10.0f};
        NS::Math::Vector3 m_target{0.0f, 0.0f, 0.0f};
        NS::Math::Vector3 m_up{0.0f, 1.0f, 0.0f};
    };
} // namespace NS::Scene
