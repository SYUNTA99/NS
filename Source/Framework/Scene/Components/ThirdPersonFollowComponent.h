#pragma once

/// @file ThirdPersonFollowComponent.h
/// @brief Mario 系ジャンプアクションの追従カメラ
///        critically-damped spring で distance を smoothing、マウス/右スティック手動回転、
///        sensitivity/invert と自動ズーム距離を idle / run / jump の 3 段で member 保持し setter で調整可
///        FOV は CameraComponent 側が所有する

#include "Framework/Math/Math.h"
#include "Framework/Scene/Components/VirtualCameraComponent.h"
#include "Framework/Scene/ObjectRef.h"

namespace NS::Scene
{
    class CharacterMovementComponent;
    class Transform;
    class GameObject;

    /// 実カメラは持たず、追従姿勢を pose として返す follow 仮想カメラ。CameraBrain が実カメラへ書く
    class ThirdPersonFollowComponent : public VirtualCameraComponent
    {
    public:
        /// target を受け取って構築する
        explicit ThirdPersonFollowComponent(Transform* target) noexcept;

        void SetTarget(Transform* target) noexcept;
        [[nodiscard]] Transform* Target() const noexcept { return m_target; }

        /// 追従対象の永続参照。データ経由の構築が反射 set で書き、OnStart が実体へ解決する
        [[nodiscard]] ObjectRef TargetRef() const noexcept { return m_targetRef; }

        /// 追従対象の参照を ObjectRefSubsystem で解決し、Transform と自動ズーム用の Movement を束ねる
        /// 参照未設定 / 解決不可なら SetTarget 済みの直結線を保つ。直結線とデータ経由の両立の要
        void OnStart() override;

        /// Dynamic zoom の判定に使い、grounded と horizontal velocity を見る。null で idle 距離固定
        void SetMovement(const CharacterMovementComponent* movement) noexcept;
        [[nodiscard]] const CharacterMovementComponent* Movement() const noexcept { return m_movement; }

        /// 設定。将来 Settings UI から bridge する
        void SetSensX(float radPerPixel) noexcept;
        [[nodiscard]] float SensX() const noexcept { return m_sensX; }
        void SetSensY(float radPerPixel) noexcept;
        [[nodiscard]] float SensY() const noexcept { return m_sensY; }
        void SetInvertX(bool invert) noexcept;
        [[nodiscard]] bool IsInvertX() const noexcept { return m_invertX; }
        void SetInvertY(bool invert) noexcept;
        [[nodiscard]] bool IsInvertY() const noexcept { return m_invertY; }

        /// 自動ズームの距離 3 段を idle / run / jump で設定する。非正値は無視する
        void SetAutoDistances(float idle, float run, float jump) noexcept;
        /// 自動ズームで run 距離へ切替える水平速度しきい値
        void SetRunSpeedThreshold(float speed) noexcept;

        /// 距離を手動固定。dynamic zoom を無効化し、ClearManualDistance() で再有効化する
        void SetDistance(float distance) noexcept;
        void ClearManualDistance() noexcept;
        [[nodiscard]] float Distance() const noexcept { return m_distance; }
        [[nodiscard]] bool IsManualDistance() const noexcept { return m_manualDistance; }

        [[nodiscard]] float Yaw() const noexcept { return m_yaw; }
        [[nodiscard]] float Pitch() const noexcept { return m_pitch; }

        /// fixed step で yaw/pitch・distance spring を更新。最終姿勢は EvaluatePose が返してガタつきを避ける
        void OnUpdate() override;

        /// alpha で補間した target を追う最終姿勢を返す。Brain が選択時に実カメラへ書く。旧 ApplyCameraTransform
        /// に相当する
        [[nodiscard]] CameraPose EvaluatePose(float alpha) const noexcept override;

        // 追従カメラの感触を Inspector へ公開する。 毎フレーム読まれるのでライブで効く
        // Target は永続参照で、live への結線は次の rebuild すなわちプレイ突入時の OnStart で効く
        NS_REFLECT_BEGIN(ThirdPersonFollowComponent)
        NS_REFLECT_FIELD(m_targetRef, "Target")
        NS_REFLECT_FIELD(m_springOmega, "Spring Omega")
        NS_REFLECT_FIELD(m_idleDistance, "Idle Distance")
        NS_REFLECT_FIELD(m_runDistance, "Run Distance")
        NS_REFLECT_FIELD(m_jumpDistance, "Jump Distance")
        NS_REFLECT_FIELD(m_runSpeedThreshold, "Run Speed Threshold")
        NS_REFLECT_FIELD(m_headHeight, "Head Height")
        NS_REFLECT_FIELD(m_sensX, "Sensitivity X")
        NS_REFLECT_FIELD(m_sensY, "Sensitivity Y")
        NS_REFLECT_FIELD(m_stickSensX, "Stick Sens X")
        NS_REFLECT_FIELD(m_stickSensY, "Stick Sens Y")
        NS_REFLECT_FIELD(m_invertX, "Invert X")
        NS_REFLECT_FIELD(m_invertY, "Invert Y")
        NS_REFLECT_FIELD(m_pitchMin, "Pitch Min")
        NS_REFLECT_FIELD(m_pitchMax, "Pitch Max")
        NS_REFLECT_ACCESSOR(float, "Far Plane", FarPlane(), SetFarPlane)
        NS_REFLECT_ACCESSOR(int, "Priority", VcamPriority(), SetVcamPriority)
        NS_REFLECT_END()

    private:
        Transform* m_target = nullptr;
        const CharacterMovementComponent* m_movement = nullptr;
        ObjectRef m_targetRef{};

        float m_yaw = 0.0f;
        float m_pitch = -0.2618f;

        float m_distance = 6.0f;
        float m_desiredDistance = 6.0f;
        float m_springOmega = 6.0f;
        bool m_manualDistance = false;

        float m_idleDistance = 5.0f;
        float m_runDistance = 6.0f;
        float m_jumpDistance = 7.0f;
        float m_runSpeedThreshold = 4.0f;

        float m_headHeight = 1.2f;

        float m_sensX = 0.0030f;
        float m_sensY = 0.0030f;
        float m_stickSensX = 2.0f;
        float m_stickSensY = 1.5f;
        bool m_invertX = false;
        bool m_invertY = false;

        float m_pitchMin = -1.396f;
        float m_pitchMax = -0.0873f;
    };
} // namespace NS::Scene
