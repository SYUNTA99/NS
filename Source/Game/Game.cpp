#include "Game/MainScene.h"

#include "ns/app/application.h"
#include "ns/app/scene.h"

#include <memory>

namespace ns::app
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

    std::unique_ptr<Scene> CreateInitialScene()
    {
        return std::make_unique<MainScene>();
    }

} // namespace ns::app
