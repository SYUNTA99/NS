#include "Game/Game.h"
#include "Runtime/App/Application.h"

#if NS_EDITOR_ENABLED
#include "Editor/Editor.h"
#endif

namespace NS::App
{

    std::unique_ptr<Application> CreateApplication()
    {
        ApplicationDesc desc{};
        desc.window.title = "NS Game";
        desc.window.size = NS::Core::Size2D{1280, 720};
#ifdef NS_BUILD_DEBUG
        desc.renderer.enableDebugLayer = true;
#else
        desc.renderer.enableDebugLayer = false;
#endif
        auto app = std::make_unique<Application>(desc);

        // 合成ルート。 overlay の editor は NS_EDITOR_ENABLED 時だけ積む
        app->AddLayer(std::make_unique<::Game>());
#if NS_EDITOR_ENABLED
        app->AddOverlay(std::make_unique<::Editor>());
#endif
        return app;
    }

} // namespace NS::App
