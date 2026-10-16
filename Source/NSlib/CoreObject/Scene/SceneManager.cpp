#include "NSlib/CoreObject/Scene/SceneManager.h"

#include "NSlib/CoreObject/Scene/Scene.h"
#include "NSlib/CoreObject/Scene/SceneJson.h"

#include <utility>

namespace NS
{

    SceneManager::SceneManager() = default;

    SceneManager::~SceneManager()
    {
        UnloadScene();
    }

    void SceneManager::SetRenderer(Gfx::Renderer* renderer) noexcept
    {
        m_renderer = renderer;
    }

    Scene& SceneManager::LoadScene(nlohmann::json&& scene)
    {
        UnloadScene();

        m_current = std::make_unique<Scene>();
        m_current->SetRenderer(m_renderer);

        m_current->ResetFromJson(std::move(scene));
        return *m_current;
    }

    void SceneManager::UnloadScene()
    {
        if (!m_current)
        {
            return;
        }
        m_current->OnShutdown();
        m_current.reset();
    }

    Scene* SceneManager::Current() noexcept
    {
        return m_current.get();
    }

    const Scene* SceneManager::Current() const noexcept
    {
        return m_current.get();
    }

    void SceneManager::Update()
    {
        if (m_current)
        {
            m_current->OnUpdate();
        }
    }

    void SceneManager::Render()
    {
        if (m_current)
        {
            m_current->OnRender();
        }
    }

} // namespace NS
