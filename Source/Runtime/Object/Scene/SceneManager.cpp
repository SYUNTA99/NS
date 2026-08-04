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

        // OnStart 中に GetSubsystem を解決できるよう、Subsystem は OnStart より先に用意する
        m_current->CreateSceneSubsystems();
        m_current->OnStart();

        // subsystem が揃ってから world を組む。逆にすると vcam の Brain 登録と objectId 登録が丸ごと飛ぶ
        m_current->LoadFromData(std::move(data));
        return *m_current;
    }

    void SceneManager::UnloadScene()
    {
        if (!m_current)
            return;

        m_current->OnShutdown();
        // OnShutdown 中に解決できる状態を保つため、GameObject の OnEndPlay より後に Subsystem を落とす
        m_current->DeinitSceneSubsystems();
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
