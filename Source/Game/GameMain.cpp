#include "Game/Game.h"
#include "Game/StandaloneLayer.h"
#include "NSlib/App/Application.h"

namespace NS
{

    std::unique_ptr<Application> CreateApplication()
    {
        ApplicationDesc desc{};
        desc.window.title = "Recoilot";
        desc.window.size = Size2D{1920, 1080};
        desc.enableAudio = true;
#ifdef NS_BUILD_DEBUG
        desc.device.enableDebugLayer = true;
#endif
        std::unique_ptr<Application> app = std::make_unique<Application>(desc);

        app->AddLayer(std::make_unique<::Game>());
        // プレイ中のカーソルと Esc は StandaloneLayer が持つ。Game は世界だけを持つ
        app->AddLayer(std::make_unique<::StandaloneLayer>());
        return app;
    }

} // namespace NS
