#include "Runtime/Object/CameraSubsystem.h"

#include "Runtime/Object/Components/CameraBrainComponent.h"
#include "Runtime/Object/Components/CameraComponent.h"
#include "Runtime/Object/GameObject.h"
#include "Runtime/Object/Scene/Scene.h"

#include <memory>

namespace NS::Object
{
    CameraSubsystem::CameraSubsystem() = default;
    CameraSubsystem::~CameraSubsystem() = default;

    void CameraSubsystem::Initialize(Scene& scene) noexcept
    {
        // 実カメラ 1 個 + Brain を載せる host。Brain が登録 vcam から選んで実カメラへ書く
        m_host = std::make_unique<GameObject>();
        auto* camera = m_host->AddComponent<CameraComponent>();
        camera->SetUp({0.0f, 1.0f, 0.0f});
        m_brain = m_host->AddComponent<CameraBrainComponent>();
        m_host->AttachScene(&scene);
        m_host->OnStart();
    }

    void CameraSubsystem::Deinitialize() noexcept
    {
        if (m_host)
            m_host->OnEndPlay();
        m_brain = nullptr;
        m_host.reset();
    }

    CameraComponent* CameraSubsystem::MainCamera() const noexcept
    {
        if (m_brain != nullptr)
            return m_brain->Camera();
        return nullptr;
    }
} // namespace NS::Object
