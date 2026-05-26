#include "Framework/Scene/SceneManager.h"

#include "Framework/Scene/SceneBase.h"

#include <utility>

namespace NS::Scene
{

    SceneManager::SceneManager() = default;

    SceneManager::~SceneManager()
    {
        LoadScene(nullptr);
    }

    void SceneManager::LoadScene(std::unique_ptr<SceneBase> scene)
    {
        if (m_current)
        {
            m_current->OnShutdown();
            m_current.reset();
        }
        if (scene)
        {
            m_current = std::move(scene);
            m_current->OnStart();
        }
    }

    SceneBase* SceneManager::Current() noexcept
    {
        return m_current.get();
    }

    const SceneBase* SceneManager::Current() const noexcept
    {
        return m_current.get();
    }

    bool SceneManager::HasScene() const noexcept
    {
        return static_cast<bool>(m_current);
    }

    void SceneManager::Update()
    {
        if (m_current)
            m_current->OnUpdate();
    }

    void SceneManager::Render()
    {
        if (m_current)
            m_current->OnRender();
    }

} // namespace NS::Scene
