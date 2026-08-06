#pragma once

#include "Runtime/Core/NonCopyable.h"

#include <memory>

namespace NS::Object
{
    class CameraBrainComponent;
    class CameraComponent;
    class GameObject;
    class Scene;

    /// @brief 実カメラ 1 個 + Brain を所有し「いまどのカメラで描くか」を決める。vcam は非所有
    /// @details 描画と入力の camera 相対処理が camera メンバの在り処を知らずに済むよう、
    /// 実 `CameraComponent` と `CameraBrainComponent` を載せた host を所有・公開する
    /// 利用側は Brain() 経由で vcam を登録する。vcam そのものの寿命は登録側が握る
    /// Scene が直接所有し、Initialize 前は Brain() / MainCamera() とも nullptr を返す
    /// 依存: NS::Object::GameObject
    class CameraSubsystem final : public NS::Core::NonCopyable
    {
    public:
        CameraSubsystem();
        ~CameraSubsystem();

        /// 実カメラ + Brain を載せた host を組んで開始する。以降 Brain() / MainCamera() が有効になる
        void Initialize(Scene& scene) noexcept;

        /// host を解体して破棄する。登録されたままの vcam 参照も host ごと消える
        void Deinitialize() noexcept;

        /// シーンの描画を駆動する brain。Initialize 前は nullptr
        [[nodiscard]] CameraBrainComponent* Brain() const noexcept { return m_brain; }

        /// brain が駆動する実カメラ。Initialize 前は nullptr
        [[nodiscard]] CameraComponent* MainCamera() const noexcept;

    private:
        std::unique_ptr<GameObject> m_host;
        CameraBrainComponent* m_brain = nullptr;
    };
} // namespace NS::Object
