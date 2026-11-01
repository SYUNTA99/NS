#include "NSlib/CoreObject/Scene/SceneManager.h"

#include "NSlib/CoreObject/Scene/Scene.h"
#include "NSlib/CoreObject/Scene/SceneJson.h"
#include "NSlib/Core/Logger.h"

#include <string>
#include <utility>

namespace NS
{

    SceneManager::SceneManager() = default;

    SceneManager::~SceneManager()
    {
        UnloadScene();
    }

    void SceneManager::SetDevice(Gfx::D3DDevice* device) noexcept { m_device = device; }

    void SceneManager::SetAudioDevice(Audio::AudioDevice* device) noexcept
    {
        m_audioDevice = device;
    }

    std::optional<nlohmann::json> SceneManager::ReadSceneFile(std::string_view name)
    {
        const std::optional<std::string> path = BuildLevelPath(name);
        if (!path)
        {
            NS_LOG_ERROR(Scene, "LoadScene: シーンの名前が使えない: {}", name);
            return std::nullopt;
        }
        nlohmann::json data;
        if (!LoadSceneFromJsonFile(data, *path))
        {
            return std::nullopt; // 理由は LoadSceneFromJsonFile が記録する
        }
        return data;
    }

    Scene& SceneManager::StartScene(std::unique_ptr<Scene> scene, nlohmann::json data)
    {
        m_current = std::move(scene);
        m_current->SetDevice(m_device);
        m_current->SetAudioDevice(m_audioDevice);

        m_current->ResetFromJson(std::move(data));
        m_current->StartPlay();
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

    Scene* SceneManager::Current() noexcept { return m_current.get(); }

    const Scene* SceneManager::Current() const noexcept { return m_current.get(); }

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
