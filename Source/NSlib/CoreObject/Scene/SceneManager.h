#pragma once

#include "NSlib/Core/NonCopyable.h"
#include "NSlib/CoreObject/Scene/SceneJson.h"

#include <memory>
#include <optional>
#include <string_view>
#include <utility>

namespace NS
{
    namespace Gfx
    {
        class D3DDevice;
    }
} // namespace NS

namespace NS
{
    namespace Audio
    {
        class AudioDevice;
    } // namespace Audio
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
        //! 立てる scene へ引き継ぐ音の装置を非所有で差す。以降 LoadScene する scene が受け取る
        void SetAudioDevice(Audio::AudioDevice* device) noexcept;

        //! @brief 名前のシーン (BuildLevelPath のパス) を読み、TScene を立てて StartPlay まで済ませる
        //! @details 現 scene は読めた後に破棄してから立てる
        //! @return 立てたシーン。名前が使えない時と読めない時は nullptr で、現在のシーンを保つ
        template <typename TScene> TScene* LoadScene(std::string_view name)
        {
            std::optional<nlohmann::json> data = ReadSceneFile(name);
            if (!data)
            {
                return nullptr;
            }
            UnloadScene();
            return &static_cast<TScene&>(StartScene(std::make_unique<TScene>(), std::move(*data)));
        }

        //! 何も置かない TScene を立てて StartPlay まで済ませる。現 scene は破棄してから立てる
        template <typename TScene> TScene& LoadEmptyScene()
        {
            UnloadScene();
            return static_cast<TScene&>(StartScene(std::make_unique<TScene>(), MakeSceneJson()));
        }

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
        //! 名前のシーンのファイルを読む。名前が使えない時と読めない時は ERROR を出して nullopt
        [[nodiscard]] static std::optional<nlohmann::json> ReadSceneFile(std::string_view name);

        //! scene に装置を渡し、文書から組んで StartPlay を呼び、現 scene にする
        Scene& StartScene(std::unique_ptr<Scene> scene, nlohmann::json data);

        std::unique_ptr<Scene> m_current;
        Gfx::D3DDevice* m_device = nullptr;
        Audio::AudioDevice* m_audioDevice = nullptr;
    };

} // namespace NS
