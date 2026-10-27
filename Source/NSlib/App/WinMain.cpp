#include "NSlib/App/Application.h"
#include "NSlib/Core/Logger.h"
#include "NSlib/Core/NonCopyable.h"
#include "NSlib/Core/Windows/Filesystem.h"

#include <windows.h>

namespace
{
    class LoggerScope : public ::NS::NonCopyable
    {
    public:
        LoggerScope()
        {
            ::NS::LoggerDesc desc;
            desc.logName = "game";
            desc.rotateOnOpen = true;
#if defined(NS_SHIPPING)
            desc.logDirectory = ::NS::OS::FileSystem::GetExeDirectory();
#else
            desc.logDirectory = ::NS::OS::FileSystem::Combine(::NS::OS::FileSystem::ContentRoot(), "build");
#endif
            ::NS::Logger::Init(desc);
        }
        ~LoggerScope() { ::NS::Logger::Shutdown(); }
    };
} // namespace

int WINAPI WinMain(_In_ HINSTANCE, _In_opt_ HINSTANCE, _In_ LPSTR, _In_ int)
{
    LoggerScope loggerScope;

    std::unique_ptr<::NS::Application> app = ::NS::CreateApplication();
    if (!app)
    {
        NS_LOG_ERROR(App, "WinMain: CreateApplication が nullptr");
        return -1;
    }
    if (!app->IsValid())
    {
        NS_LOG_ERROR(App, "WinMain: Application 構築失敗");
        return -1;
    }

    return app->Run();
}
