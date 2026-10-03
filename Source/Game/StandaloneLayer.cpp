#include "Game/StandaloneLayer.h"

#include "Runtime/App/Application.h"
#include "Runtime/Platform/Input.h"

StandaloneLayer::StandaloneLayer() : NS::App::Layer("Standalone") {}

StandaloneLayer::EscapeResponse StandaloneLayer::ResolveEscape(bool cursorVisible) noexcept
{
    if (!cursorVisible)
    {
        return EscapeResponse::ReleaseCursor;
    }
    return EscapeResponse::Quit;
}

void StandaloneLayer::OnAttach()
{
    NS::App::Application* app = NS::App::Application::Get();
    if (app == nullptr)
    {
        NS_LOG_ERROR(Game, "StandaloneLayer::OnAttach: Application::Get()==null");
        return;
    }
    // Esc で出すまで非表示のまま
    app->SetCursorCaptured(true);
}

void StandaloneLayer::OnUpdate()
{
    NS::App::Application* app = NS::App::Application::Get();
    if (app == nullptr)
    {
        return;
    }
    if (!app->Input().Keyboard().IsPressed(NS::Platform::Key::Escape))
    {
        return;
    }
    const EscapeResponse response = ResolveEscape(app->Window().IsCursorVisible());
    if (response == EscapeResponse::ReleaseCursor)
    {
        app->SetCursorCaptured(false);
        return;
    }
    NS::App::Application::Quit();
}
