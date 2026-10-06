#include "Game/Game.h"
#include "NSlib/App/Application.h"

#if NS_EDITOR_ENABLED
#include "Editor/Editor.h"
#else
#include "Game/StandaloneLayer.h"
#endif

namespace NS
{

    std::unique_ptr<Application> CreateApplication()
    {
        ApplicationDesc desc{};
        desc.window.title = "NS Game";
        desc.window.size = NS::Size2D{1920, 1080};
#ifdef NS_BUILD_DEBUG
        desc.renderer.enableDebugLayer = true;
#else
        desc.renderer.enableDebugLayer = false;
#endif
        std::unique_ptr<Application> app = std::make_unique<Application>(desc);

        app->AddLayer(std::make_unique<::Game>());
        // プレイ中のカーソルと Esc の持ち主を構成ごとに 1 つ積む。Game は世界だけを持つ
#if NS_EDITOR_ENABLED
        app->AddOverlay(std::make_unique<::Editor>());
#else
        app->AddLayer(std::make_unique<::StandaloneLayer>());
#endif
        return app;
    }

} // namespace NS
