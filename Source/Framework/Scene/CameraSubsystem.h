#pragma once

/// @file CameraSubsystem.h
/// @brief NS::Scene::CameraSubsystem — 「いまどのカメラで描くか」のシーン単位の検索窓
///
/// @details 描画と入力の camera 相対処理が scene の具体型や camera メンバの在り処を
/// 知らずに済むよう、アクティブな `CameraBrainComponent` の非所有参照をシーン service
/// として公開する。brain の所有と寿命は登録側が握り、破棄前に `SetBrain(nullptr)` で
/// 解除する。未登録の間は Brain() / MainCamera() とも nullptr を返す
/// 依存: NS::Scene::SceneSubsystem

#include "Framework/Scene/SceneSubsystem.h"

namespace NS::Scene
{
    class CameraBrainComponent;
    class CameraComponent;

    /// アクティブな camera brain の在り処を握るシーン service。brain は非所有参照
    class CameraSubsystem final : public SceneSubsystem
    {
    public:
        /// シーンの描画を駆動する brain を登録する。nullptr で解除。brain 破棄前に必ず解除する
        void SetBrain(CameraBrainComponent* brain) noexcept { m_brain = brain; }

        /// 登録中の brain。未登録は nullptr
        [[nodiscard]] CameraBrainComponent* Brain() const noexcept { return m_brain; }

        /// brain が駆動する実カメラ。brain 未登録か実カメラ未解決なら nullptr
        [[nodiscard]] CameraComponent* MainCamera() const noexcept;

        /// シーン破棄で参照だけ手放す。brain 実体の破棄は所有者に任せる
        void Deinitialize() noexcept override { m_brain = nullptr; }

    private:
        CameraBrainComponent* m_brain = nullptr;
    };
} // namespace NS::Scene
