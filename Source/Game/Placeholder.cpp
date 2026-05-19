#include "ns/app/application.h"
#include "ns/app/scene.h"

#include <memory>

//  で Phase1CubeScene + 本実装 Application desc に置換予定。
// 現状は ns::app の WinMain がリンクできるよう最小 stub を提供する。

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
        return std::make_unique<Scene>();
    }

} // namespace ns::app
