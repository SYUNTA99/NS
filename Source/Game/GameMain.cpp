#include "GameCore/Game.h"

#if NS_EDITOR_ENABLED
#include "Editor/EditorLayer.h"
#endif

#include <memory>

namespace NS::App
{

    std::unique_ptr<Application> CreateApplication()
    {
        ApplicationDesc desc{};
        desc.window.title = "NS Game";
        desc.window.size = NS::Math::Size2D{1280, 720};
#ifdef NS_BUILD_DEBUG
        desc.renderer.enableDebugLayer = true;
#else
        desc.renderer.enableDebugLayer = false;
#endif
        auto app = std::make_unique<Application>(desc);

        // 合成ルート。 ゲーム本体 GameCore は常時、 overlay の editor は editor 構成のみ積む
        app->AddLayer(std::make_unique<::Game>());
#if NS_EDITOR_ENABLED
        app->AddOverlay(std::make_unique<::EditorLayer>());
#endif
        return app;
    }

} // namespace NS::App
