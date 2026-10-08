#include "NSlib/Object/Scene/SceneManager.h"

#include "NSlib/Object/Scene/Scene.h"
#include "NSlib/Object/Scene/SceneJson.h"

#include <utility>

namespace NS::Obj
{

    SceneManager::SceneManager() = default;

    SceneManager::~SceneManager()
    {
        UnloadScene();
    }

    void SceneManager::SetAssets(AssetManager* assets) noexcept
    {
        m_assets = assets;
    }

    void SceneManager::SetRenderer(NS::Gfx::Renderer* renderer) noexcept
    {
        m_renderer = renderer;
    }

    Scene& SceneManager::LoadScene(nlohmann::json&& scene)
    {
        UnloadScene();

        m_current = std::make_unique<Scene>();
        m_current->SetAssets(m_assets);
        m_current->SetRenderer(m_renderer);

        m_current->LoadJson(std::move(scene));
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

} // namespace NS::Obj
