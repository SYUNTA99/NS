#pragma once

#include "NSlib/Core/NonCopyable.h"
#include "NSlib/CoreObject/ActorJson.h"

#include <memory>

namespace NS
{
    namespace Gfx
    {
        class D3DDevice;
    }
} // namespace NS

namespace NS
{

    class Scene;

    //! シーン一つを所有し、アプリケーションの更新・描画・終了を転送する
    class SceneManager : public NonCopyable
    {
    public:
        SceneManager();
        ~SceneManager();

        //! 立てる scene へ引き継ぐ装置を非所有で差す。以降 LoadScene する scene が受け取る
        void SetDevice(Gfx::D3DDevice* device) noexcept;

        //! シーンの JSON 文書から scene を立てる。現 scene は破棄してから作り直す
        //! 文書は取込後に用済みで、以降の出所は実体になる
        Scene& LoadScene(nlohmann::json&& scene);

        //! 現 scene を破棄して scene 無し状態にする。未ロードなら何もしない
        void UnloadScene();

        //! 現在有効な scene。未ロードなら nullptr
        [[nodiscard]] Scene* Current() noexcept;
        [[nodiscard]] const Scene* Current() const noexcept;

        //! 現 scene の OnUpdate へ取り次ぐ。未ロードなら何もしない
        void Update();
        //! 現 scene の OnRender へ取り次ぐ。未ロードなら何もしない
        void Render();

    private:
        std::unique_ptr<Scene> m_current;
        Gfx::D3DDevice* m_device = nullptr;
    };

} // namespace NS
