#pragma once

/// @file CameraSubsystem.h
/// @brief NS::Scene::CameraSubsystem — 実カメラ 1 個 + Brain を所有する「いまどのカメラで描くか」の窓口
///
/// @details 描画と入力の camera 相対処理が scene の具体型や camera メンバの在り処を
/// 知らずに済むよう、実 `CameraComponent` と `CameraBrainComponent` を載せた host を
/// シーン service として所有・公開する。vcam の登録は消費者が Brain() 経由で行い、
/// vcam 実体の寿命は登録側が握る。Initialize 前は Brain() / MainCamera() とも nullptr を返す
/// 依存: NS::Scene::SceneSubsystem, NS::Scene::GameObject

#include "Framework/Scene/SceneSubsystem.h"

#include <memory>

namespace NS::Scene
{
    class CameraBrainComponent;
    class CameraComponent;
    class GameObject;

    /// 実カメラ + Brain を載せた host を所有するシーン service。vcam は非所有
    class CameraSubsystem final : public SceneSubsystem
    {
    public:
        CameraSubsystem();
        ~CameraSubsystem() override;

        /// 実カメラ + Brain を載せた host を組んで開始する。以降 Brain() / MainCamera() が有効になる
        void Initialize(SceneBase& scene) noexcept override;

        /// host を畳んで破棄する。登録されたままの vcam 参照も host ごと消える
        void Deinitialize() noexcept override;

        /// シーンの描画を駆動する brain。Initialize 前は nullptr
        [[nodiscard]] CameraBrainComponent* Brain() const noexcept { return m_brain; }

        /// brain が駆動する実カメラ。Initialize 前は nullptr
        [[nodiscard]] CameraComponent* MainCamera() const noexcept;

    private:
        std::unique_ptr<GameObject> m_host;
        CameraBrainComponent* m_brain = nullptr;
    };
} // namespace NS::Scene
