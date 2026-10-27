#include "Game/StandaloneLayer.h"

#include "NSlib/App/Application.h"
#include "NSlib/Core/Windows/InputReader.h"

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
    const NS::OS::InputPresses presses = m_inputReader.Read();
    NS::Application* app = NS::Application::Get();
    if (app == nullptr)
    {
        return;
    }
    if (!presses.WasPressed(NS::OS::Key::Escape))
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
