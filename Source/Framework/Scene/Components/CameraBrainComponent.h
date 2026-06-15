#pragma once

/// @file CameraBrainComponent.h
/// @brief NS::Scene::CameraBrainComponent — 登録 vcam から 1 個選び実 Camera を駆動する
///
/// @details 実 `CameraComponent` 1 個を参照し、登録済み `VirtualCameraComponent` のうち
/// active かつ最高 `VcamPriority` のものを毎フレーム選び、その `EvaluatePose(alpha)` を
/// 実カメラへ書く。描画 / aspect 設定 / PlayerInput の forward 取得は全てこの Brain 経由に
/// 集約する (旧 2 系統カメラの窓口を 1 本化)。現状は instant cut で切替時のブレンドは持たない
/// 依存: NS::Math, NS::Scene::Component / CameraComponent / VirtualCameraComponent

#include "Framework/Math/Math.h"
#include "Framework/Scene/Component.h"

#include <vector>

namespace NS::Scene
{
    class CameraComponent;
    class VirtualCameraComponent;

    /// 仮想カメラ群を束ね、選ばれた 1 個の pose を実カメラへ流す
    class CameraBrainComponent : public Component
    {
    public:
        CameraBrainComponent() noexcept;

        /// 出力先の実カメラを注入する (Brain と同じ GameObject に乗せる想定)
        void SetCamera(CameraComponent* camera) noexcept { m_camera = camera; }

        /// 候補 vcam を登録する (null / 重複は無視)。寿命は呼出側が支配する非所有参照
        void AddVirtualCamera(VirtualCameraComponent* vcam);

        /// active な vcam から最高優先度を選び、その EvaluatePose(alpha) を実カメラへ書く
        /// fixed step は alpha=1、render は FrameTimer::Alpha() を渡す
        void Evaluate(float alpha) noexcept;

        /// Evaluate 後に有効。選ばれている vcam (無ければ nullptr)
        [[nodiscard]] VirtualCameraComponent* ActiveVirtualCamera() const noexcept { return m_active; }

        /// 実カメラへの pass-through。描画 / 半透明ソート / PlayerInput forward の接続先
        [[nodiscard]] NS::Math::Matrix ViewProjection() const noexcept;
        [[nodiscard]] NS::Math::Vector3 ForwardHorizontal() const noexcept;
        [[nodiscard]] CameraComponent* Camera() const noexcept { return m_camera; }

    private:
        [[nodiscard]] VirtualCameraComponent* SelectActive() const noexcept;

        CameraComponent* m_camera = nullptr;
        std::vector<VirtualCameraComponent*> m_vcams;
        VirtualCameraComponent* m_active = nullptr;
    };
} // namespace NS::Scene
