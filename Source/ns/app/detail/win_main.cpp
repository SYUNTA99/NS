#include "ns/app/application.h"
#include "ns/app/scene.h"
#include "ns/core/log_categories.h"
#include "ns/core/logger.h"

#include <Windows.h>

#include <utility>

namespace
{
    /// Logger の Init / Shutdown を RAII で対応付けて、早期 return の追加で Shutdown
    /// 呼び忘れを起こさないようにする。
    class LoggerScope
    {
    public:
        LoggerScope() { ::ns::core::Logger::Init(); }
        ~LoggerScope() { ::ns::core::Logger::Shutdown(); }

        LoggerScope(const LoggerScope&) = delete;
        LoggerScope& operator=(const LoggerScope&) = delete;
        LoggerScope(LoggerScope&&) = delete;
        LoggerScope& operator=(LoggerScope&&) = delete;
    };
} // namespace

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int)
{
    LoggerScope loggerScope;

    auto app = ::ns::app::CreateApplication();
    if (!app)
    {
        NS_LOG_ERROR(::ns::core::LogCat::App, "WinMain: CreateApplication が nullptr");
        return -1;
    }

    auto scene = ::ns::app::CreateInitialScene();
    if (!scene)
    {
        NS_LOG_ERROR(::ns::core::LogCat::App, "WinMain: CreateInitialScene が nullptr");
        return -1;
    }

    return app->Run(std::move(scene));
}
