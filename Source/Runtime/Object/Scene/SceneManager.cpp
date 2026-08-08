#include "Runtime/Object/Scene/SceneManager.h"

#include "Runtime/Object/Scene/Scene.h"
#include "Runtime/Object/Scene/SceneData.h"

#include <utility>

namespace NS::Object
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

    void SceneManager::SetRenderer(NS::Graphics::Renderer* renderer) noexcept
    {
        m_renderer = renderer;
    }

    Scene& SceneManager::LoadScene(SceneData&& data)
    {
        UnloadScene();

        m_current = std::make_unique<Scene>();
        m_current->SetAssets(m_assets);
        m_current->SetRenderer(m_renderer);

        m_current->OnStart();
        m_current->LoadFromData(std::move(data));
        return *m_current;
    }

    void SceneManager::UnloadScene()
    {
        if (!m_current)
            return;

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

} // namespace NS::Object
