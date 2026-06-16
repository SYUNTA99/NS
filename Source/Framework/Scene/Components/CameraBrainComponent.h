#pragma once

/// @file CameraBrainComponent.h
/// @brief NS::Scene::CameraBrainComponent — 登録 vcam から 1 個選び実 Camera を駆動する
///
/// @details 実 `CameraComponent` 1 個を参照し、登録済み `VirtualCameraComponent` のうち
/// active かつ最高 `VcamPriority` のものを毎フレーム選び、その `EvaluatePose(alpha)` を
/// 実カメラへ書く。描画 / aspect 設定 / PlayerInput の forward 取得は全てこの Brain 経由に
/// 集約する (旧 2 系統カメラの窓口を 1 本化)。active が入れ替わると `SetBlendDuration` 秒かけて
/// 旧 pose から新 vcam の pose へ ease-in-out で繋ぐ (0 で即時カット)
/// 依存: NS::Math, NS::Scene::Component / CameraComponent / VirtualCameraComponent

#include "Framework/Math/Math.h"
#include "Framework/Scene/Component.h"
#include "Framework/Scene/Components/VirtualCameraComponent.h"

#include <vector>

namespace NS::Scene
{
    class CameraComponent;

    /// 仮想カメラ群を束ね、選ばれた 1 個の pose を実カメラへ流す
    class CameraBrainComponent : public Component
    {
    public:
        CameraBrainComponent() noexcept;

        /// 出力先の実カメラを注入する (Brain と同じ GameObject に乗せる想定)
        void SetCamera(CameraComponent* camera) noexcept { m_camera = camera; }

        /// 候補 vcam を登録する (null / 重複は無視)。寿命は呼出側が支配する非所有参照
        void AddVirtualCamera(VirtualCameraComponent* vcam);

        /// 登録済み vcam を外す (未登録 / null は無視)。外した vcam が active 中なら選び直す
        /// 寿命を呼出側が握る area camera を破棄する前に呼んで dangling を防ぐ
        void RemoveVirtualCamera(VirtualCameraComponent* vcam) noexcept;

        /// active 切替時のブレンド秒数。0 以下で即時カット。負値は 0 に丸める
        void SetBlendDuration(float seconds) noexcept;
        [[nodiscard]] float BlendDuration() const noexcept { return m_blendDuration; }

        /// fixed step で active 切替を検出しブレンドタイマーを進める (描画はしない)
        void OnUpdate() override;

        /// 現在の active vcam の EvaluatePose(alpha) を (ブレンド中なら旧 pose と補間して) 実カメラへ書く
        /// fixed step は alpha=1、render は FrameTimer::Alpha() を渡す。タイマーは進めない
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

        CameraPose m_lastPose{};  // 直近 Evaluate が実カメラへ書いた pose。切替時のブレンド始点になる
        CameraPose m_blendFrom{}; // ブレンド開始時にスナップした旧 pose
        float m_blendDuration = 0.35f;
        float m_blendElapsed = 0.0f;
        bool m_blending = false;
    };
} // namespace NS::Scene
