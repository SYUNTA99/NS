#pragma once

/// @file VirtualCameraComponent.h
/// @brief NS::Scene::VirtualCameraComponent — 実カメラを持たない「仮想カメラ」基底
///
/// @details 描画も実 Camera 所有もせず、`EvaluatePose(alpha)` で desired pose
/// (位置 / 注視点 / up + 投影設定) を返すだけ。`CameraBrainComponent` が登録済みの
/// vcam から最高優先度の active なものを選び、その pose を 1 個の実 CameraComponent へ書く
/// fixed-step の状態更新は `OnUpdate` で行い、最終姿勢は `EvaluatePose` で返す
/// (follow 系は render 時に alpha で補間 target を追うため、姿勢決定を pose 返却へ分離する)
/// 依存: NS::Math, NS::Scene::Component

#include "Framework/Math/Math.h"
#include "Framework/Scene/Component.h"

namespace NS::Scene
{
    /// 仮想カメラが返す 1 フレーム分のカメラ姿勢 + 投影設定。Brain が実 Camera へそのまま書く
    struct CameraPose
    {
        NS::Math::Vector3 position{0.0f, 0.0f, -5.0f};
        NS::Math::Vector3 target{0.0f, 0.0f, 0.0f};
        NS::Math::Vector3 up{0.0f, 1.0f, 0.0f};
        NS::Math::Radians fovY{NS::Math::ToRadians(NS::Math::Degrees{60.0f})};
        float nearPlane = 0.1f;
        float farPlane = 1000.0f;
    };

    /// 描画しないカメラ定義。CameraBrain が選んで実 CameraComponent を駆動する
    class VirtualCameraComponent : public Component
    {
    public:
        /// tick 順は Camera 帯 (派生が TickPriority::Camera を渡す)。vcam 選択用の優先度は別 (SetVcamPriority)
        explicit VirtualCameraComponent(int tickPriority) noexcept : Component(tickPriority) {}
        ~VirtualCameraComponent() noexcept override;

        /// この vcam の最終姿勢を返す。alpha は補間係数 (follow 系が補間 target に使う、free-fly は無視可)
        [[nodiscard]] virtual CameraPose EvaluatePose(float alpha) const noexcept = 0;

        /// Brain の選択優先度。大きいほど優先、同値は登録順。active な vcam の中から最大が選ばれる
        void SetVcamPriority(int priority) noexcept { m_vcamPriority = priority; }
        [[nodiscard]] int VcamPriority() const noexcept { return m_vcamPriority; }

        /// 投影設定 (vcam ごとに保持し EvaluatePose の pose へ載せる。play=far100 / editor=far200 等の差を吸収)
        void SetFovY(NS::Math::Radians fov) noexcept { m_fovY = fov; }
        [[nodiscard]] NS::Math::Radians FovY() const noexcept { return m_fovY; }
        void SetNearPlane(float nearPlane) noexcept { m_nearPlane = nearPlane; }
        void SetFarPlane(float farPlane) noexcept { m_farPlane = farPlane; }

    protected:
        /// 派生が position/target/up を渡すと、保持中の投影設定を載せた CameraPose を返す helper
        [[nodiscard]] CameraPose MakePose(const NS::Math::Vector3& position,
                                          const NS::Math::Vector3& target,
                                          const NS::Math::Vector3& up) const noexcept
        {
            CameraPose pose{};
            pose.position = position;
            pose.target = target;
            pose.up = up;
            pose.fovY = m_fovY;
            pose.nearPlane = m_nearPlane;
            pose.farPlane = m_farPlane;
            return pose;
        }

    private:
        int m_vcamPriority = 0;
        NS::Math::Radians m_fovY{NS::Math::ToRadians(NS::Math::Degrees{60.0f})};
        float m_nearPlane = 0.1f;
        float m_farPlane = 1000.0f;
    };
} // namespace NS::Scene
