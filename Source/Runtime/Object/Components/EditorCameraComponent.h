#pragma once

#include "Runtime/Math/Math.h"
#include "Runtime/Object/Components/VirtualCameraComponent.h"

#include <cstdint>

namespace NS::Object
{
    class GameObject;

    /// @brief 外部が集めた 1 フレーム分の free-fly 入力。装置に依存しない写し
    /// @details 感度とズームバネは EditorCameraComponent 側の設定値を適用する
    struct FreeFlightInput
    {
        float lookYawPixels = 0.0f;   // 見回しのマウス X 差分 (ピクセル)
        float lookPitchPixels = 0.0f; // 見回しのマウス Y 差分 (ピクセル)
        float panXPixels = 0.0f;      // 平行移動のマウス X 差分 (ピクセル)
        float panYPixels = 0.0f;      // 平行移動のマウス Y 差分 (ピクセル)
        float wheelNotches = 0.0f;    // ホイール (1 目盛 = 1.0)
        float forwardAxis = 0.0f;     // 前後 (-1..1)。flying 中のみ効く
        float strafeAxis = 0.0f;      // 左右 (-1..1)。flying 中のみ効く
        float verticalAxis = 0.0f;    // 上下 (-1..1)。flying 中のみ効く
        float speedScale = 1.0f;      // 移動の加速倍率
        float deltaSeconds = 0.0f;    // 経過秒。ズームバネと移動量が使う
        bool flying = false;          // 見回しドラッグ中か。偽なら見回しと移動を捨てる
    };

    /// @brief 編集モード用 free-fly 仮想カメラ Component
    /// @details Spherical 座標 yaw / pitch / distance に center pivot を加えた姿勢を持つ
    /// 実カメラは持たず pose として返す
    /// マウス右ドラッグでその場を見回し、押している間は WASD で前後左右 / Q E で上下に動ける
    /// 中ドラッグで Pan、ホイールで Zoom
    /// ゲームパッドは右スティックで Orbit、左スティックで Pan、LT-RT で Zoom
    /// モード切替時に SetActive(bool) で切り替え、CameraBrain が選択して実カメラへ書く
    /// UI がマウスを掴んでいる間は Input::UiWantsMouse で判定してマウス入力を無視する
    /// pitch / distance は clamp で有限範囲に抑え、NaN / 巨大値での描画クラッシュを防ぐ
    class EditorCameraComponent : public VirtualCameraComponent
    {
    public:
        EditorCameraComponent() noexcept;

        void OnUpdate() override;
        /// free-fly の現在姿勢を返す。alpha は使わない。Brain が選択時に実カメラへ書く
        [[nodiscard]] CameraPose EvaluatePose(float alpha) const noexcept override;

        // プログラム制御用。テストやモード切替で状態を保存・復元するのに使う
        void SetYawPitch(float yaw, float pitch) noexcept;
        void SetCenter(NS::Math::Vector3 center) noexcept;
        void SetDistance(float distance) noexcept;

        [[nodiscard]] float Yaw() const noexcept { return m_yaw; }
        [[nodiscard]] float Pitch() const noexcept { return m_pitch; }
        [[nodiscard]] float Distance() const noexcept { return m_distance; }
        [[nodiscard]] NS::Math::Vector3 Center() const noexcept { return m_center; }
        [[nodiscard]] NS::Math::Vector3 ComputeCameraPosition() const noexcept;

        // 入力を直接注入する。テスト経路や OnUpdate に頼らない外部制御で使う
        void ApplyOrbit(float yawDelta, float pitchDelta) noexcept;
        void ApplyPan(float panX, float panY) noexcept;
        void ApplyZoom(float zoomDelta) noexcept;
        /// eye を固定したまま yaw / pitch を回す その場の見回し。 右ドラッグのフライ視点で使う
        void ApplyLook(float yawDelta, float pitchDelta) noexcept;
        /// 視線方向へのフライ移動。 forward は pitch を含む視線方向、 strafe は画面右、 vertical は world 上下で各軸
        /// -1..1 speedScale は移動量の倍率で Shift 押下時の加速に使う。 既定 1.0 は従来速度
        void ApplyFlyMove(
            float forwardAxis, float strafeAxis, float verticalAxis, float dt, float speedScale = 1.0f) noexcept;

        /// free-fly 入力を 1 フレーム分適用する。 感度・ ズームバネは自分の設定値で処理する
        void ApplyFreeFlightInput(const FreeFlightInput& input) noexcept;

        // free-fly の感触を Inspector へ公開する。 毎フレーム読まれるのでライブで効く
        NS_REFLECT_BEGIN(EditorCameraComponent, VirtualCameraComponent)
        NS_REFLECT_FIELD(m_springOmega, "Spring Omega")
        NS_REFLECT_FIELD(m_mouseSensOrbit, "Mouse Orbit Sens")
        NS_REFLECT_FIELD(m_mouseSensPan, "Mouse Pan Sens")
        NS_REFLECT_FIELD(m_mouseSensZoom, "Mouse Zoom Sens")
        NS_REFLECT_FIELD(m_padSensOrbit, "Pad Orbit Sens")
        NS_REFLECT_FIELD(m_padSensPan, "Pad Pan Sens")
        NS_REFLECT_FIELD(m_padSensZoom, "Pad Zoom Sens")
        NS_REFLECT_FIELD(m_keyMoveSpeed, "Key Move Speed")
        NS_REFLECT_END()

        // 2m 未満は block 内側へ入り描画破綻するので下限は残す。 上限は広い地形を一望できるよう実質無制限まで
        // 開け、 LevelEditorController の far plane と揃えて遠景も映す。 完全な無限は inf / far 越えで全消えを招く
        static constexpr float k_MinDistance = 2.0f;
        // WASD の速さを距離比例で出す時の下限。 これより寄っても 3.6m/s は残り、 細かく詰めるには十分遅い
        static constexpr float k_MinMoveDistance = 6.0f;
        static constexpr float k_MaxDistance = 4000.0f;
        // ±90° は up/forward 平行で gimbal lock 寸前のため ±89° でクランプ
        static constexpr float k_PitchMin = -1.553f; // -89°
        static constexpr float k_PitchMax = +1.553f; // +89°

    private:
        NS::Math::Vector3 m_center{0.0f, 0.0f, 0.0f}; // orbit の中心 pivot
        float m_yaw = 0.0f;                           // 方位角 (ラジアン)
        float m_pitch = -0.5236f;                     // 仰角 (ラジアン)
        float m_distance = 15.0f;                     // center からの現在距離
        float m_desiredDistance = 15.0f;              // ズームの目標距離、バネで寄せる
        float m_springOmega = 6.0f;                   // 距離バネの角速度

        float m_mouseSensOrbit = 0.003f;
        float m_mouseSensPan = 0.02f;
        // 1 wheel notch あたりの zoomDelta 倍率。 ApplyZoom が log scale なので
        // 1.0 で 1 notch = 10% 距離変化、 2.0 で 19%、 0.5 で 5% と直感的に効く
        float m_mouseSensZoom = 1.0f;
        float m_padSensOrbit = 2.5f;
        float m_padSensPan = 8.0f;
        float m_padSensZoom = 4.0f;
        // WASD の 1 秒あたり移動量 = この値 × distance。 distance 比例でズーム量に依らず体感速度を一定に保つ
        // ただし距離は k_MinMoveDistance で下支えするので、 寄り切っても速さは残る
        float m_keyMoveSpeed = 0.6f;
    };

} // namespace NS::Object
