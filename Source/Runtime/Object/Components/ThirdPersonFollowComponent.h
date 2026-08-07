#pragma once

#include "Runtime/Core/Math.h"
#include "Runtime/Object/Components/VirtualCameraComponent.h"
#include "Runtime/Object/Reflection/ObjectRef.h"
#include "Runtime/Object/Scene/SceneData.h"

namespace NS::Object
{
    class CharacterMovementComponent;
    class Transform;
    class GameObject;

    /// @brief Mario 系ジャンプアクションの追従カメラ
    /// @details 実カメラは持たず、追従姿勢を pose として返す。CameraBrain が実カメラへ書く
    /// distance は臨界減衰バネでなめらかに寄せ、マウス / 右スティックで手動回転できる
    /// 感度・反転と idle / run / jump 3 段の自動ズーム距離は setter で調整できる
    /// FOV は基底 VirtualCameraComponent が持つ
    class ThirdPersonFollowComponent : public VirtualCameraComponent
    {
    public:
        ThirdPersonFollowComponent() noexcept;

        void SetTarget(Transform* target) noexcept;
        [[nodiscard]] Transform* Target() const noexcept { return m_target; }

        /// 追従対象の永続参照。データ経由の構築がリフレクション set で書き、OnStart が live へ解決する
        [[nodiscard]] ObjectRef TargetRef() const noexcept { return m_targetRef; }

        /// 追従対象の参照を world の永続 id 解決で引き、Transform と自動ズーム用の Movement を束ねる
        /// 参照未設定 / 解決不可なら SetTarget 済みの直結線を保つ。直結線とデータ経由の両立の要
        void OnStart() override;

        /// 自動ズームの判定に使い、接地と水平速度を見る。null で idle 距離固定
        void SetMovement(const CharacterMovementComponent* movement) noexcept;
        [[nodiscard]] const CharacterMovementComponent* Movement() const noexcept { return m_movement; }

        /// 将来 Settings UI から繋ぐ
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

        /// 距離を手動固定。自動ズームを止め、ClearManualDistance() で戻す
        void SetDistance(float distance) noexcept;
        void ClearManualDistance() noexcept;
        [[nodiscard]] float Distance() const noexcept { return m_distance; }
        [[nodiscard]] bool IsManualDistance() const noexcept { return m_manualDistance; }

        [[nodiscard]] float Yaw() const noexcept { return m_yaw; }
        [[nodiscard]] float Pitch() const noexcept { return m_pitch; }

        /// プレイ開始時の向き。OnStart で現在 yaw/pitch へ写し、以降はプレイ中の手動回転で動く
        [[nodiscard]] float InitialYaw() const noexcept { return m_initialYaw; }
        [[nodiscard]] float InitialPitch() const noexcept { return m_initialPitch; }

        /// editor のギズモで置いたカメラ world 位置から、target 頭を基準に yaw/pitch/距離を逆算し初期姿勢へ書く
        /// target 未解決や距離ほぼ 0 なら何もしない。初期姿勢は data 保存され、プレイ開始時の向きになる
        /// 逆算後の yaw/pitch/distance を現在値にも即反映し、edit 中の EvaluatePose 表示をその場で追従させる
        void SetInitialPoseFromCameraPosition(const NS::Core::Vector3& cameraPosition) noexcept;

        /// fixed step で yaw/pitch・distance spring を更新。最終姿勢は EvaluatePose が返してガタつきを避ける
        void OnUpdate() override;

        /// alpha で補間した target を追う最終姿勢を返す。Brain が選択時に実カメラへ書く
        [[nodiscard]] CameraPose EvaluatePose(float alpha) const noexcept override;

        // 追従カメラの感触を Inspector へ公開する。 毎フレーム読まれるのでライブで効く
        // 追従対象は永続参照で、live への結線は次の rebuild すなわちプレイ突入時の OnStart で効く
        NS_REFLECT_BEGIN(ThirdPersonFollowComponent, VirtualCameraComponent)
        NS_REFLECT_FIELD(m_targetRef, "追従対象")
        NS_REFLECT_FIELD(m_initialYaw, "初期ヨー")
        NS_REFLECT_FIELD(m_initialPitch, "初期ピッチ")
        NS_REFLECT_FIELD(m_springOmega, "バネ角速度")
        NS_REFLECT_FIELD(m_idleDistance, "待機時距離")
        NS_REFLECT_FIELD(m_runDistance, "走行時距離")
        NS_REFLECT_FIELD(m_jumpDistance, "ジャンプ時距離")
        NS_REFLECT_FIELD(m_runSpeedThreshold, "走り判定速度")
        NS_REFLECT_FIELD(m_headHeight, "頭の高さ")
        NS_REFLECT_FIELD(m_sensX, "感度 X")
        NS_REFLECT_FIELD(m_sensY, "感度 Y")
        NS_REFLECT_FIELD(m_stickSensX, "スティック感度 X")
        NS_REFLECT_FIELD(m_stickSensY, "スティック感度 Y")
        NS_REFLECT_FIELD(m_invertX, "反転 X")
        NS_REFLECT_FIELD(m_invertY, "反転 Y")
        NS_REFLECT_FIELD(m_pitchMin, "ピッチ下限")
        NS_REFLECT_FIELD(m_pitchMax, "ピッチ上限")
        NS_REFLECT_ACCESSOR(float, "ファークリップ", FarPlane(), SetFarPlane)
        NS_REFLECT_ACCESSOR(int, "優先度", VcamPriority(), SetVcamPriority)
        NS_REFLECT_END()

    private:
        Transform* m_target = nullptr;                          // 追従対象の Transform (非所有)
        const CharacterMovementComponent* m_movement = nullptr; // 自動ズーム判定用の移動 Component (非所有)
        ObjectRef m_targetRef{};                                // 追従対象の永続参照

        float m_yaw = 0.0f;       // 水平回転角
        float m_pitch = -0.2618f; // 仰俯角

        // プレイ開始時の初期姿勢。 editor のギズモ / Inspector が書き、 OnStart で m_yaw/m_pitch へ写す
        float m_initialYaw = 0.0f;
        float m_initialPitch = -0.2618f;

        float m_distance = 6.0f;        // 現在のカメラ距離
        float m_desiredDistance = 6.0f; // 目標カメラ距離
        float m_springOmega = 6.0f;     // 距離バネの追従の速さ
        bool m_manualDistance = false;  // 距離を手動固定中か

        float m_idleDistance = 5.0f;      // 静止時の距離
        float m_runDistance = 6.0f;       // 走行時の距離
        float m_jumpDistance = 7.0f;      // 空中時の距離
        float m_runSpeedThreshold = 4.0f; // run 距離へ切替える水平速度

        float m_headHeight = 1.2f; // 注視点を頭へ上げる高さ

        float m_sensX = 0.0030f;   // マウス水平感度
        float m_sensY = 0.0030f;   // マウス垂直感度
        float m_stickSensX = 2.0f; // スティック水平感度
        float m_stickSensY = 1.5f; // スティック垂直感度
        bool m_invertX = false;    // 水平反転
        bool m_invertY = false;    // 垂直反転

        float m_pitchMin = -1.396f;  // 仰俯角の下限
        float m_pitchMax = -0.0873f; // 仰俯角の上限
    };

} // namespace NS::Object
