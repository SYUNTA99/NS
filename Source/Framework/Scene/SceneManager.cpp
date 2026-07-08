#include "Framework/Scene/SceneManager.h"

#include "Framework/Scene/SceneBase.h"


namespace NS::Scene
{

    SceneManager::SceneManager() = default;

    SceneManager::~SceneManager()
    {
        LoadScene(nullptr);
    }

    void SceneManager::SetSubsystemProvider(ISubsystemProvider* provider) noexcept
    {
        m_provider = provider;
    }

    void SceneManager::LoadScene(std::unique_ptr<SceneBase> scene)
    {
        if (m_current)
        {
            m_current->OnShutdown();
            // service は GameObject の OnEndPlay より後に落とす。OnShutdown 中は解決できる状態を保つ
            m_current->DeinitSceneSubsystems();
            m_current.reset();
        }
        if (scene)
        {
            m_current = std::move(scene);
            // service は OnStart より先に用意する。OnStart 中の GetSubsystem を解決可能にする
            m_current->SetSubsystemProvider(m_provider);
            m_current->CreateSceneSubsystems();
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
