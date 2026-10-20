#pragma once

#include "NSlib/App/Application.h"
#include "NSlib/App/Layer.h"

#include <functional>
#include <utility>

// Application を要る試しの本体を、積まれた時に 1 回だけ流してアプリを閉じる
class TestCheckLayer : public NS::Layer
{
public:
    explicit TestCheckLayer(std::function<void()> check) : m_check(std::move(check)) {}
    void OnAttach() override
    {
        m_check();
        NS::Application::Quit();
    }

private:
    std::function<void()> m_check;
};
