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

    void SceneManager::SetDevice(Gfx::D3DDevice* device) noexcept
    {
        m_device = device;
    }

    Scene& SceneManager::LoadScene(nlohmann::json&& scene)
    {
        UnloadScene();

        m_current = std::make_unique<Scene>();
        m_current->SetDevice(m_device);

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
