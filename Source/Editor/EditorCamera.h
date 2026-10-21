#pragma once

#include "NSlib/Core/Math.h"
#include "NSlib/CoreObject/ClassObjs/VirtualCamera.h"

namespace NS
{
    namespace Editor
    {
        //! 1 フレームの自由視点入力。感度と距離の追従は EditorCamera の設定を使う
        struct EditorCameraInput
        {
            float lookYawPixels = 0.0f;   // 見回しのマウス X 差分 (ピクセル)
            float lookPitchPixels = 0.0f; // 見回しのマウス Y 差分 (ピクセル)
            float panXPixels = 0.0f;      // 平行移動のマウス X 差分 (ピクセル)
            float panYPixels = 0.0f;      // 平行移動のマウス Y 差分 (ピクセル)
            float wheelNotches = 0.0f;    // ホイール (1 目盛 = 1.0)
            float forwardAxis = 0.0f;     // 前後 (-1..1)。flying 中のみ効く
            float strafeAxis = 0.0f;      // 左右 (-1..1)。flying 中のみ効く
            float verticalAxis = 0.0f;    // 上下 (-1..1)。flying 中のみ効く
            bool boost = false;           // Shift の加速を掛けるか
            float deltaSeconds = 0.0f;    // 経過秒。ズームバネと移動量が使う
            bool flying = false;          // 見回しドラッグ中か。偽なら見回しと移動を捨てる
        };

        //! 編集用の自由視点。実カメラは所有せず、Pose が返す姿勢を呼び手が反映する
        class EditorCamera
        {
        public:
            EditorCamera() noexcept = default;

            //! Platform Input から 1 フレーム分の自由視点入力を集めて姿勢へ適用する。編集モードの毎フレーム呼ぶ
            void Tick() noexcept;

            //! 現在姿勢と投影設定をまとめた pose を返す
            [[nodiscard]] CameraPose Pose() const noexcept;

            //! 投影設定。Pose() が返す pose に載せる
            void SetFovY(Radians fov) noexcept { m_fovY = fov; }
            void SetNearPlane(float nearPlane) noexcept { m_nearPlane = nearPlane; }
            void SetFarPlane(float farPlane) noexcept { m_farPlane = farPlane; }

            // 起動時の視点と、選んだ物へ寄せる時に使う
            void SetCenter(Vector3 center) noexcept;
            void SetDistance(float distance) noexcept;

            [[nodiscard]] Vector3 Center() const noexcept { return m_center; }
            [[nodiscard]] Vector3 ComputeCameraPosition() const noexcept;

            void ApplyOrbit(float yawDelta, float pitchDelta) noexcept;
            void ApplyPan(float panX, float panY) noexcept;
            void ApplyZoom(float zoomDelta) noexcept;
            //! eye を固定したまま yaw / pitch を回す その場の見回し。右ドラッグのフライ視点で使う
            void ApplyLook(float yawDelta, float pitchDelta) noexcept;
            //! forward は視線方向、strafe は画面右、vertical は世界の上下。各入力は -1〜1
            //! dt は秒、speedScale は移動量の倍率
            void ApplyFlyMove(
                float forwardAxis, float strafeAxis, float verticalAxis, float dt, float speedScale = 1.0f) noexcept;

            //! 自由視点入力を 1 フレーム分適用する。感度・ズームバネは自分の設定値で処理する
            //! 設定値に 0 以下・非数・無限が 1 つでもあれば、姿勢を変えずに断る
            void ApplyInput(const EditorCameraInput& input) noexcept;

            // 注視対象の内側へ入り込まないための距離の下限 (m)
            static constexpr float k_MinDistance = 2.0f;
            // 近距離で移動が遅くなりすぎないための距離の下限 (m)
            static constexpr float k_MinMoveDistance = 6.0f;
            static constexpr float k_MaxDistance = 4000.0f;
            // ±90° は up/forward 平行で gimbal lock 寸前のため ±89° でクランプ
            static constexpr float k_PitchMin = -1.553f; // -89°
            static constexpr float k_PitchMax = +1.553f; // +89°

            //! 自由視点の調整値。Inspector で即反映し、保存しない
            struct FeelTuning
            {
                float springOmega = 6.0f; //!< ズーム距離の追従係数 (1/秒)
                float mouseSensOrbit = 0.003f;
                float mouseSensPan = 0.02f;
                // ホイール 1 刻みのズーム倍率。距離に比例して変えるため、近距離でも遠距離でも同じ割合で動く
                float mouseSensZoom = 1.0f;
                float padSensOrbit = 2.5f;
                float padSensPan = 8.0f;
                float padSensZoom = 4.0f;
                // 距離に比例する移動速度。寄った時も k_MinMoveDistance に相当する速さを保つ
                float keyMoveSpeed = 0.6f;
                // Shift の間の移動の倍率。微調整と広い地形の移動を両立する
                float boostMoveScale = 4.0f;
            };

            [[nodiscard]] FeelTuning& Tuning() noexcept { return m_tuning; }
            [[nodiscard]] const FeelTuning& Tuning() const noexcept { return m_tuning; }

        private:
            // Inspector の手入力は欄の範囲を越えるため、使う前に全部の欄が正の有限か確かめる
            [[nodiscard]] bool HasValidTuning() const noexcept;

            Vector3 m_center{0.0f, 0.0f, 0.0f}; // orbit の中心 pivot
            float m_yaw = 0.0f;                     // 方位角 (ラジアン)
            float m_pitch = -0.5236f;               // 仰角 (ラジアン)
            float m_distance = 15.0f;
            float m_desiredDistance = 15.0f; // ズームの目標距離、バネで寄せる

            FeelTuning m_tuning{}; // 自由視点の感触。Inspector がライブで書く

            Radians m_fovY{ToRadians(Degrees{60.0f})};
            float m_nearPlane = 0.1f;
            float m_farPlane = 1000.0f;
        };
    } // namespace Editor
} // namespace NS
