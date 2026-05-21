#include "Framework/App/Application.h"
#include "Framework/App/Scene.h"
#include "Framework/Core/LogCategories.h"
#include "Framework/Core/Logger.h"

#include "Framework/Framework.h"

#include <utility>

namespace
{
    /// Logger の Init / Shutdown を RAII で対応付けて、早期 return の追加で Shutdown
    /// 呼び忘れを起こさないようにする。
    class LoggerScope
    {
    public:
        LoggerScope() { ::NS::Core::Logger::Init(); }
        ~LoggerScope() { ::NS::Core::Logger::Shutdown(); }

        LoggerScope(const LoggerScope&) = delete;
        LoggerScope& operator=(const LoggerScope&) = delete;
        LoggerScope(LoggerScope&&) = delete;
        LoggerScope& operator=(LoggerScope&&) = delete;
    };
} // namespace

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int)
{
    LoggerScope loggerScope;

    auto app = ::NS::App::CreateApplication();
    if (!app)
    {
        NS_LOG_ERROR(::NS::Core::LogCat::App, "WinMain: CreateApplication が nullptr");
        return -1;
    }

    auto scene = ::NS::App::CreateInitialScene();
    if (!scene)
    {
        NS_LOG_ERROR(::NS::Core::LogCat::App, "WinMain: CreateInitialScene が nullptr");
        return -1;
    }

    return app->Run(std::move(scene));
}
