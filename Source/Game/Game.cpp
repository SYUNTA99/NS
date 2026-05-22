#include "Game/MainScene.h"

#include "Framework/App/Application.h"
#include "Framework/Scene/RootScene.h"

#include <memory>

namespace NS::App
{

    std::unique_ptr<Application> CreateApplication()
    {
        ApplicationDesc desc{};
        desc.window.title = "NS Game";
        desc.window.width = 1280;
        desc.window.height = 720;
#ifdef NS_BUILD_DEBUG
        desc.renderer.enableDebugLayer = true;
#else
        desc.renderer.enableDebugLayer = false;
#endif
        return std::make_unique<Application>(desc);
    }

    std::unique_ptr<NS::Scene::RootScene> CreateInitialScene()
    {
        return std::make_unique<MainScene>();
    }

} // namespace NS::App
