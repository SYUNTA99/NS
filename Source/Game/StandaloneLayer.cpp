#include "Game/StandaloneLayer.h"

#include "NSlib/App/Application.h"
#include "NSlib/Windows/Input.h"

StandaloneLayer::StandaloneLayer() : NS::Layer("Standalone") {}

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
    NS::Application* app = NS::Application::Get();
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
    NS::Application* app = NS::Application::Get();
    if (app == nullptr)
    {
        return;
    }
    if (!app->Input().Keyboard().IsPressed(NS::OS::Key::Escape))
    {
        return;
    }
    const EscapeResponse response = ResolveEscape(app->Window().IsCursorVisible());
    if (response == EscapeResponse::ReleaseCursor)
    {
        app->SetCursorCaptured(false);
        return;
    }
    NS::Application::Quit();
}
